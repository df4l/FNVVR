#include "../d3d9_device.h"

#include "../../util/log/log.h"
#include "../../util/util_env.h"
#include "../../util/util_time.h"

#include "vr_crash_log.h"
#include "vr_d3d9_bridge.h"
#include "vr_frame_submitter.h"

namespace dxvk {

  namespace {

    /// A frame this long with the backend is reported as a hang
    constexpr auto HangTime    = std::chrono::seconds(3);
    constexpr auto WatchPeriod = std::chrono::seconds(1);

    const char* stepName(uint32_t step) {
      static const char* names[] = {
        "waiting for DXVK to record the frame", "locking the queue",
        "handing the panels over", "handing the eyes over", "unlocking the queue",
      };

      return step < std::size(names) ? names[step] : "unknown";
    }

  }

  VrFrameSubmitter::VrFrameSubmitter(IDirect3DDevice9* device, IVRBackend& backend)
  : m_device(device), m_deviceEx(static_cast<D3D9DeviceEx*>(device)), m_backend(backend) { }


  VrFrameSubmitter::~VrFrameSubmitter() {
    finish();
    stopThread();
  }


  void VrFrameSubmitter::setThreaded(bool threaded) {
    if (threaded == m_threaded)
      return;

    finish();

    if (!threaded)
      stopThread();

    m_threaded = threaded;
  }


  void VrFrameSubmitter::finish() {
    if (!m_thread.joinable())
      return;

    std::unique_lock<dxvk::mutex> lock(m_mutex);
    m_cond.wait(lock, [this] { return !m_pending; });

    // The textures are released here: the last reference may destroy one,
    // which belongs on the game thread
    m_job = Job();
  }


  void VrFrameSubmitter::beginFrame() {
    finish();

    m_next = Job();
    m_collecting = true;
  }


  bool VrFrameSubmitter::submitPanel(
          VrPanelId             id,
          IDirect3DTexture9*    texture,
    const VrPose&               pose,
          float                 width,
          VrPanelAnchor         anchor,
          bool                  premultiplied) {
    if (!m_collecting) {
      finish();
      return VrD3D9Bridge::submitPanel(m_device, m_backend, id, texture, pose, width, anchor, premultiplied);
    }

    PanelOp op;
    op.id = id;
    op.panel.pose   = pose;
    op.panel.width  = width;
    op.panel.anchor = anchor;
    op.panel.premultiplied = premultiplied;

    if (!VrD3D9Bridge::queryImage(texture, op.texture, op.panel.image))
      return false;

    m_next.panels.push_back(std::move(op));
    return true;
  }


  void VrFrameSubmitter::hidePanel(VrPanelId id) {
    if (!m_collecting) {
      finish();
      m_backend.hidePanel(id);
      return;
    }

    PanelOp op;
    op.id   = id;
    op.hide = true;
    m_next.panels.push_back(std::move(op));
  }


  bool VrFrameSubmitter::submitFrame(
          IDirect3DTexture9*    eyes[VrEyeCount],
    const std::array<VrEyeView, VrEyeCount>& views,
          int64_t               displayTime) {
    Job job = std::move(m_next);
    m_next = Job();
    m_collecting = false;

    job.hasEyes     = true;
    job.displayTime = displayTime;
    job.frame.views = views;
    job.frame.displayTime = displayTime;

    for (uint32_t i = 0; i < VrEyeCount; i++)
      job.hasEyes &= VrD3D9Bridge::queryImage(eyes[i], job.eyes[i], job.frame.images[i]);

    // One flush gets the panels and eyes to the CS thread, the wait for
    // it is the submission thread's
    job.flushSeq = m_deviceEx->FlushForVr();

    if (!m_threaded) {
      run(job);
      return job.hasEyes;
    }

    if (!m_thread.joinable())
      startThread();

    bool hasEyes = job.hasEyes;

    std::unique_lock<dxvk::mutex> lock(m_mutex);
    m_cond.wait(lock, [this] { return !m_pending; });
    m_job      = std::move(job);
    m_pending  = true;
    m_handedAt = high_resolution_clock::now();
    m_gameThread = GetCurrentThreadId();
    m_cond.notify_all();
    return hasEyes;
  }


