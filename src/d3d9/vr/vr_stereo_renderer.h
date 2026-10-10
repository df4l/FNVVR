#pragma once

#include <array>
#include <memory>

#include "../d3d9_include.h"

#include "../../util/com/com_pointer.h"

#include "vr_backend.h"
#include "vr_game_camera.h"
#include "vr_game_state.h"
#include "vr_hud_layers.h"
#include "vr_menu_scenes.h"
#include "vr_preview_window.h"

namespace dxvk {

  /**
   * \brief Where the panels are placed
   *
   * Set by \c d3d9.vrPanelDistance, \c d3d9.vrPanelWidth,
   * \c d3d9.vrHudDistance, \c d3d9.vrHudWidth, \c d3d9.vrHudHeight and
   * \c d3d9.vrHudMessagesOffset in dxvk.conf. The panel of the main menu and the loading screens covers
   * about 53 degrees horizontally. The HUD panel is wider, so that the
   * messages at its edges stay readable, and sits slightly below eye level.
   * Menus opened in game use the HUD distance and width.
   */
  struct VrPanelConfig {
    /// Distance of the menu panel from the head when it appears, in metres
    float distance    = 2.0f;
    /// Width of the menu panel, in metres
    float width       = 2.0f;
    /// Distance of the HUD panel in front of the head, in metres
    float hudDistance = 1.0f;
    /// Width of the HUD panel, in metres
    float hudWidth    = 1.5f;
    /// Height of the HUD panel's centre relative to the eyes, in metres
    float hudHeight   = -0.15f;
    /// How far the messages and objectives, drawn by the game in the top
    /// left corner, are moved towards the centre of the HUD panel, in metres
    float hudMessagesOffset = 0.25f;
  };

  /**
   * \brief Renders the game's frame once per eye
   *
   * Takes over the call to Main::Swap in the game's main loop, which draws
   * the world, the HUD and presents. Everything before it, the game logic,
   * still runs once per frame. For each eye the world camera is moved to the
   * eye's pose and field of view and the game draws a whole frame. The 3D
   * image is copied into the eye texture just before the game draws the HUD
   * over it, so the eye textures hold the scene without the interface.
   *
   * The game positions the camera again at several points while it draws,
   * so two more hooks keep the eye pose in place.
   *
   * In game, the HUD messages, objectives and subtitles are shown on a
   * panel that follows the head. The left eye's interface pass, whose image
   * is never shown, draws them alone into the panel's texture instead of the
   * backbuffer, with the other HUD groups hidden. Menus opened in game
   * (pause, dialogue, containers, ...) are drawn the same way, without the
   * HUD, and shown on the menu panel. Menus with a 3D scene of their own
   * (lockpicking, casino games) get it drawn first, on an opaque
   * background. The Pip-Boy is not shown.
   *
   * The main menu and the loading screens have no 3D scene. There, the game
   * draws a single frame, and the whole presented image, interface included,
   * is shown on the backend's panel. The panel is placed in front of the
   * head when it appears and then stays where it is.
   */
  class VrStereoRenderer {

  public:

    ~VrStereoRenderer();

    /**
     * \brief Hooks the game's frame
     *
     * \param [in] backend Backend that provides poses and receives frames
     * \param [in] device D3D9 device the game renders with
     * \param [in] showPreview Show both eyes in a window of their own
     * \param [in] panel Placement of the panel for menus and loading screens
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrStereoRenderer> install(
            IVRBackend&           backend,
            IDirect3DDevice9*     device,
            bool                  showPreview,
      const VrPanelConfig&        panel);

    /**
     * \brief Called when the game presents, before the image is shown
     *
     * The game presents after every frame. The left eye's present is
     * dropped, so the window shows the right eye and the game's frame
     * state machine still sees a completed frame. Frames drawn once, such
     * as the main menu's, are copied to the panel here, with the interface.
     * This also catches the frames the game draws outside of its main loop
     * while it loads.
     *
     * \param [in] swapchain Swap chain being presented
     * \returns \c true if the present must be dropped
     */
    bool onPresent(IDirect3DSwapChain9* swapchain);

  private:

    static constexpr uint32_t NoEye = ~0u;

    /**
     * \brief What the left eye's interface pass draws for the headset
     */
    enum class InterfaceLayer {
      None,
      /// HUD messages, objectives and subtitles, for the panel in front of the head
      Hud,
      /// Menus opened in game, for the menu panel
      Menu,
    };

    VrStereoRenderer(IVRBackend& backend, IDirect3DDevice9* device,
      bool showPreview, const VrPanelConfig& panel);

    IVRBackend&        m_backend;
    IDirect3DDevice9*  m_device;
    bool               m_showPreview;
    VrPanelConfig      m_panelConfig;

