#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "../d3d9_include.h"

#include "vr_backend.h"
#include "vr_caravan_pick.h"
#include "vr_caravan_reach.h"
#include "vr_controller_pad.h"
#include "vr_game_camera.h"
#include "vr_game_nodes.h"
#include "vr_minigame_space.h"

namespace dxvk {

  class VrSceneModel;

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

    /// Whether the hands are shown, without the weapon: Caravan's cards are
    /// held in the left hand and chosen with the right
    bool showsHands() const { return m_kind == Kind::Caravan; }

    /// The gamepad input of the hands' gestures, for the next frame
    const VrMinigamePad& pad() const { return m_pad; }

    /// The surface the lasers can point at, \c nullptr if none
    const VrPanelPlacement* surface() const { return m_hasSurface ? &m_surface : nullptr; }

    /// Where the panel goes, \c nullptr to place it as usual
    const VrPanelPlacement* panel() const { return active() ? &m_panel : nullptr; }

    /// Changes whenever the panel has to be placed again
    uint32_t panelRevision() const { return m_panelRevision; }

    /// The values of Caravan's tracks and piles to lay on the mat, \c nullptr if none
    const VrCaravanBoard* caravanBoard() const { return m_hasCaravanBoard ? &m_caravanBoard : nullptr; }

    /**
     * \brief Says whether the numbers of caravanBoard() are shown
     *
     * The game's own values and cards left are hidden while they are.
     */
    void setCaravanValuesShown(bool shown);

    /**
     * \brief Where the Caravan challenger looks instead of the player
     *
     * The middle of the mat while cards move or the AI plays: actors look
     * at the player's looking-at location, so it is given this point then.
     * \param [out] point The point, in the game world
     * \returns \c false on the player's turn or outside a Caravan game
     */
    bool caravanTableGaze(VrVector3& point) const;

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

    /**
     * \brief Bends the Caravan challenger's free arm while they play a card
     *
     * The hand takes the card from the fan to the mat. Only the world
     * transforms of the arm's bones change, after the game's update, for
     * the eyes' draws: restoreChallengerArm() puts them back.
     */
    void bendChallengerArm();

    /// Puts back the arm bendChallengerArm() bent
    void restoreChallengerArm();

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
    // Whether the world's surfaces can be found with the game's rays
    bool              m_canPick = false;
    // The table under Caravan's mat when the world has none
    std::unique_ptr<VrSceneModel> m_caravanTable;
    std::vector<void*>     m_kept;
    std::vector<uint32_t*> m_objectHidden;
    // Characters hidden for now: they come and go while the world runs
    std::vector<uint32_t*> m_actorsHidden;
    uint32_t          m_actorsWait = 0;
    uint32_t*         m_bodyHidden = nullptr;

    // The button or trigger a gesture presses, held for a few frames
    uint16_t          m_pulse       = 0;
    bool              m_pulseLeftTrigger  = false;
    bool              m_pulseRightTrigger = false;
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

    // Caravan: what the laser is on (a hand card, a track * rows + row,
    // -1 for nothing), and a trigger's click waiting for the menu to take
    // the step to it
    int32_t           m_caravanCard = -1;
    int32_t           m_caravanSlot = -1;
    bool              m_caravanTrigger = false;
    bool              m_caravanClick   = false;
    float             m_caravanClickAge = 0.0f;
    // B and Y during the game, and the caravan a held B discards once the
    // menu has selected it (-1 for none)
    VrCaravanButtons  m_caravanButtons;
    int32_t           m_caravanDiscard = -1;
    float             m_caravanDiscardAge = 0.0f;
    // The values of the tracks on the mat, and whether the game's are hidden
    bool              m_canHideTiles = false;
    bool              m_canReadTiles = false;
    uint32_t          m_promptIconTrait = 0;
    bool              m_hasCaravanBoard = false;
    VrCaravanBoard    m_caravanBoard;
    bool              m_caravanTilesHidden = false;
    // How many cards out of the game's frame the eyes last left out, for the log
    size_t            m_caravanCardsOutOfGame = 0;
    bool              m_caravanPlanksHidden = false;
    bool              m_caravanCoinsLaid = false;
    // The selection as it was before a step was asked, put back if the menu
    // leaves its idle state before it takes the step
    bool              m_caravanStepping = false;
    int32_t           m_caravanWasCard  = 0;
    int32_t           m_caravanWasTrack = 0;
    int32_t           m_caravanWasRow   = 0;
    // Where the player's hand of cards is carried from the table, in the
    // left hand during the game
    // Deck building: the card on the laser (as caravanDeckCardOnLaser) and
    // the clicked card brought to the middle
    int32_t             m_caravanDeckSlot = -1;
    VrCaravanDeckPicker m_caravanDeck;
    bool              m_caravanFanHeld = false;
    int32_t           m_caravanCursorSlot = -1;
    VrCaravanTransform m_caravanFan;
    // The opponent's hand of cards: whether the challenger's left hand is
    // raised to hold it, and where it holds it in the scene
    bool              m_caravanOpponentHeld = false;
    bool              m_caravanFadingOut = false;
    VrVector3         m_caravanOpponentHold = { };
    VrVector3         m_caravanOpponentUp   = { 0.0f, 0.0f, 1.0f };
    // A card played, carried from a hand to its row, where the card is
    // drawn in the scene. The challenger's free hand takes it from the fan
    // (the fan's last card, the fan as big as before the play), with the
    // arm bent for it. The player's goes from its place in the left hand's
    // fan (the hand's slot, as the table had it when the play started).
    struct CaravanPlay {
      bool               active  = false;
      float              time    = 0.0f;
      uint32_t           track   = 0;
      uint32_t           row     = 0;
      uint32_t           column  = 0;
      uint32_t           fanCount = 0;
      uint32_t           slot    = 0;
      VrCaravanReachStep step;
      VrCaravanTransform rest;
      VrCaravanTransform card;
    };
    CaravanPlay       m_caravanPlay;
    CaravanPlay       m_caravanOwnPlay;
    uint32_t          m_caravanLastState    = 0;
    uint32_t          m_caravanOwnLastState = 0;
    uint32_t          m_caravanOpponentCards = 0;
    bool              m_caravanReachLeft    = true;
    VrGameWorldTransforms m_challengerArm;

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

