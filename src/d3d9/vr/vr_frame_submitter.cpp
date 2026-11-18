#include "../d3d9_device.h"

#include "../../util/log/log.h"
#include "../../util/util_env.h"
#include "../../util/util_time.h"

#include "vr_d3d9_bridge.h"
#include "vr_frame_submitter.h"

namespace dxvk {

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
    m_job     = std::move(job);
    m_pending = true;
    m_cond.notify_all();
    return hasEyes;
  }


  void VrFrameSubmitter::run(Job& job) {
    auto start = high_resolution_clock::now();

    // Rendering has to reach the queue before the backend reads the images.
    // Locking the submission queue waits for DXVK's submission thread to
    // drain, so everything recorded up to the flush is queued ahead of us.
    m_deviceEx->WaitForVrFlush(job.flushSeq);
    m_deviceEx->GetDXVKDevice()->lockSubmission();

    double syncMs = std::chrono::duration<double, std::milli>(high_resolution_clock::now() - start).count();

    for (const PanelOp& op : job.panels) {
      if (op.hide)
        m_backend.hidePanel(op.id);
      else
        m_backend.submitPanel(op.id, op.panel);
    }

    if (job.hasEyes) {
      job.frame.syncMs = syncMs;

      if (!m_backend.submitFrame(job.frame) && !m_logged) {
        Logger::err("VR: The backend rejected a stereo frame");
        m_logged = true;
      }
    } else {
      m_backend.submitEmptyFrame(job.displayTime);
    }

    m_deviceEx->GetDXVKDevice()->unlockSubmission();
  }


  void VrFrameSubmitter::runThread() {
    env::setThreadName("vr-submit");

    std::unique_lock<dxvk::mutex> lock(m_mutex);

    while (true) {
      m_cond.wait(lock, [this] { return m_pending || m_stop; });

      // A pending frame goes before stopping: the backend expects it
      if (!m_pending)
        return;

      lock.unlock();
      run(m_job);
      lock.lock();

      m_pending = false;
      m_cond.notify_all();
    }
  }


  void VrFrameSubmitter::startThread() {
    m_stop    = false;
    m_pending = false;
    m_thread  = dxvk::thread([this] { runThread(); });
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
    m_job = Job();
  }

}
