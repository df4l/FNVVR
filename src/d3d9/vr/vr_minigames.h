#pragma once

#include <array>
#include <cstdint>
#include <memory>

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
  };

  /**
   * \brief Shows the game's mini-games in the room
   *
   * The lockpicking, slot machine, blackjack, roulette and Caravan menus
   * and the world's terminals (hacking included) keep all of their game
   * logic. Their 3D scene, which the game draws with a camera of its own,
   * is drawn in each eye's image with that camera on the eye, at the place
   * of the room the game's view gives it, instead of on the flat panel. The
   * interface's tiles stay on the panel, which is out of the way of the
   * scene. The controllers act on the scene: see findings/minigames.md.
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

    /// Whether the eyes show a world's terminal
    bool showsTerminal() const { return m_kind == Kind::Terminal; }

    bool active() const { return m_kind != Kind::None; }

    /// Whether the hands are shown, without the weapon
    bool showsHands() const { return active() && m_kind != Kind::Terminal; }

    /// The gamepad input of the hands' gestures, for the next frame
    const VrMinigamePad& pad() const { return m_pad; }

    /// The surface the lasers can point at, \c nullptr if none
    const VrPanelPlacement* surface() const { return m_hasSurface ? &m_surface : nullptr; }

    /// Where the panel goes, \c nullptr to place it as usual
    const VrPanelPlacement* panel() const { return active() ? &m_panel : nullptr; }

    /// Changes whenever the panel has to be placed again
    uint32_t panelRevision() const { return m_panelRevision; }

    /**
     * \brief Draws the scene for an eye
     *
     * \param [in] eye The eye
     * \param [in] nearPlane Near plane of the world's camera, in its units
     * \param [in] farPlane Far plane of the world's camera
     */
    void drawEye(const VrEyeView& eye, float nearPlane, float farPlane);

    /// Whether the scene can be drawn before the first-person pass
    bool drawsBeforeFirstPerson() const { return active() && m_kind != Kind::Terminal; }

  private:

    enum class Kind {
      None,
      Lock,
      SlotMachine,
      BlackJack,
      Roulette,
      Caravan,
      Terminal,
    };

    VrMinigames(IDirect3DDevice9* device, IVRBackend& backend);

    IDirect3DDevice9* m_device;
    IVRBackend&       m_backend;

    Kind              m_kind = Kind::None;
    VrSceneSpace      m_space;
    VrPanelPlacement  m_panel;
    VrPanelPlacement  m_surface;
    bool              m_hasSurface = false;
    uint32_t          m_panelRevision = 0;
    VrMinigamePad     m_pad;

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

    // Terminal
    uint8_t*          m_terminal    = nullptr;
    void*             m_texture     = nullptr;

    Kind findKind() const;

    bool open(Kind kind, const VrMinigameFrame& frame);

    void close();

    void placeBoard(const VrMinigameFrame& frame, bool aboveTable);

    bool placeTerminal(const VrMinigameFrame& frame);

    void updateLock(const VrInputState& input, float dt);

    void updateSlotMachine(const VrInputState& input);

    void updateBlackJack(const VrInputState& input, float dt);

    void updateRoulette(const VrPointerSurfaceState& pointer);

    void press(uint16_t button);

    void drawTerminal(const VrEyeView& eye);

    void renderTerminalScreen();

    void releaseTexture();

    static void __cdecl afterOffscreen();

  };

}
