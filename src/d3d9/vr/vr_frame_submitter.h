#pragma once

#include <array>
#include <vector>

#include "../d3d9_include.h"
#include "../d3d9_interfaces.h"

#include "../../util/com/com_pointer.h"
#include "../../util/thread.h"

#include "vr_backend.h"

namespace dxvk {

  class D3D9DeviceEx;

  /**
   * \brief Hands each stereo frame and its panels to the backend
   *
   * Between beginFrame and submitFrame, panel submissions and hides are
   * collected. submitFrame flushes the device once and hands them, with the
   * eyes, to a thread of its own: it waits for DXVK's CS thread to record
   * the frame, locks the queue and calls the backend, while the game thread
   * goes on with the game's update. On a slow CPU (the Steam Frame runs the
   * game in an x86 emulator) this wait and the runtime's calls took 2-3 ms of
   * the game thread per frame (findings/performance.md §13).
   *
   * finish waits for that thread. Every other method calls it first outside
   * a frame, and the renderer calls it before waiting for the next frame, so
   * the textures of a frame are not drawn to again while the backend still
   * reads them, and the backend's calls stay in the order they were made.
   *
   * Without the thread, the same work runs on the game thread in
   * submitFrame, with one flush for the panels and eyes together.
   */
  class VrFrameSubmitter {

  public:

    VrFrameSubmitter(IDirect3DDevice9* device, IVRBackend& backend);

    ~VrFrameSubmitter();

    VrFrameSubmitter             (const VrFrameSubmitter&) = delete;
    VrFrameSubmitter& operator = (const VrFrameSubmitter&) = delete;

    /**
     * \brief Chooses whether frames are handed off on a thread
     *
     * Takes effect from the next frame.
     */
    void setThreaded(bool threaded);

    /**
     * \brief Waits until the last frame has reached the backend
     */
    void finish();

    /**
     * \brief Collects the panels until submitFrame
     */
    void beginFrame();

    /**
     * \brief Shows a panel, or updates it if it is shown
     *
     * Within a frame, the panel goes to the backend with the eyes.
     * Otherwise, it goes at once, see VrD3D9Bridge::submitPanel.
     *
     * \returns \c false if the texture has no image the backend can
     *    read, or if the backend rejected the panel at once
     */
    bool submitPanel(
            VrPanelId             id,
            IDirect3DTexture9*    texture,
      const VrPose&               pose,
            float                 width,
            VrPanelAnchor         anchor,
            bool                  premultiplied = false);

    /**
     * \brief Hides a panel, in order with the panels of the frame
     */
    void hidePanel(VrPanelId id);

    /**
     * \brief Hands the frame's panels and eyes to the backend
     *
     * Ends the frame begun by beginFrame. If an eye has no image the
     * backend can read, the panels still go and the frame is closed
     * with IVRBackend::submitEmptyFrame.
     *
     * \returns \c false if the eye images could not be read
     */
    bool submitFrame(
            IDirect3DTexture9*    eyes[VrEyeCount],
      const std::array<VrEyeView, VrEyeCount>& views,
            int64_t               displayTime);

  private:

    struct PanelOp {
      VrPanelId               id      = VrPanelId::Menu;
      bool                    hide    = false;
      Com<ID3D9VkInteropTexture> texture;
      VrPanelSubmission       panel;
    };

    struct Job {
      uint64_t                flushSeq    = 0;
      std::vector<PanelOp>    panels;
      bool                    hasEyes     = false;
      Com<ID3D9VkInteropTexture> eyes[VrEyeCount];
      VrFrameSubmission       frame;
      int64_t                 displayTime = 0;
    };

    IDirect3DDevice9*         m_device;
    D3D9DeviceEx*             m_deviceEx;
    IVRBackend&               m_backend;

    bool                      m_threaded   = true;
    bool                      m_collecting = false;
    Job                       m_next;

    dxvk::thread              m_thread;
    dxvk::mutex               m_mutex;
    dxvk::condition_variable  m_cond;
    Job                       m_job;
    bool                      m_pending = false;
    bool                      m_stop    = false;
    bool                      m_logged  = false;

    void run(Job& job);

    void runThread();

    void startThread();

    void stopThread();

  };

}
