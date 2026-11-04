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
   * \brief Brings a point of the game world into the recenter reference
   *
   * The inverse of the position part of vrComputeEyeCameraPose: an eye
   * placed at the result is drawn at \c point.
   *
   * \param [in] base Pose the game computed for its camera this frame
   * \param [in] point Point in the game world, in game units
   * \param [in] unitsPerMetre Scale from metres to game units
   * \returns The point relative to the reference, in metres
   */
  VrVector3 vrComputeGamePointInReference(
    const VrGameCameraPose& base,
    const VrVector3&        point,
          float             unitsPerMetre);

  /**
   * \brief Moves the recenter reference along the game's world
   *
   * For the same base camera, an eye is drawn by \c shift further back
   * than before, with the base camera having moved by that much.
   *
   * \param [in] reference Result of vrMakeRecenterReference
   * \param [in] base Pose the game computed for its camera this frame
   * \param [in] shift Displacement in the game world, in game units
   * \param [in] unitsPerMetre Scale from metres to game units
   */
  VrPose vrShiftReference(
    const VrPose&           reference,
    const VrGameCameraPose& base,
    const VrVector3&        shift,
          float             unitsPerMetre);

  /**
   * \brief Heading and pitch of the head in the game's convention
   *
   * The yaw is clockwise seen from above, like the game's heading, and the
   * pitch is positive when looking down, like the game's look angle (see
   * findings/input.md). Both are in radians.
   */
  struct VrHeadAngles {
    float yaw   = 0.0f;
    float pitch = 0.0f;
  };

  /**
   * \brief Computes the heading and pitch of the head
   *
   * Roll is ignored. Looking straight up or down gives a yaw of zero.
   *
   * \param [in] headInReference Head pose relative to the recenter reference
   */
  VrHeadAngles vrComputeHeadAngles(const VrPose& headInReference);

  /**
   * \brief Builds a level camera pose facing a game heading
   *
   * \param [in] heading Heading in radians, 0 north, clockwise
   * \param [in] position Camera position in game units
   */
  VrGameCameraPose vrComputeBodyCameraPose(float heading, const VrVector3& position);

  /**
   * \brief Levels the camera of V.A.T.S. for a head that keeps its yaw
   *
   * The eyes add the head's yaw and pitch to the camera they are placed on.
   * V.A.T.S. turns the camera to its target, wherever the head is turned in
   * the room: on its own, the target would be seen at the head's yaw away
   * from where the player looks. Taking away the yaw the head had when
   * V.A.T.S. began puts the target in front of that head, and the tilt
   * of the game's camera is left to the player's own head.
   *
   * \param [in] game Camera pose the game computed
   * \param [in] headYaw Yaw of the head when V.A.T.S. began, see
   *    vrComputeHeadAngles
   * \returns A level camera at the same position
   */
  VrGameCameraPose vrComputeVatsCameraPose(const VrGameCameraPose& game, float headYaw);

  /**
   * \brief Moves a point as if a body standing on a pivot turned
   *
   * The game turns the player's whole skeleton around the player's position
   * when the heading changes. A point carried by the skeleton, such as the
   * camera, follows by this rotation. Height is kept.
   *
   * \param [in] point Point in game units
   * \param [in] pivot Position the body turns around
   * \param [in] headingChange Turn in radians, clockwise like the heading
   */
  VrVector3 vrTurnAroundPivot(const VrVector3& point, const VrVector3& pivot, float headingChange);

  /**
   * \brief Wraps an angle into [0, 2 pi), the range of the game's heading
   */
  float vrWrapHeading(float angle);

  /**
   * \brief Converts a field of view into a game frustum
   *
   * An asymmetric field of view gives an asymmetric frustum.
   */
  VrGameFrustum vrComputeGameFrustum(const VrFov& fov);

  /**
   * \brief Places the camera of a menu's own scene for a VR eye
   *
   * Rendered menus, such as the game's terminals, draw a model with a
   * camera of their own, in a space of their own where the camera looks
   * along +X with +Y up and +Z to its right. The model's point \c target
   * of that space is shown at the pose \c anchor of the room, whose -Z
   * axis points away from the viewer: an eye then sees the model as if it
   * stood there, scaled by \c unitsPerMetre.
   *
   * \param [in] anchor Where the target is shown, in tracking space
   * \param [in] eye Eye pose in tracking space
   * \param [in] target Point of the menu's space shown at the anchor
   * \param [in] unitsPerMetre Menu units per metre of the room
   * \returns The eye's camera in the menu's space
   */
  VrGameCameraPose vrComputeMenuSceneCameraPose(
    const VrPose&           anchor,
    const VrPose&           eye,
    const VrVector3&        target,
          float             unitsPerMetre);

  /**
   * \brief Finds where a point of a menu's own scene is seen in the room
   *
   * The reverse of the mapping of vrComputeMenuSceneCameraPose: a point
   * drawn in the menu's space appears there to the eyes.
   *
   * \param [in] anchor Where the target is shown, in tracking space
   * \param [in] target Point of the menu's space shown at the anchor
   * \param [in] unitsPerMetre Menu units per metre of the room
   * \param [in] point Point of the menu's space
   * \returns The point in tracking space
   */
  VrVector3 vrComputeMenuScenePointInRoom(
    const VrPose&           anchor,
    const VrVector3&        target,
          float             unitsPerMetre,
    const VrVector3&        point);

  /**
   * \brief Finds which way a direction of a menu's own scene points in the room
   *
   * \param [in] anchor Where the menu's scene is shown, see
   *    vrComputeMenuScenePointInRoom
   * \param [in] direction Direction in the menu's space
   * \returns The direction in tracking space, of the same length
   */
  VrVector3 vrComputeMenuSceneDirectionInRoom(
    const VrPose&           anchor,
    const VrVector3&        direction);

  /**
   * \brief Field of view that covers both eyes, widened by a margin
   *
   * Each side takes the wider of the two eyes and the margin, and is
   * limited to \ref VrMaxCullingAngle so that its tangent stays finite.
   *
   * \param [in] left Field of view of the left eye
   * \param [in] right Field of view of the right eye
   * \param [in] margin Angle added on every side, in radians
   */
  VrFov vrComputeCullingFov(const VrFov& left, const VrFov& right, float margin);

  /**
   * \brief Widest angle from the view direction a culling side may reach, in radians
   */
  constexpr float VrMaxCullingAngle = 1.3962634f;

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
