#pragma once

#include "vr_math.h"
#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Game units per metre
   *
   * See findings/scale.md: k_dUnitsPerMeter_69_99 in the executable.
   */
  constexpr float VrGameUnitsPerMetre = 69.99126f;

  /**
   * \brief Camera pose in the game world
   *
   * The game world is right-handed with X east, Y north and Z up, in game
   * units. The axes are unit vectors and right = forward x up, which is how
   * the game stores them as the columns of a camera rotation.
   */
  struct VrGameCameraPose {
    VrVector3 forward;
    VrVector3 up;
    VrVector3 right;
    VrVector3 position;
  };

  /**
   * \brief Frustum extents as tangents at unit distance
   *
   * Left and bottom are negative. This is the unit the game's NiFrustum uses.
   */
  struct VrGameFrustum {
    float left   = 0.0f;
    float right  = 0.0f;
    float top    = 0.0f;
    float bottom = 0.0f;
  };

  /**
   * \brief Builds the reference frame for a recenter
   *
   * The reference keeps the head position and only its heading, so that
   * looking up or down after the recenter still tilts the camera.
   *
   * \param [in] head Head pose in tracking space at the time of the recenter
   */
  VrPose vrMakeRecenterReference(const VrPose& head);

  /**
   * \brief Expresses an eye pose relative to the recenter reference
   *
   * \param [in] reference Result of vrMakeRecenterReference
   * \param [in] eyeInTracking Eye pose in tracking space
   */
  VrPose vrComputeEyeInReference(const VrPose& reference, const VrPose& eyeInTracking);

  /**
   * \brief Places a VR eye on top of the game's own camera
   *
   * The game camera stands for the recentered head: an eye at the reference
   * pose gets exactly the base pose, and any displacement or rotation of the
   * eye is applied in the base camera's frame.
   *
   * \param [in] base Pose the game computed for its camera this frame
   * \param [in] eyeInReference Result of vrComputeEyeInReference
   * \param [in] unitsPerMetre Scale from metres to game units
   */
  VrGameCameraPose vrComputeEyeCameraPose(
    const VrGameCameraPose& base,
    const VrPose&           eyeInReference,
          float             unitsPerMetre);

  /**
   * \brief Converts a field of view into a game frustum
   *
   * An asymmetric field of view gives an asymmetric frustum.
   */
  VrGameFrustum vrComputeGameFrustum(const VrFov& fov);

  /**
   * \brief Narrowest width to height ratio the game's interface is laid out for
   *
   * The interface is designed for 4:3 and wider screens. A headset eye is
   * usually narrower than that.
   */
  constexpr float VrGameMinAspect = 4.0f / 3.0f;

  /**
   * \brief Computes the resolution the game renders at for a headset
   *
   * The game draws one frame per eye into its backbuffer, which is then
   * scaled into the eye image. The height is the eye's, so that no detail
   * is lost vertically, and the width is the eye's or more, so that the
   * frame is at least as wide as \c minAspect allows. The width is even.
   *
   * \param [in] eye Recommended size of one eye's image
   * \param [in] minAspect Narrowest width to height ratio
   */
  VrExtent vrComputeGameResolution(const VrExtent& eye, float minAspect);

}
