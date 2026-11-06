#pragma once

#include <cstdint>
#include <memory>

#include "vr_game_space.h"

namespace dxvk {

  /**
   * \brief Keeps the view in the player's head while the game shows the body
   *
   * The game plays sitting down and getting up on the third-person body, and
   * moves its camera behind the player for that time (temporary third person,
   * findings/camera.md §8.1). The animation is left to play, but the eyes are
   * placed from the head of the body, as Enhanced Camera does on a screen, and
   * the body is seen from inside: its head is shrunk to a point for the frame.
   *
   * The position follows the animation, and the heading follows the way the
   * head of the body turns: its animation turns it around, which the player's
   * heading only does at its end, in one go (findings/camera.md §8.3). On
   * furniture the view stays level: an animation tilting it without the head
   * is not bearable in a headset, and the headset's own rotation is added to it.
   *
   * The same goes for the Sandman and Cannibal kills, on the same temporary
   * third person. A knocked down player stays on the game's camera: the head
   * of a ragdoll cannot be followed.
   *
   * In first person, scripts play their idles on the first-person skeleton,
   * such as waking up and getting out of bed at a new game, and the game's
   * camera follows them. Its tilt is kept then, so that lying on the back
   * looks at the ceiling, and the head's look is held: the look angle bends
   * that skeleton, and nodding would move the view up and down.
   *
   * Going in and out of these views is blended.
   *
   * Only used on the game's main thread.
   */
  class VrBodyView {

  public:

    /**
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrBodyView> install();

    /**
     * \brief Replaces the game's camera while the body is shown
     *
     * Called once per frame, before the head's look turns the camera. On a
     * seat, where it does not, the game's own look angle is taken out: it is
     * the one the head had when the player sat down, and would stay in the view.
     *
     * \param [in,out] camera The game's camera
     * \param [in] dt Time since the last frame, in seconds
     */
    void placeCamera(VrGameCameraPose& camera, float dt);

    /**
     * \brief Blends the camera after a change of view
     *
     * \param [in,out] camera The camera the eyes are about to be placed from
     * \param [in] headTurnsCamera Whether the head's look has just placed it
     * \param [in] dt Time since the last frame, in seconds
     */
    void finishCamera(VrGameCameraPose& camera, bool headTurnsCamera, float dt);

    /// Whether the eyes are not on the game's camera as the head's look
    /// turns it: on the body, a seat or an idle's tilted camera
    bool movesView() const { return m_view != View::Game; }

    /// Heading of the camera the eyes were last placed from
    float viewHeading() const { return m_viewHeading; }

    /// Whether an idle in first person places the eyes: the head must not
    /// write the player's look
    bool holdsLook() const { return m_view == View::Idle; }

  private:

    VrBodyView() = default;

    /**
     * \brief Logs the changes of the first-person idle's state
     *
     * A few of them: they tell whether an animation the view should follow
     * is seen as an idle.
     */
    void logIdleState(uintptr_t animation);

    enum class View { Game, Body, Seat, Idle };

    VrCameraBlend m_blend;
    VrViewHeading m_heading;
    View          m_view        = View::Game;
    float         m_viewHeading = 0.0f;
    float         m_pitch       = 0.0f;
    float         m_idleLeft    = 0.0f;
    bool          m_findsIdles  = false;
    int32_t       m_idleState   = -1;
    uint32_t      m_idleStateLogs = 0;
    bool          m_hasView     = false;
    bool          m_headTurns   = false;
    bool          m_changed     = false;
    bool          m_logged      = false;
    bool          m_loggedIdle  = false;

  };

}
