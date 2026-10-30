#pragma once

#include <array>
#include <memory>

#include "../d3d9_include.h"

#include "../../util/com/com_pointer.h"
#include "../../util/util_time.h"

#include "vr_activate_target.h"
#include "vr_backend.h"
#include "vr_comfort_settings.h"
#include "vr_controls.h"
#include "vr_culling_camera.h"
#include "vr_developer_mode.h"
#include "vr_game_camera.h"
#include "vr_game_state.h"
#include "vr_game_timer.h"
#include "vr_hand_activate_pick.h"
#include "vr_hand_hud.h"
#include "vr_hud_layers.h"
#include "vr_hud_meters.h"
#include "vr_charge_ring.h"
#include "vr_console_terminal.h"
#include "vr_hand_throw.h"
#include "vr_holster.h"
#include "vr_ammo_label.h"
#include "vr_game_images.h"
#include "vr_quick_wheel.h"
#include "vr_melee_swing.h"
#include "vr_menu_scenes.h"
#include "vr_manual_reload.h"
#include "vr_part_highlight.h"
#include "vr_npc_gaze.h"
#include "vr_painter.h"
#include "vr_pipboy_animation.h"
#include "vr_weapon_drawn.h"
#include "vr_pipboy_control.h"
#include "vr_physics_grab.h"
#include "vr_pipboy_gaze.h"
#include "vr_pipboy_size.h"
#include "vr_power_attack_rules.h"
#include "vr_pipboy_render.h"
#include "vr_preview_window.h"
#include "vr_prompt_icon.h"
#include "vr_render_passes.h"
#include "vr_sky_anchor.h"
#include "vr_tile_texture_retry.h"
#include "vr_weapon_hand.h"

namespace dxvk {

  /**
   * \brief What a swap chain does with a present
   */
  enum class VrPresentMode {
    /// Present as the game asked
    Game,
    /// Present without waiting for the window's vertical blank. The VR
    /// runtime already paced the frame, and waiting for the desktop
    /// display would cap the game at the monitor's refresh rate.
    Immediate,
    /// Drop the present (first eye's frame)
    Drop,
  };

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
   * backbuffer, with the other HUD groups hidden. The right eye's pass draws
   * the health bar, the compass, the action points, the ammunition and the
   * interaction prompt alone in the same way; they are cut out of its image
   * and shown on the hands: the health bar and compass, with the action
   * points and ammunition below them, on the back of the right hand while
   * the player looks at it, the prompt above the fingertip nearest the
   * target. The game window
   * then shows no HUD. Menus opened in game
   * (pause, dialogue, containers, ...) are drawn the same way, without the
   * HUD, and shown on the menu panel. Meanwhile the right eye's pass, which
   * the headset does not show either, draws the head HUD groups the game
   * keeps showing (messages during a dialogue) without the menus, so the
   * game window shows no interface then. Menus with a 3D scene of their own
   * (lockpicking, casino games) get it drawn first, on an opaque
   * background. The Pip-Boy is drawn over the live world, see
   * VrPipBoyRender.
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
     * \param [in] turn How the turning stick turns the player
     * \param [in] weaponInHand Draw the weapon in the right hand and shoot along it
     * \param [in] timer How the game's frame timer is driven
     * \param [in] passes Which render passes both eyes share
     * \param [in] developer What the developer mode gives after each loading screen
     * \param [in] swing How fast a melee weapon is swung to hit
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrStereoRenderer> install(
            IVRBackend&           backend,
            IDirect3DDevice9*     device,
            bool                  showPreview,
      const VrPanelConfig&        panel,
      const VrTurnConfig&         turn,
            bool                  weaponInHand,
      const VrGameTimerConfig&    timer,
      const VrRenderPassConfig&   passes,
      const VrDeveloperConfig&    developer,
      const VrSwingConfig&        swing);

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
     * The game also plays movies, such as the intro of a new game, in a
     * loop of its own that presents every movie frame and polls the
     * gamepad to let the player skip the movie. The controllers are read
     * here for those frames, see \ref updateBlockedInput.
     *
     * Frames drawn by the game's main loop were paced by the runtime, so
     * they are presented without waiting for the desktop display.
     *
     * \param [in] swapchain Swap chain being presented
     * \returns What the swap chain does with the present
     */
    VrPresentMode onPresent(IDirect3DSwapChain9* swapchain);

    /**
     * \brief Changes how the turning stick turns
     *
     * Called from the game's options page, on the game's thread.
     */
    void setTurnConfig(const VrTurnConfig& turn) {
      m_controls.setTurnConfig(turn);
    }

    /**
     * \brief Changes how large the Pip-Boy grows while it is open
     *
     * Called from the game's options page, on the game's thread.
     *
     * \param [in] scale Size as a factor of the game's size, 1 to keep it
     */
    void setPipBoyScale(float scale) {
      m_pipBoyScale = scale;
    }

