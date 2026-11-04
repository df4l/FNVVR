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
   * Only the position follows the animation. The orientation stays level and
   * on the player's heading, the headset's own being added to it: an animation
   * turning the view without the head turning is not bearable in a headset.
   *
   * The same goes for the Sandman and Cannibal kills, on the same temporary
   * third person. A knocked down player stays on the game's camera: the head of
   * a ragdoll cannot be followed. Going in and out of the view is blended.
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
     */
    void placeCamera(VrGameCameraPose& camera);

    /**
     * \brief Blends the camera after a change of view
     *
     * \param [in,out] camera The camera the eyes are about to be placed from
     * \param [in] dt Time since the last frame, in seconds
     */
    void finishCamera(VrGameCameraPose& camera, float dt);

    /// Whether the eyes follow the body's head
    bool followsHead() const { return m_following; }

  private:

    VrBodyView() = default;

    VrCameraBlend m_blend;
    bool          m_following = false;
    bool          m_changed   = false;
    bool          m_logged    = false;

  };

}
