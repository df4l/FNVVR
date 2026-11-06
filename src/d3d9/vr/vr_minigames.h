#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "../d3d9_include.h"

#include "vr_backend.h"
#include "vr_controller_pad.h"
#include "vr_game_camera.h"
#include "vr_minigame_space.h"

namespace dxvk {

  /**
   * \brief Sizes the panel of a mini-game is placed with
   */
  struct VrMinigameFrame {
    /// Head pose in tracking space
    VrPose head;
    /// Whether the frame is drawn per eye and the game is in a menu
    bool   stereo      = false;
    /// Width and distance of the menu panel when no mini-game places it
    float  panelWidth  = 1.5f;
    float  panelDistance = 1.0f;
    /// Height of the panel's image over its width
    float  panelAspect = 0.75f;
    float  dt          = 0.0f;
    /// The recenter reference, if there is one
    bool   hasReference = false;
    VrPose reference;
    /// Height of the game's camera in the world, known when \c stereo is set
    float  cameraHeight = 0.0f;
    /// The camera the eyes were placed from in the last frame
    VrGameCameraPose camera;
  };

  /**
   * \brief Shows the game's mini-games in the room
   *
   * The lockpicking, slot machine, blackjack, roulette, Caravan and vigor
   * tester menus, the world's terminals (hacking included) and the Reflectron
   * of the character creation (with the barber's and the surgeon's) keep all
   * of their game logic. Their 3D scene, which the game draws with a camera of its own,
   * is drawn in each eye's image with that camera on the eye, at the place
   * of the room the game's view gives it, instead of on the flat panel. The
   * interface's tiles stay on the panel, which is out of the way of the
   * scene. The controllers act on the scene: see findings/minigames.md.
   *
   * A casino table or a slot machine of the world takes the scene on itself:
   * the scene is drawn where the object is, tested against the world, what
   * it replaces of the object is hidden, and the player is brought to the
   * object's seat while the menu is open, by the view only. The player's own
   * body and the characters frozen in the way of the scene are hidden too. The
   * slot machine's payout screen is shown only while it is lowered. The
   * vigor tester's scene holds the world's machine itself: it is shown only on
   * it, else the menu stays on the panel. The lasers click its buttons on the
   * rectangle its camera sees, as on a terminal, with that camera on the laser
   * so that the game picks along it. A world's terminal shows the menu's
   * screen on its own glass, seen from where the player stands.
   *
   * Everything reaches the menus as the gamepad or the cursor would, and
   * the game's camera is put back after each draw, because the game's
   * picking in a terminal reads it.
   *
   * Only used on the game's main thread.
   */
  class VrMinigames {

  public:

    /**
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrMinigames> create(IDirect3DDevice9* device, IVRBackend& backend);

    ~VrMinigames();

    /**
     * \brief Follows the game's menus and reads the hands
     *
     * Called once per frame, before the controls are given to the game.
     *
     * \param [in] frame The frame's sizes and head pose
     * \param [in] input The controllers
     * \param [in] surface Where the lasers pointed at surface() in the last frame
     */
    void update(
      const VrMinigameFrame&        frame,
      const VrInputState&           input,
      const VrPointerSurfaceState&  surface);

    /// Id of the menu whose scene the eyes show, 0 if none
    uint32_t menuId() const;

    /// Whether the eyes show a rendered menu: a world's terminal or the Reflectron
    bool showsRenderedMenu() const { return m_kind == Kind::Terminal || m_kind == Kind::Reflectron; }

    bool active() const { return m_kind != Kind::None; }

    /// The gamepad input of the hands' gestures, for the next frame
    const VrMinigamePad& pad() const { return m_pad; }

    /// The surface the lasers can point at, \c nullptr if none
    const VrPanelPlacement* surface() const { return m_hasSurface ? &m_surface : nullptr; }

    /// Where the panel goes, \c nullptr to place it as usual
    const VrPanelPlacement* panel() const { return active() ? &m_panel : nullptr; }

    /// Changes whenever the panel has to be placed again
    uint32_t panelRevision() const { return m_panelRevision; }

    /// The camera the eyes are placed from, \c nullptr for the game's own
    const VrGameCameraPose* seatCamera() const { return m_seated ? &m_viewCamera : nullptr; }

    /**
     * \brief Gives the camera the eyes are placed from in this frame
     *
     * A scene on an object seen from where the player stands follows it.
     * \param [in] camera The camera, after the head and the body moved it
     */
    void viewFrom(const VrGameCameraPose& camera);

    /**
     * \brief Draws the scene for an eye
     *
     * \param [in] eye The eye
     * \param [in] nearPlane Near plane of the world's camera, in its units
     * \param [in] farPlane Far plane of the world's camera
     */
    void drawEye(const VrEyeView& eye, float nearPlane, float farPlane);

    /// Whether the scene can be drawn before the first-person pass
    bool drawsBeforeFirstPerson() const { return active() && !showsRenderedMenu(); }