    /** The menu being drawn is still the game's current rendered menu */
    bool renderedMenuAlive() const;

    bool placeRenderedMenu(const VrMinigameFrame& frame, bool onScreen, float distance, float width);

    bool placeTerminalOnScreen(const VrMinigameFrame& frame);

    void placeTerminalPanel();

    bool placeOnObject(uint32_t id, const VrMinigameFrame& frame);

    uint8_t* activateRoot() const;

    void takeSeat(uint8_t* root, const VrWorldObject& object, const VrSceneFit* fit, const VrMinigameFrame& frame);

    bool placeCaravanTable(const VrMinigameFrame& frame);

    void layCaravanMat(VrWorldObject table, const VrMinigameFrame& frame);

    void showCaravanTable();

    void standAt(uint8_t* root, const VrWorldObject& object, const VrSceneFit* fit, const VrMinigameFrame& frame);

    void standOnObject(const VrMinigameFrame& frame);

    void placeOnObjectSpace();

    void keep(void* object);

    void hideRoot(uint8_t* root);

    void hidePlayerBody();

    void updateActorsInTheWay(bool logActors);

    void hideRaisedPayoutScreen(std::vector<uint32_t*>& hidden);

    void hideCaravanCardsOutOfGame(std::vector<uint32_t*>& hidden);

    void hideCaravanPlanks(std::vector<uint32_t*>& hidden);

    void layCaravanCoins(uint8_t* menu);

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

    void placeCaravanSurface();

    void updateCaravan(const VrMinigameFrame& frame, const VrPointerSurfaceState& pointer, const VrInputState& input);

    void placeCaravanFan(const VrInputState& input, bool playing);

    VrCaravanTransform caravanCardInHand(uintptr_t card) const;

    VrCaravanTransform caravanCardInHand(const VrCaravanTransform& world) const;

    int32_t caravanCardLeavingHand(uint8_t* menu) const;

    void holdCaravanFan(std::vector<uint32_t*>& hidden, VrGameWorldTransforms& moved);

    void placeOpponentFan(uint8_t* menu, bool playing);

    void holdOpponentFan(std::vector<uint32_t*>& hidden, VrGameWorldTransforms& moved);

    void updateOpponentPlay(uint8_t* menu, bool playing, float dt);

    void updateOwnPlay(uint8_t* menu, bool playing, float dt);

    uintptr_t playCard(uint8_t* menu, const CaravanPlay& play) const;

    void carryCaravanPlay(const CaravanPlay& play, std::vector<uint32_t*>& hidden, VrGameWorldTransforms& moved);

    uintptr_t challengerRoot(uint8_t* menu) const;

    VrVector3 caravanSceneToWorld(const VrVector3& point) const;

    static void carryCaravanCard(uintptr_t model, uintptr_t card, const VrCaravanTransform& place,
      const std::string& shadow, std::vector<uint32_t*>& hidden);

    void placeCaravanCursor(VrGameWorldTransforms& moved);

    bool caravanCardOnLaser(uint8_t* menu, const VrVector3& origin, const VrVector3& direction);

    bool caravanSlotOnLaser(uint8_t* menu, const VrVector2& at, bool ownTracksOnly);

    void askCaravanStep(uint8_t* menu, VrCaravanKey key, VrHand hand);

    bool placeCaravanBoard(uint8_t* menu);

    void updateCaravanDeck(uint8_t* menu, const VrPointerSurfaceState& pointer, const VrInputState& input, float dt);
    /// The deck building's card on the laser: the deck's row 0-23, the
    /// available row 24-47, -1 for none
    int32_t caravanDeckCardOnLaser(uint8_t* menu, const VrVector3& origin, const VrVector3& direction) const;
    bool caravanCardInDeck(uint8_t* menu, uint32_t card) const;
    /// Shows B on the game menu's discard prompts, which show the triggers
    void showCaravanDiscardPrompts(uint8_t* menu);
    void commandCaravan(uint8_t* menu, VrCaravanCommand command, VrHand hand, const VrVector2* at);

    void press(uint16_t button);

    void pressTrigger(VrHand side);

    void drawRenderedMenu(const VrEyeView& eye);

    void renderRenderedMenuScreen();

    void releaseTexture();

    static void __cdecl afterOffscreen();

  };

}