  void VrFrameSubmitter::run(Job& job) {
    auto start = high_resolution_clock::now();

    // Rendering has to reach the queue before the backend reads the images.
    // Locking the submission queue waits for DXVK's submission thread to
    // drain, so everything recorded up to the flush is queued ahead of us.
    m_step = uint32_t(Step::Flush);
    m_deviceEx->WaitForVrFlush(job.flushSeq);
    m_step = uint32_t(Step::Queue);
    m_deviceEx->GetDXVKDevice()->lockSubmission();

    double syncMs = std::chrono::duration<double, std::milli>(high_resolution_clock::now() - start).count();

    m_step = uint32_t(Step::Panels);

    for (const PanelOp& op : job.panels) {
      if (op.hide)
        m_backend.hidePanel(op.id);
      else
        m_backend.submitPanel(op.id, op.panel);
    }

    m_step = uint32_t(Step::Eyes);

    if (job.hasEyes) {
      job.frame.syncMs = syncMs;

      if (!m_backend.submitFrame(job.frame) && !m_logged) {
        Logger::err("VR: The backend rejected a stereo frame");
        m_logged = true;
      }
    } else {
      m_backend.submitEmptyFrame(job.displayTime);
    }

    m_step = uint32_t(Step::Unlock);
    m_deviceEx->GetDXVKDevice()->unlockSubmission();
  }


  void VrFrameSubmitter::runThread() {
    env::setThreadName("vr-submit");

    std::unique_lock<dxvk::mutex> lock(m_mutex);
    m_submitThread = GetCurrentThreadId();

    while (true) {
      m_cond.wait(lock, [this] { return m_pending || m_stop; });

      // A pending frame goes before stopping: the backend expects it
      if (!m_pending)
        return;

      lock.unlock();
      run(m_job);
      lock.lock();

      if (m_hangReported) {
        double seconds = std::chrono::duration<double>(high_resolution_clock::now() - m_handedAt).count();
        Logger::warn(str::format("VR: The frame reached the runtime after ", seconds, " s"));
        m_hangReported = false;
      }

      m_pending = false;
      m_cond.notify_all();
    }
  }


  void VrFrameSubmitter::runWatchdog() {
    env::setThreadName("vr-submit-watchdog");

    std::unique_lock<dxvk::mutex> lock(m_mutex);

    while (!m_stop) {
      m_cond.wait_for(lock, WatchPeriod, [this] { return m_stop; });

      if (!m_pending || m_hangReported)
        continue;

      auto late = high_resolution_clock::now() - m_handedAt;

      if (late < HangTime)
        continue;

      m_hangReported = true;
      lock.unlock();
      reportHang(std::chrono::duration<double>(late).count());
      lock.lock();
    }
  }


  void VrFrameSubmitter::reportHang(double seconds) {
    Logger::err(str::format("VR: A frame has been with the submission thread for ", seconds,
      " s, ", stepName(m_step.load())));
    Logger::err(str::format("VR: Submission thread ", m_submitThread, ": ",
      VrCrashLog::describeThread(m_submitThread)));
    Logger::err(str::format("VR: Game thread ", m_gameThread, ": ",
      VrCrashLog::describeThread(m_gameThread)));
  }


  void VrFrameSubmitter::startThread() {
    m_stop    = false;
    m_pending = false;
    m_thread  = dxvk::thread([this] { runThread(); });
    m_watchdog = dxvk::thread([this] { runWatchdog(); });
    Logger::info("VR: Frames and panels are handed to the runtime on a thread of their own");
  }


  void VrFrameSubmitter::stopThread() {
    if (!m_thread.joinable())
      return;

    { std::unique_lock<dxvk::mutex> lock(m_mutex);
      m_stop = true;
      m_cond.notify_all();
    }

    m_thread.join();
    m_watchdog.join();
    m_job = Job();
  }

}