  private:

    enum class Kind {
      None,
      Lock,
      SlotMachine,
      BlackJack,
      Roulette,
      Caravan,
      VigorTester,
      Terminal,
      Reflectron,
    };

    VrMinigames(IDirect3DDevice9* device, IVRBackend& backend);

    IDirect3DDevice9* m_device;
    IVRBackend&       m_backend;

    Kind              m_kind = Kind::None;
    // A menu whose scene could not be placed: it stays on the panel
    Kind              m_declined = Kind::None;
    VrSceneSpace      m_space;
    VrPanelPlacement  m_panel;
    VrPanelPlacement  m_surface;
    bool              m_hasSurface = false;
    uint32_t          m_panelRevision = 0;
    VrMinigamePad     m_pad;

    // What the player would activate in the last frame without a menu
    void*             m_activateRef = nullptr;

    // The object of the world the scene lies on. What is hidden is kept alive
    // until it is shown again.
    bool              m_onObject = false;
    // The view is brought to the object's seat, or stays the player's
    bool              m_seated   = false;
    VrPose            m_reference;
    VrWorldObject     m_object;
    const VrSceneFit* m_fit = nullptr;
    // For a scene that holds the object's own model
    VrSceneFit        m_modelFit;
    VrGameCameraPose  m_viewCamera;
    std::vector<void*>     m_kept;
    std::vector<uint32_t*> m_objectHidden;
    // Characters hidden for now: they come and go while the world runs
    std::vector<uint32_t*> m_actorsHidden;
    uint32_t          m_actorsWait = 0;
    uint32_t*         m_bodyHidden = nullptr;

    // The button a gesture presses, held for a few frames
    uint16_t          m_pulse       = 0;
    uint32_t          m_pulseFrames = 0;

    // Lock
    VrPose            m_lock;
    float             m_pinAngle    = 0.0f;
    VrVector3         m_stickHand;
    bool              m_handDrives  = true;
    VrLockTension     m_tension;
    float             m_hapticWait  = 0.0f;

    // Slot machine
    std::array<VrLeverPull, VrHandCount> m_levers;
    std::array<bool, VrHandCount>        m_leverHeld = { };

    // Blackjack
    VrTableSignals    m_signals;

    // Roulette
    bool              m_betTrigger  = false;
    int32_t           m_selectorNudge = 1;

    // Vigor tester: the menu's camera as the menu made it, the hand the
    // cursor follows and the triggers of the last frame
    VrGameCameraPose  m_vigorCamera;
    VrGameFrustum     m_vigorFrustum;
    VrHand            m_vigorHand   = VrHand::Right;
    std::array<bool, VrHandCount> m_vigorTriggers = { };

    // Rendered menu: a terminal or the Reflectron, the depth of its model
    // ahead of its camera and that camera's frustum
    uint8_t*          m_rendered    = nullptr;
    void*             m_texture     = nullptr;
    float             m_renderedDepth = 0.0f;
    VrGameFrustum     m_renderedFrustum;

    Kind findKind() const;

    bool open(Kind kind, const VrMinigameFrame& frame);

    void close();

    void placeBoard(const VrMinigameFrame& frame, bool aboveTable);

    static bool renderedMenuReady(uint8_t* rendered);

    bool placeRenderedMenu(const VrMinigameFrame& frame, bool onScreen, float distance, float width);

    bool placeTerminalOnScreen(const VrMinigameFrame& frame);

    void placeTerminalPanel();

    bool placeOnObject(uint32_t id, const VrMinigameFrame& frame);

    uint8_t* activateRoot() const;

    void takeSeat(uint8_t* root, const VrWorldObject& object, const VrSceneFit* fit, const VrMinigameFrame& frame);

    void standAt(uint8_t* root, const VrWorldObject& object, const VrSceneFit* fit, const VrMinigameFrame& frame);

    void standOnObject(const VrMinigameFrame& frame);

    void placeOnObjectSpace();

    void keep(void* object);

    void hideRoot(uint8_t* root);

    void hidePlayerBody();

    void updateActorsInTheWay(bool logActors);

    void hideRaisedPayoutScreen(std::vector<uint32_t*>& hidden);

    void placeRouletteSurface();

    bool fitVigorTester();

    void placeVigorTesterPanel();

    void aimVigorTesterCamera(const VrInputState& input);

    static bool vigorTesterButtonOnRay(const VrVector3& origin, const VrVector3& direction);

    void leaveObject();

    void updateLock(const VrInputState& input, float dt);

    void updateSlotMachine(const VrInputState& input);

    void updateBlackJack(const VrInputState& input, float dt);

    void updateRoulette(const VrPointerSurfaceState& pointer);

    void press(uint16_t button);

    void drawRenderedMenu(const VrEyeView& eye);

    void renderRenderedMenuScreen();

    void releaseTexture();

    static void __cdecl afterOffscreen();

  };

}
