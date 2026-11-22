#include "../d3d9_device.h"
#include "../d3d9_texture.h"

#include "../../util/log/log.h"
#include "../../util/util_env.h"
#include "../../util/util_time.h"

#include "vr_crash_log.h"
#include "vr_d3d9_bridge.h"
#include "vr_frame_submitter.h"

namespace dxvk {

  namespace {

    /// A frame this long with the backend is reported as a hang
    constexpr int64_t HangTimeUs    = 3000000;
    constexpr DWORD   WatchPeriodMs = 1000;

    /// Threaded jobs whose steps are logged, to see where a first frame stops
    constexpr uint32_t TracedJobCount = 3;

    int64_t nowUs() {
      return std::chrono::duration_cast<std::chrono::microseconds>(
        high_resolution_clock::now().time_since_epoch()).count();
    }

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

    if (!threaded && m_thread.joinable()) {
      stopThread();
      Logger::info("VR: Frames and panels are handed to the runtime from the game thread");
    }

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

    pinImage(texture);

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

    for (uint32_t i = 0; i < VrEyeCount; i++) {
      job.hasEyes &= VrD3D9Bridge::queryImage(eyes[i], job.eyes[i], job.frame.images[i]);

      if (job.hasEyes)
        pinImage(eyes[i]);
    }

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
    m_handedAtUs = nowUs();
    m_gameThread = GetCurrentThreadId();
    m_busy       = true;
    m_cond.notify_all();
    return hasEyes;
  }


  void VrFrameSubmitter::run(Job& job) {
    auto start = high_resolution_clock::now();

    // Rendering has to reach the queue before the backend reads the images.
    // Locking the submission queue waits for DXVK's submission thread to
    // drain, so everything recorded up to the flush is queued ahead of us.
    setStep(Step::Flush);
    m_deviceEx->WaitForVrFlush(job.flushSeq);
    setStep(Step::Queue);
    m_deviceEx->GetDXVKDevice()->lockSubmission();

    // The images' handles are read again now that the CS thread has pinned
    // them: the ones read on the game thread may have been relocated since
    for (PanelOp& op : job.panels) {
      if (!op.hide)
        refreshImage(op.texture.ptr(), op.panel.image);
    }

    if (job.hasEyes) {
      for (uint32_t i = 0; i < VrEyeCount; i++)
        refreshImage(job.eyes[i].ptr(), job.frame.images[i]);
    }

    double syncMs = std::chrono::duration<double, std::milli>(high_resolution_clock::now() - start).count();

    setStep(Step::Panels);

    for (const PanelOp& op : job.panels) {
      if (op.hide)
        m_backend.hidePanel(op.id);
      else
        m_backend.submitPanel(op.id, op.panel);
    }

    setStep(Step::Eyes);

    if (job.hasEyes) {
      job.frame.syncMs = syncMs;

      if (!m_backend.submitFrame(job.frame) && !m_logged) {
        Logger::err("VR: The backend rejected a stereo frame");
        m_logged = true;
      }
    } else {
      m_backend.submitEmptyFrame(job.displayTime);
    }

    setStep(Step::Unlock);
    m_deviceEx->GetDXVKDevice()->unlockSubmission();
  }


  void VrFrameSubmitter::pinImage(IDirect3DTexture9* texture) {
    // DXVK's defragmentation may move an image to new memory at any flush
    // and destroy the old image once the GPU is done with it. The runtime
    // copies from the handle it is given, outside DXVK's tracking, so the
    // images it reads are kept where they are.
    m_deviceEx->EmitVrCommand([
      cImage = GetCommonTexture(texture)->GetImage()
    ] (DxvkContext* ctx) {
      DxvkImageUsageInfo usage = { };
      usage.stableGpuAddress = VK_TRUE;
      ctx->ensureImageCompatibility(cImage, usage);
    });
  }


  void VrFrameSubmitter::refreshImage(ID3D9VkInteropTexture* interop, VrEyeImage& image) {
    VkImage handle = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (FAILED(interop->GetVulkanImageInfo(&handle, &layout, nullptr)) || !handle)
      return;

    if (handle != image.image && !m_loggedMove) {
      Logger::warn("VR: An image moved in memory between the hand-off and its submission, its new handle is used");
      m_loggedMove = true;
    }

    image.image  = handle;
    image.layout = layout;
  }


  void VrFrameSubmitter::runThread() {
    env::setThreadName("vr-submit");

    std::unique_lock<dxvk::mutex> lock(m_mutex);
    m_submitThread = GetCurrentThreadId();
    VrCrashLog::watchThread(m_submitThread);

    while (true) {
      m_cond.wait(lock, [this] { return m_pending || m_stop; });

      // A pending frame goes before stopping: the backend expects it
      if (!m_pending)
        return;

      lock.unlock();
      run(m_job);
      lock.lock();

      if (m_hangReported.exchange(false)) {
        double seconds = double(nowUs() - m_handedAtUs) / 1.0e6;
        Logger::warn(str::format("VR: The frame reached the runtime after ", seconds, " s"));
      }

      if (m_tracedJobs < TracedJobCount && ++m_tracedJobs == TracedJobCount)
        Logger::info("VR: The submission thread's first frames went through");

      m_busy    = false;
      m_pending = false;
      m_cond.notify_all();
    }
  }


  void VrFrameSubmitter::runWatchdog() {
    env::setThreadName("vr-submit-watchdog");
    Logger::info("VR: The submission watchdog is running");

    // No lock here: a hang may sit on anything the other threads hold
    while (!m_watchStop) {
      Sleep(WatchPeriodMs);

      if (!m_busy || m_hangReported)
        continue;

      int64_t lateUs = nowUs() - m_handedAtUs;

      if (lateUs < HangTimeUs)
        continue;

      m_hangReported = true;
      reportHang(double(lateUs) / 1.0e6);
    }
  }


  void VrFrameSubmitter::setStep(Step step) {
    m_step = uint32_t(step);

    if (m_tracedJobs < TracedJobCount && m_threaded) {
      Logger::info(str::format("VR: Submission thread ", GetCurrentThreadId(), ", frame ", m_tracedJobs,
        ", ", double(nowUs() - m_handedAtUs) / 1000.0, " ms after the hand-off: ", stepName(uint32_t(step))));
    }
  }


  void VrFrameSubmitter::reportHang(double seconds) {
    Logger::err(str::format("VR: A frame has been with the submission thread for ", seconds,
      " s, ", stepName(m_step.load())));
    Logger::err(str::format("VR: Submission thread ", m_submitThread.load(), ": ",
      VrCrashLog::describeThread(m_submitThread.load())));
    Logger::err(str::format("VR: Game thread ", m_gameThread.load(), ": ",
      VrCrashLog::describeThread(m_gameThread.load())));
  }


  void VrFrameSubmitter::startThread() {
    m_stop      = false;
    m_pending   = false;
    m_busy      = false;
    m_watchStop = false;
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
    m_watchStop = true;
    m_watchdog.join();
    m_job = Job();
  }

}
