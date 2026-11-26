#pragma once

#include <memory>

#include "vr_pipboy_touch.h"

namespace dxvk {

  /**
   * \brief Lets the right index finger work the Pip-Boy's screen
   *
   * The game's own mouse pick of the Pip-Boy does the rest, see
   * findings/pipboy.md §9: FOPipboyManager::CheckOrangeButtonsClicked,
   * called by RenderPipBoy, casts a ray from the camera through the cursor
   * onto the screen and the three tab buttons, and the interface takes the
   * texture point it hits as the mouse on the Pip-Boy's menus. While the
   * finger has the cursor, that ray goes from in front of the screen to the
   * point the finger is at, or to the button it presses, along the screen's
   * normal, in the space RenderPipBoy draws the model in. The pick is made
   * once per frame and its results given again to the other eyes, so that a
   * tab clicked in one eye is not undone by the next one.
   *
   * The fingertip is measured on the drawn model by VrWeaponHand, which
   * calls measure once both forearms are placed for RenderPipBoy. The
   * screen stops the finger: once it comes from the front, the hand is
   * drawn resting on the glass however far the controller goes through,
   * until the finger is lifted or slides off the screen.
   *
   * Only used on the game's main thread.
   */
  class VrPipBoyTouchGame {

  public:

    /// The ray starts this far in front of the screen, in metres
    constexpr static float RayBack     = 0.05f;
    /// A tab button is looked for this far from the fingertip, in metres
    constexpr static float ButtonReach = 0.03f;
    /// The finger's pad rests on the glass this far from the measured
    /// fingertip, in metres
    constexpr static float FingerPad   = 0.007f;
    /// The glass stops the finger this far past the screen's texture, as a
    /// fraction of it
    constexpr static float RestMargin  = 0.03f;
    /// A hand deeper behind the screen is let through, in metres
    constexpr static float RestDepth   = 0.3f;

    ~VrPipBoyTouchGame();

    /**
     * \brief Redirects the game's calls
     * \returns \c nullptr if the calls were not found
     */
    static std::unique_ptr<VrPipBoyTouchGame> install();

    /**
     * \brief Starts a frame
     *
     * The measure of the previous frame becomes the one point() gives, and
     * the next measure is this frame's. Called before setAim.
     */
    void beginFrame();

    /**
     * \brief The fingertip on the Pip-Boy, as measured on the previous frame
     *
     * \returns \c nullptr if the Pip-Boy was not drawn with the finger
     */
    const VrPipBoyTouchPoint* point() const {
      return m_hasPoint ? &m_point : nullptr;
    }

    /**
     * \brief Sets what the game's pick looks at for this frame
     *
     * \param [in] aim The point or button, see VrPipBoyTouchState
     * \param [in] active Whether the finger has the cursor: otherwise the
     *    game picks along the camera as it does without the mod
     */
    void setAim(const VrPipBoyAim& aim, bool active);

    /**
     * \brief Measures the fingertip on the drawn Pip-Boy
     *
     * Called in RenderPipBoy's update of the first-person model, once the
     * forearms are placed, before the game's pick. All points are in the
     * space the model is drawn in.
     *
     * \param [in] fingertip The right index fingertip, where the
     *    controller puts it
     * \param [in] elbow The left forearm bone
     * \param [in] wrist The left hand bone
     * \returns How far to move the right hand so that the finger rests on
     *    the glass, zero if it does not touch it
     */
    VrVector3 measure(const VrVector3& fingertip, const VrVector3& elbow, const VrVector3& wrist);

  private:

    VrPipBoyTouchGame() = default;

    /// The screen fitted to its mesh, and what it was fitted to
    struct ScreenCache {
      const void*   data      = nullptr;
      uint32_t      vertices  = 0;
      VrVector2     firstUv;
      VrVector2     lastUv;
      VrTouchScreen screen;
    };

    ScreenCache        m_cache;
    bool               m_loggedScreen = false;
    bool               m_loggedTouch  = false;
    bool               m_loggedRest   = false;

    // Whether the finger was last in front of the glass, and whether the
    // glass stops it
    bool               m_inFront = false;
    bool               m_resting = false;

    VrPipBoyTouchPoint m_point;
    bool               m_hasPoint = false;
    VrPipBoyTouchPoint m_framePoint;
    bool               m_frameMeasured = false;

    VrPipBoyAim        m_aim;
    bool               m_active = false;

    // The ray for the pick that follows the measure
    VrVector3          m_rayOrigin;
    VrVector3          m_rayDirection;
    bool               m_hasRay = false;
    bool               m_rayOverride = false;

    // The pick of this frame, given again to the later eyes
    bool               m_picked = false;
    bool               m_pickHit = false;
    float              m_pickX = 0.0f;
    float              m_pickY = 0.0f;
    uint32_t           m_pickTab = 0;

    const VrTouchScreen* screen(const uint8_t* geometry);

    static char __fastcall checkButtonsHook(
            void*                 pipBoy,
            void*                 unused,
            uint32_t              pressed,
            uint32_t              released,
            float*                x,
            float*                y,
            uint32_t*             tab);

    static bool __fastcall rayHook(
            void*                 camera,
            void*                 unused,
            int32_t               x,
            int32_t               y,
            float*                origin,
            float*                direction,
            void*                 target);

  };

}
