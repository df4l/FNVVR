#pragma once

#include "vr_game_space.h"

namespace dxvk {

  /**
   * \brief Camera values saved and restored around an eye render
   */
  struct VrGameCameraState {
    float rotation[9];
    float translation[3];
    float frustum[6];
  };

  /**
   * \brief Access to the game's world camera (NiCamera)
   *
   * Reads and writes the camera through its known memory layout and asks the
   * game to refresh the matrices it derives from it. Nothing here knows about
   * VR: callers pass poses and frustums in the game's own frame.
   */
  class VrGameCamera {

  public:

    /**
     * \brief Looks up the world camera
     * \returns \c false while the game has no scene graph
     */
    bool acquire();

    /**
     * \brief Checks whether a pointer is the camera found by acquire()
     */
    bool isCamera(const void* camera) const { return camera && camera == m_camera; }

    VrGameCameraState save() const;

    /**
     * \brief Writes a saved state back and refreshes the camera
     */
    void restore(const VrGameCameraState& state);

    /**
     * \brief Pose the game computed for the camera this frame
     */
    VrGameCameraPose readPose() const;

    /**
     * \brief Moves the camera and sets its frustum
     *
     * The near and far planes are kept. The rotation is written as
     * the columns forward, up and right, and the cached matrices are
     * refreshed so that the next render uses the new values.
     */
    void apply(const VrGameCameraPose& pose, const VrGameFrustum& frustum);

  private:

    uint8_t* m_camera = nullptr;

    void refresh();

  };

}