    /**
     * \brief Turns the reload with the hands on or off
     *
     * \param [in] enabled Off, every weapon is reloaded as in the game
     */
    void setManualReload(bool enabled) {
      if (m_manualReload)
        m_manualReload->setEnabled(enabled);
    }

    /**
     * \brief Turns the pushing spheres of the free hands on or off
     *
     * \param [in] enabled Off, the hands no longer push objects
     */
    void setHandPush(bool enabled) {
      if (m_physicsGrab)
        m_physicsGrab->setHandPush(enabled);
    }

    /**
     * \brief Changes how the player sneaks
     *
     * Called from the game's options page, on the game's thread.
     */
    void setSneakConfig(const VrSneakConfig& config) {
      m_controls.setSneakConfig(config);
    }

  private:

    static constexpr uint32_t NoEye = ~0u;

    /**
     * \brief What an eye's interface pass draws for the headset
     */
    enum class InterfaceLayer {
      None,
      /// HUD messages, objectives and subtitles, for the panel in front of the head
      Hud,
      /// Health bar, compass and interaction prompt, for the hand panels
      HandHud,
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
    // left eye's interface pass. While a menu is open, the right eye's pass
    // draws the HUD elements the game still shows.
    VrHudLayers            m_hudLayers;
    VrTileTextureRetry     m_tileTextureRetry;
    VrHudMeters            m_hudMeters;
    bool              m_hudMetersAvailable = false;
    VrHudMetersPlaced m_hudMetersPlaced;
    Com<IDirect3DTexture9> m_hudTexture;
    Com<IDirect3DTexture9> m_hudPanelTexture;
    Com<IDirect3DTexture9> m_menuTexture;
    InterfaceLayer m_layer = InterfaceLayer::None;

    // Health bar, compass, action points, ammunition and interaction prompt,
    // drawn by the right eye's interface pass in game and cut out into the
    // hand panels' textures
    VrActivateTarget       m_activateTarget;
    Com<IDirect3DTexture9> m_handHudTexture;
    Com<IDirect3DTexture9> m_wristTexture;
    Com<IDirect3DTexture9> m_promptTexture;
    VrWristGaze            m_wristGaze;
    VrPromptHand           m_promptHand;
    VrHand                 m_promptAt = VrHand::Right;
    VrPromptIcon           m_promptIcon;
    bool   m_handHudAvailable = false;
    bool   m_promptAvailable  = false;
    bool   m_handHudRendered  = false;
    bool   m_wristShown       = false;
    bool   m_promptShown      = false;
    bool   m_loggedHandHudCrops = false;
    bool   m_loggedWrist      = false;
    bool   m_loggedPrompt     = false;
    bool   m_loggedLeftPrompt = false;
    bool   m_loggedHandHudFailure = false;
    bool   m_hudAvailable  = false;
    bool   m_hudRendered   = false;
    bool   m_menuRendered  = false;
    bool   m_hudIsolated   = false;
    bool   m_hudShown      = false;

    // The Pip-Boy is not on the panel, but the menus the game draws over
    // it, such as message boxes, are: the panel is shown only while the
    // left eye's pass finds one
    bool   m_overPipBoy    = false;
    bool   m_menuDrawn     = false;
    bool   m_interfaceFailed = false;
    bool   m_loggedHudFailure = false;

    // The game's blurred, frozen menu background is turned off, so that the
    // world stays live and in stereo behind menus opened in game
    bool   m_menuBackgroundFound = false;
    bool   m_loggedMenuBackground = false;

    // That branch of the game also draws the 3D scenes of menus such as
    // lockpicking, so they are drawn into the menu panel instead
    VrMenuScenes            m_menuScenes;
    Com<IDirect3DSurface9>     m_menuDepth;
    Com<IDirect3DStateBlock9>  m_menuSceneState;
    bool   m_menuDepthFailed = false;
    bool   m_loggedMenuDepthChange = false;

    // Controller input for the game, and the head driving the player's look
    VrControls          m_controls;

    // The first-person weapon, drawn in the hand when it is installed
    std::unique_ptr<VrWeaponHand> m_weaponHand;

    // Hits dealt by swinging the melee weapon in the hand
    std::unique_ptr<VrMeleeSwing> m_meleeSwing;

    // Grenades, mines and thrown weapons armed and thrown by hand
    std::unique_ptr<VrHandThrow>  m_handThrow;

    // Draws the images of the panels below, which it outlives
    std::unique_ptr<VrPainter>    m_painter;

