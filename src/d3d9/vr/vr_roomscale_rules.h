#pragma once

#include "vr_types.h"

namespace dxvk {

  /// How far the head goes from the player's capsule before the capsule follows, in metres
  constexpr float VrRoomscaleDeadZone = 0.12f;

  /// How far the head goes from a blocked capsule before the view stops at it, in metres
  constexpr float VrRoomscaleHeadRoom = 0.25f;

  /// Fastest the capsule catches up with the head, in metres per second
  constexpr float VrRoomscaleMaxSpeed = 5.0f;

  /// How long the capsule keeps moving by itself after a move was asked, in seconds
  constexpr float VrRoomscaleFollowTime = 0.25f;

  /// How long the capsule has to be blocked before the view stops, in seconds
  constexpr float VrRoomscaleBlockedTime = 0.1f;

  /**
   * \brief Move that brings the capsule back within the dead zone of the head
   *
   * All vectors of these functions are on the ground plane of the game's
   * world, in game units.
   *
   * \param [in] offset Head relative to the capsule
   * \param [in] deadZone Radius of the dead zone
   */
  VrVector2 vrRoomscaleWantedMove(VrVector2 offset, float deadZone);

  /**
   * \brief Shortens a move to the longest the capsule may make in one step
   */
  VrVector2 vrRoomscaleLimitMove(VrVector2 move, float maxStep);

  /**
   * \brief Part of what the capsule moved that the head's move accounts for
   *
   * The capsule moves by the game's own move, the stick's, and the head's
   * together, with the walls and slopes deciding what is left of them.
   * Without a game move everything is the head's, unless no head's move
   * was asked lately: then something else pushed the player, and the view
   * goes along.
   *
   * \param [in] moved What the capsule moved
   * \param [in] gameMove The move the game asked
   * \param [in] asked The head's move added to it
   * \param [in] following A head's move was asked lately
   * \param [in] maxStep Longest step: more than twice that is a teleport
   */
  VrVector2 vrRoomscaleAchievedMove(
          VrVector2             moved,
          VrVector2             gameMove,
          VrVector2             asked,
          bool                  following,
          float                 maxStep);

  /**
   * \brief Checks whether something stopped the capsule
   *
   * \param [in] achieved Result of vrRoomscaleAchievedMove
   * \param [in] asked The head's move
   */
  bool vrRoomscaleIsBlocked(VrVector2 achieved, VrVector2 asked);

  /**
   * \brief What the view gives back to stay within the head room
   *
   * \param [in] offset Head relative to the capsule
   * \param [in] headRoom Longest distance between the head and the capsule
   * \returns How far the head's place in the world has to move back, along
   *    the offset, which is zero within the head room
   */
  VrVector2 vrRoomscalePushback(VrVector2 offset, float headRoom);

}
