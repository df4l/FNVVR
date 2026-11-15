#pragma once

#include <array>
#include <memory>

#include "../d3d9_include.h"

#include "../../dxvk/dxvk_buffer.h"
#include "../../dxvk/dxvk_gpu_query.h"
#include "../../dxvk/dxvk_image.h"

#include "vr_foveation_map.h"

namespace dxvk {

  class D3D9DeviceEx;

  /**
   * \brief Eye-tracked foveated rendering
   *
   * Shades the world at full rate around where each eye looks, and once
   * per 2x2 or 4x4 pixels further away (VrFoveationZones), with a shading
   * rate attachment (VK_KHR_fragment_shading_rate). Edges keep the full
   * resolution: only the shading of a surface is shared by the pixels of
   * a fragment, the coverage of each pixel and each sample is not.
   *
   * Each eye has a shading rate image the size of the world's render
   * target divided by the attachment's texel size. Its content is a window
   * of the eye's VrFoveationMap, copied by the GPU from a buffer that holds
   * the whole map, at the gaze of the frame. The image is attached to the
   * render passes of the world (scene and first-person model) and nothing
   * else: shadow maps, water reflections and the scope have other sizes,
   * and image space effects and the interface are drawn after endWorld.
   *
   * See findings/performance.md, eye-tracked foveation.
   *
   * Only used on the game's main thread.
   */
  class VrFoveation {

  public:

    /**
     * \brief Creates the foveation of a device
     *
     * \param [in] device The game's D3D9 device, created by DXVK
     * \param [in] zones Angles of the zones around the gaze
     * \returns \c nullptr if the GPU has no shading rate attachments
     */
    static std::unique_ptr<VrFoveation> create(
            IDirect3DDevice9*   device,
      const VrFoveationZones&   zones);

    ~VrFoveation();

    /**
     * \brief Sets the eyes' shading rates for a frame
     *
     * Records the copies of the frame's rates. An eye without a gaze, or
     * whose gaze is outside its view, is shaded at full rate.
     *
     * \param [in] views The frame's views, with the fields of view the eyes
     *    are drawn with
     * \param [in] gaze Where the eyes look, or \c nullptr if not known
     * \param [in] target Size of the world's render target, in pixels
     */
    void prepareFrame(
      const std::array<VrEyeView, VrEyeCount>&  views,
      const std::array<VrGaze, VrEyeCount>*     gaze,
            VrExtent                            target);

    /**
     * \brief Starts the world's passes of an eye
     *
     * Its render passes take the eye's rates until endWorld.
     * \param [in] eye 0 for the left eye, 1 for the right eye
     */
    void beginWorld(uint32_t eye);

    /**
     * \brief Ends the world's passes: every pixel is shaded again
     */
    void endWorld();

    /**
     * \brief Shades every pixel until the next prepareFrame
     */
    void stop();

  private:

    struct Eye {
      VrFov                           fov;
      std::unique_ptr<VrFoveationMap> map;
      Rc<DxvkBuffer>                  rates;
      Rc<DxvkImage>                   image;
      Rc<DxvkImageView>               view;
      /// The image read back by the GPU at the previous log window
      Rc<DxvkBuffer>                  readback;
      bool                            readbackWritten = false;
      /// Map index of the window the image holds, none at first
      size_t                          window   = ~size_t(0u);
      bool                            foveated = false;
    };

    /// Left eye's world measured on the current frame
    enum class Sample : uint32_t {
      None,
      Rates,
      Full,
    };

    // Frames counted in each logged window
    static constexpr uint32_t WindowFrames = 900;

    VrFoveation(D3D9DeviceEx* device, uint32_t texelSize, const VrFoveationZones& zones);

    D3D9DeviceEx*            m_device;
    uint32_t                 m_texelSize;
    VrFoveationZones         m_zones;
    VrExtent                 m_target = { };
    std::array<Eye, VrEyeCount> m_eyes;
    bool                     m_attached = false;

    uint32_t                 m_frames         = 0;
    uint32_t                 m_foveatedFrames = 0;

    /// Fragment shader invocations of the left eye's world, on the first
    /// frame of a log window with the eye's rates and on the second at
    /// full rate. Empty if the GPU has no pipeline statistics.
    std::array<Rc<DxvkQuery>, 2> m_invocations;
    Sample                   m_sample       = Sample::None;
    bool                     m_queryActive  = false;
    bool                     m_ratesSampled = false;
    bool                     m_fullSampled  = false;

    void pickSample();

    void createEye(Eye& eye, const VrFov& fov, VrExtent target);

    void logWindow();

    void logImageContent(Eye& eye, uint32_t index);

    void logInvocations();

    static uint32_t pickTexelSize(const DxvkDevice& device);

  };

}