    // Power attacks charged by holding the trigger, and their ring beside
    // the hand
    std::unique_ptr<VrPowerAttackRules> m_powerRules;
    std::unique_ptr<VrChargeRing>       m_chargeRing;
    VrGameImages                        m_gameImages;
    std::unique_ptr<VrQuickWheel>       m_quickWheel;
    std::unique_ptr<VrAmmoLabel>        m_ammoLabel;
    // The left hand held ammunition at the last frame, and the reload
    // button was down
    bool                                m_ammoInHand     = false;
    bool                                m_ammoButtonDown = false;
    VrPowerCharge       m_powerCharge;
    VrPowerRefusal      m_powerRefusal;
    uint32_t            m_loggedPowerHits = 0;

    // The game's console, typed in on a terminal with the lasers
    std::unique_ptr<VrConsoleTerminal> m_terminal;

    // The activation target, picked along the hands instead of the view
    std::unique_ptr<VrHandActivatePick> m_handPick;
    VrVector3           m_eyePosition;

    // The sky, centred on the eye being drawn when it is installed
    std::unique_ptr<VrSkyAnchor> m_skyAnchor;

    // The game's visibility pass, which runs before the frame is drawn,
    // culls for the latest head pose and both eyes' field of view
    std::unique_ptr<VrCullingCamera> m_cullingCamera;
    VrFov               m_cullingFov;
    bool                m_hasCullingFov = false;

    // The Pip-Boy over the live world. Its screen pass moves the camera into
    // the first-person drawing space, offset from the world by this shift.
    std::unique_ptr<VrPipBoyRender> m_pipBoyRender;

    // The Pip-Boy opens and closes without the arm animations
    std::unique_ptr<VrPipBoyAnimation> m_pipBoyAnimation;

    // The player's weapon stays drawn, without the draw and holster animations
    std::unique_ptr<VrWeaponDrawn> m_weaponDrawn;
    VrHolsterHand       m_holster;

    // The idle camera off and the kill camera in first person
    std::unique_ptr<VrComfortSettings> m_comfortSettings;

    // The other actors look at the head
    std::unique_ptr<VrNpcGaze> m_npcGaze;

    // Reloading with the hands
    std::unique_ptr<VrManualReload> m_manualReload;

    // World objects held with the hands
    std::unique_ptr<VrPhysicsGrab> m_physicsGrab;

    // Keeps each drawn hand on the object it holds
    std::array<VrGrabHandGlue, VrHandCount> m_grabGlue;

    // Closes each drawn hand's fingers on the object it holds
    std::array<VrGrabFingerBlend, VrHandCount> m_grabFingers;

    // Makes the parts of the weapon the reload needs worked glow
    std::unique_ptr<VrPartHighlight> m_partHighlight;

    // God mode, weapons and ammunition, for testing (d3d9.vrDeveloperMode)
    std::unique_ptr<VrDeveloperMode> m_developerMode;

    // The game's frame timer, measured against real time
    std::unique_ptr<VrGameTimer> m_gameTimer;
    std::unique_ptr<VrRenderPasses> m_renderPasses;

    VrVector3           m_screenShift;
    bool                m_screenPass = false;

    // The Pip-Boy opens when the player looks at the left forearm, and
    // closes through the menu's back button on the next frame when the
    // player looks away. A menu drawn over it on the previous frame keeps
    // it open.
    VrPipBoyControl     m_pipBoyControl;
    VrPipBoyGaze        m_pipBoyGaze;
    bool                m_pipBoyGazeAvailable = false;
    bool                m_closePipBoy = false;
    // The game's Pip-Boy control is blocked until the back button that
    // closed the Pip-Boy has been released and seen by the game
    uint32_t            m_pipBoyBackFrames = 0;
    bool                m_menuOverPipBoy = false;
    bool                m_loggedPipBoyGaze = false;
    bool                m_loggedPipBoyGazeAway = false;
    bool                m_rightHandAtPipBoy = false;
    // Size the Pip-Boy grows to while it is open, for players who cannot
    // read it at the game's size
    VrPipBoyGrowth      m_pipBoyGrowth;
    float               m_pipBoyScale = 1.0f;
    bool                m_loggedPipBoyLight = false;

    // Thread that runs the game's main loop, and whether Main::Swap ran
    // since the previous present. A present on that thread without a swap
    // comes from a loop of the game that blocks the main loop.
    DWORD  m_mainThread    = 0;
    bool   m_swapped       = false;
    // Whether the runtime paced the current frame of the main loop
    bool   m_paced         = false;
    bool   m_loggedImmediatePresent = false;
    high_resolution_clock::time_point m_blockedInputTime;
    bool   m_blockedInput  = false;
    bool   m_loggedBlockedInput = false;

    // Latest tracked head pose, used to place the panel
    VrPose m_headPose;
    bool   m_hasHeadPose   = false;

    // The panel shows the whole frame. The staging texture is only used
    // when the backbuffer has an alpha channel, see copyPanelImage.
    Com<IDirect3DTexture9> m_panelTexture;
    Com<IDirect3DTexture9> m_panelStaging;
    VrPose m_panelPose;
    float  m_panelWidth    = 0.0f;

