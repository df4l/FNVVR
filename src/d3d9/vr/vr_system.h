#pragma once

#include <memory>

#include "../d3d9_include.h"

#include "vr_backend.h"
#include "vr_game_resolution.h"
#include "vr_stereo_renderer.h"

namespace dxvk {

  class VrExtensionProvider;

  /**
   * \brief Process-wide owner of the active VR backend
   *
   * The backend has to exist before the Vulkan instance is created,
   * because it decides which Vulkan extensions are enabled. For that reason
   * this object is created once per process, before the first D3D9
   * interface, and lives until the process ends.
   *
   * The backend is chosen by the \c d3d9.vrBackend option in dxvk.conf, which
   * the \c DXVK_VR_BACKEND environment variable overrides. Valid values are
   * "off" (default), "emulator" and "openxr".
   */
  class VrSystem {

  public:

    ~VrSystem();

    /**
     * \brief Creates the VR system if VR is enabled
     *
     * Registers the Vulkan extension provider. Must be called before
     * the first Vulkan instance is created. Calling it again has no effect.
     */
    static void initialize();

    /**
     * \brief Called after a D3D9 interface was created
     *
     * The VR runtime is running from then on, so the headset's resolution
     * is known. The first call makes the game render at a resolution chosen
     * for the headset, unless \c d3d9.vrHeadsetResolution is off. Safe to
     * call when VR is disabled.
     */
    static void onInterfaceCreated();

    /**
     * \brief Returns the VR system, or \c nullptr when VR is disabled
     */
    static VrSystem* get();

    /**
     * \brief Called by a swap chain before it presents
     *
     * Hands the frame to the VR layer, see VrStereoRenderer::onPresent.
     * Safe to call when VR is disabled.
     *
     * \param [in] swapchain Swap chain being presented
     * \returns \c true if the present must be dropped, which is the case
     *    for the first eye's frame
     */
    static bool onPresent(IDirect3DSwapChain9* swapchain);

    IVRBackend& backend() { return *m_backend; }

    /**
     * \brief Starts the VR session on the device's Vulkan queue
     *
     * Also hooks the game's rendering so that it draws both eyes,
     * if the executable is the supported version.
     *
     * \param [in] device D3D9 device created by DXVK
     * \returns \c true if the session is running
     */
    bool attachDevice(IDirect3DDevice9* device);

    /**
     * \brief Ends the session
     */
    void detachDevice();

    bool hasSession() const { return m_hasSession; }

  private:

    VrSystem(std::unique_ptr<IVRBackend> backend, bool showPreview,
      const VrPanelConfig& panel, bool headsetResolution);

    std::unique_ptr<IVRBackend>          m_backend;
    std::unique_ptr<VrExtensionProvider> m_extensionProvider;
    std::unique_ptr<VrStereoRenderer>    m_stereoRenderer;
    bool                                 m_showPreview;
    VrPanelConfig                        m_panelConfig;
    VrGameResolution                     m_resolution;
    bool                                 m_headsetResolution;
    bool                                 m_resolutionChosen = false;
    bool                                 m_hasSession = false;

    void chooseResolution();

  };

}