    Com<IDirect3DTexture9> m_eyeTextures[VrEyeCount];
    std::unique_ptr<VrPreviewWindow> m_preview;

    // The game composes its final 3D image into the backbuffer
    Com<IDirect3DSurface9> m_gameTarget;

    // The eye the game is drawing, or NoEye outside of the per-eye frames
    uint32_t            m_eye         = NoEye;
    const VrEyeView*    m_eyeView     = nullptr;
    bool                m_eyeCopied[VrEyeCount] = { };

    // The camera the game draws with, as the game placed it for this frame.
    // It is restored after the eyes.
    VrGameCamera        m_renderCamera;
    VrGameCameraState   m_gameCamera  = { };
    VrGameCameraPose    m_gameCameraPose;

    // Logged on every change, so tools that drive the game can follow it
    VrGameState         m_gameState;
    VrGameStateKind     m_lastGameState = VrGameStateKind::Unknown;

    // HUD panel in front of the head and menus opened in game, drawn by the
    // left eye's interface pass
    VrHudLayers            m_hudLayers;
    Com<IDirect3DTexture9> m_hudTexture;
    Com<IDirect3DTexture9> m_hudPanelTexture;
    Com<IDirect3DTexture9> m_menuTexture;
    InterfaceLayer m_layer = InterfaceLayer::None;
    bool   m_hudAvailable  = false;
    bool   m_layerRendered = false;
    bool   m_hudIsolated   = false;
    bool   m_hudShown      = false;
    bool   m_interfaceFailed = false;
    bool   m_loggedHudFailure = false;

    // The game's blurred, frozen menu background is turned off, so that the
    // world stays live and in stereo behind menus opened in game
    bool   m_menuBackgroundFound = false;
    bool   m_loggedMenuBackground = false;

    // That branch of the game also draws the 3D scenes of menus such as
    // lockpicking, so they are drawn into the menu panel instead
    VrMenuScenes            m_menuScenes;
    Com<IDirect3DSurface9>  m_menuDepth;
    bool   m_menuDepthFailed = false;

    // Latest tracked head pose, used to place the panel
    VrPose m_headPose;
    bool   m_hasHeadPose   = false;

    // The panel shows the whole frame. The staging texture is only used
    // when the backbuffer has an alpha channel, see copyPanelImage.
    Com<IDirect3DTexture9> m_panelTexture;
    Com<IDirect3DTexture9> m_panelStaging;
    VrPose m_panelPose;
    bool   m_panelShown    = false;
    bool   m_panelPlacedWithHead = false;
    bool   m_panelShowsMenu = false;
    bool   m_panelFailed   = false;
    bool   m_loggedPanelFailure = false;

    VrPose m_reference;
    bool   m_hasReference  = false;
    bool   m_texturesFailed = false;
    bool   m_loggedStart   = false;
    bool   m_loggedFailure = false;

    static void __fastcall swapHook(
            void*                 main,
            void*                 unused);

    static void __fastcall renderInterfaceHook(
            void*                 interfaceManager,
            void*                 unused,
            void*                 arg0,
            uint32_t              arg1);

    static void __cdecl interfaceCullHook(
            void*                 camera,
            void*                 sceneGraph,
            void*                 data);

    static void __fastcall accumTaskHook(
            void*                 manager,
            void*                 unused,
            uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
            uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8);

    static void __fastcall placeCameraHook(
            void*                 main,
            void*                 unused);

    static void __fastcall cameraUpdateHook(
            void*                 camera,
            void*                 unused,
            void*                 updateData);

    void renderFrame(void* main);

    void disableStaticMenuBackground();

    void renderPanelFrame(void* main);

    void trackHead(const VrInputState& input);

    VrGameStateKind readGameState();

    bool createEyeTextures();

    void renderEye(
            uint32_t              eye,
      const VrEyeView&            view,
            void*                 main);

    void applyEyePose();

    void captureEye();

    bool copyRenderTarget(uint32_t eye);

    InterfaceLayer chooseLayer(VrGameStateKind state) const;

    bool renderLayer(
            void*                 interfaceManager,
            void*                 arg0,
            uint32_t              arg1);

    void renderMenuScenes();

    bool createMenuDepth();

    void isolateHud();

    bool createInterfaceTextures();

    bool composeHud();

    void submitHud();

    void submitMenu();

    void hideHud();

    void updatePanel(IDirect3DSwapChain9* swapchain);

    void placePanel(float distance);

    void hidePanel();

    bool createPanelTextures(const D3DSURFACE_DESC& desc);

    bool copyPanelImage(IDirect3DSurface9* backBuffer);

  };

}