    // The actor a dialogue focused on this frame, and the one the menu
    // panel was placed for: a dialogue's panel is placed between the head
    // and the speaker's head
    void*  m_dialogueSpeaker = nullptr;
    void*  m_panelSpeaker    = nullptr;
    bool   m_loggedSpeakerPanel = false;
    VrPanelPlacement m_menuPanel;
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

    // Geometry logs, to compare headsets whose image looks distorted. The
    // game's own frustum changes during dialogue zooms, so only the first
    // changes are logged
    constexpr static uint32_t MaxGameFrustumLogs = 8;
    float    m_loggedGameFrustum[4] = { };
    uint32_t m_gameFrustumLogs      = 0;
    uint32_t m_loggedTargetWidth    = 0;
    uint32_t m_loggedTargetHeight   = 0;
    bool     m_loggedEyeFrustum     = false;
    bool     m_warnedEyeFrustum     = false;

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

    void renderPanelFrame(void* main, VrGameStateKind state);

    void trackHead(const VrInputState& input);

    void updateBlockedInput();

    VrGameStateKind readGameState();

    bool createEyeTextures();

    void renderEye(
            uint32_t              eye,
      const VrEyeView&            view,
            void*                 main);

    void applyEyePose();

    void setScreenPass(const VrVector3* shift);

    void updateWeaponHand(const VrInputState& input, VrGameStateKind state,
      const VrGameCameraPose& gameCamera, float dt);

    void updateHandPick(const VrInputState& input, VrGameStateKind state);

    void updateNpcGaze();

    void updateMeleeSwing(const VrInputState& input, VrGameStateKind state, float dt);

    void updateHandThrow(const VrInputState& input, VrGameStateKind state, float dt);

    void updateManualReload(const VrInputState& input, VrGameStateKind state, float dt);

    void updateHolster(const VrInputState& input, VrGameStateKind state);

    void updatePhysicsGrab(const VrInputState& input, VrGameStateKind state);

    bool leftGripOnWeapon(const VrInputState& input) const;

    void updateQuickWheel(const VrInputState& input, VrGameStateKind state);

    void updateAmmoChoice(const VrInputState& input, VrGameStateKind state);

    void hideQuickPanels();

    bool updatePowerCharge(const VrInputState& input, int32_t& cost);

    void updateControls(const VrInputState& input, VrGameStateKind state, bool stereo, float dt);

    VrGameCameraPose handInGame(const VrControllerState& controller) const;

    bool cullingView(VrGameCameraPose& pose, VrGameFrustum& frustum);

    bool sharedPassView(VrGameCameraPose& pose, VrGameFrustum& frustum);

    void updatePipBoyGaze(
      const VrInputState&         input,
            VrGameStateKind       state,
            float                 dt);

    bool isRightHandAtPipBoy(
      const VrInputState&         input,
            VrGameStateKind       state);

    /**
     * \brief Logs the frustum the game set for its camera when it changes
     */
    void logGameFrustum();

    /**
     * \brief Logs the viewport and sizes of the eye copy, and warns when the
     *    frustum the eye is drawn with is not the one written for it
     */
    void logEyeCapture();

    void captureEye();

    bool copyRenderTarget(uint32_t eye);

    InterfaceLayer chooseLayer(VrGameStateKind state) const;

    InterfaceLayer eyeLayer() const;

    bool renderLayer(
            void*                 interfaceManager,
            void*                 arg0,
            uint32_t              arg1);

    void renderMenuScenes();

    void makeMenuSceneOpaque(bool depthTested);

    bool createMenuSceneResources();

    void isolateHud();

    bool createInterfaceTextures();

    bool composeHud();

    void submitHud();

    void submitMenu();

    void hideHud();

    void submitHandHud(const VrInputState& input, float dt);

    /**
     * \brief Part of a hand panel's image, cut out of the hand HUD's image
     *
     * Both rectangles are in pixels; the target is in the panel's image.
     */
    struct HandPanelPart {
      RECT source;
      RECT target;
    };

    bool showHandPanel(
            VrPanelId             id,
      const HandPanelPart*        parts,
            size_t                count,
            float                 metresPerPixel,
      const VrPose&               pose,
            Com<IDirect3DTexture9>& texture);

    void hideHandPanel(VrPanelId id, bool& shown);

    void hideHandHud();

    void updatePanel(IDirect3DSwapChain9* swapchain);

    void placePanel(float distance);

    void placeMenuPanel();

    /**
     * \brief Where the menu panel is, \c nullptr while it is hidden
     */
    const VrPanelPlacement* menuPanel();

    void hidePanel();

    bool createPanelTextures(const D3DSURFACE_DESC& desc);

    bool copyPanelImage(IDirect3DSurface9* backBuffer);

  };

}
