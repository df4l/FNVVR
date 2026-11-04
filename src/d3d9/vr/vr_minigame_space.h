#pragma once

#include <array>

#include "vr_game_space.h"
#include "vr_menu_pointer.h"
#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Where the 3D scene of a menu stands in the room
   *
   * A point of the scene at \c origin is seen at the anchor, and the scene's
   * \c right, \c up and \c back axes are the anchor's X, Y and Z axes. The
   * axes are unit vectors of the scene's space, with right x up = back.
   */
  struct VrSceneSpace {
    VrPose    anchor;
    VrVector3 origin;
    VrVector3 right;
    VrVector3 up;
    VrVector3 back;
    float     unitsPerMetre = VrGameUnitsPerMetre;
  };

  /**
   * \brief Stands a table's scene around the head
   *
   * The scene's own camera is put on the head as it was when the menu
   * opened: the table is where the game shows it, at the world's scale when
   * the model is. Scene +Z is up and the camera's heading is the head's
   * heading, its slope is dropped.
   *
   * \param [in] head Head pose in tracking space
   * \param [in] camera Position of the scene's camera
   * \param [in] forward Direction the scene's camera looks in
   * \param [in] unitsPerMetre Scene units per metre of the room
   */
  VrSceneSpace vrSceneSpaceAroundHead(
    const VrPose&           head,
    const VrVector3&        camera,
    const VrVector3&        forward,
          float             unitsPerMetre);

  /**
   * \brief Stands a menu's scene in front of the head
   *
   * For the scenes in the space of a menu's own camera: +X ahead, +Y up
   * and +Z to the right, see findings/minigames.md.
   *
   * \param [in] head Head pose in tracking space
   * \param [in] target Point of the scene put in front of the head
   * \param [in] distance Distance of the target ahead of the head, in metres
   * \param [in] drop Height of the target below the eyes, in metres
   * \param [in] unitsPerMetre Scene units per metre of the room
   */
  VrSceneSpace vrSceneSpaceInFront(
    const VrPose&           head,
    const VrVector3&        target,
          float             distance,
          float             drop,
          float             unitsPerMetre);

  /**
   * \brief An object of the world, as its model stands in it
   *
   * A point of the model at (x, y, z) is in the world at
   * position + (this->x * x + this->y * y + this->z * z) * scale.
   */
  struct VrWorldObject {
    VrVector3 x;
    VrVector3 y;
    VrVector3 z;
    VrVector3 position;
    float     scale = 1.0f;
  };

  /**
   * \brief A rectangle of the ground a world's object stands on, in its model
   */
  struct VrModelArea {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
  };

  /**
   * \brief How a menu's scene lies on the model of a world's object
   *
   * The axes are unit vectors of the model's space, with x cross y = z. A
   * point of the scene at (x, y, z) is in the model at
   * origin + (sceneX * x + sceneY * y + sceneZ * z) * scale.
   */
  struct VrSceneFit {
    VrVector3 sceneX;
    VrVector3 sceneY;
    VrVector3 sceneZ;
    VrVector3 origin;
    /// Model units per scene unit
    float     scale = 1.0f;
    /// Where the player's head goes, in the model. Its height is not used.
    VrVector3 seat;
    /// Direction the player looks in from there, level, in the model
    VrVector3 facing;
    /// What the object takes of the ground, in the model
    const VrModelArea* areas = nullptr;
    uint32_t  areaCount = 0;
    /// Height of the head above the model's origin, 0 to keep the game camera's
    float     eyeHeight = 0.0f;
  };

  /**
   * \brief Finds the camera that puts the head at the seat of an object
   *
   * It takes the place of the game's camera, from which the eyes are placed
   * (vrComputeEyeCameraPose): the head as it is now gets to the seat, looking
   * the way the fit says. Without an eye height in the fit, its height does
   * not change: a player sitting stays low, a player standing keeps the floor
   * under the feet. With one, the head as it is now gets to that height too.
   *
   * \param [in] object The object
   * \param [in] fit How the scene lies on it
   * \param [in] headInReference Head pose, see vrComputeEyeInReference
   * \param [in] cameraHeight Height of the game's camera in the world
   * \param [in] unitsPerMetre World units per metre of the room
   */
  VrGameCameraPose vrSeatCamera(
    const VrWorldObject&    object,
    const VrSceneFit&       fit,
    const VrPose&           headInReference,
          float             cameraHeight,
          float             unitsPerMetre);

  /**
   * \brief Stands a scene where an object of the world is seen
   *
   * \param [in] reference Recenter reference, see vrMakeRecenterReference
   * \param [in] camera The camera the eyes are placed from
   * \param [in] object The object
   * \param [in] fit How the scene lies on it
   * \param [in] unitsPerMetre World units per metre of the room
   */
  VrSceneSpace vrSceneSpaceOnObject(
    const VrPose&           reference,
    const VrGameCameraPose& camera,
    const VrWorldObject&    object,
    const VrSceneFit&       fit,
          float             unitsPerMetre);

  /**
   * \brief Finds a point of the world in the model of an object
   *
   * \param [in] object The object
   * \param [in] point The point, in the world
   * \returns The point in the model's space
   */
  VrVector3 vrPointInObject(
    const VrWorldObject&    object,
    const VrVector3&        point);

  /**
   * \brief Checks whether a body at a point is in the way of a scene
   *
   * The game's AI is frozen while a menu is open, so someone walking by stays
   * where the menu found them: inside the object, or on the player's seat.
   *
   * \param [in] fit How the scene lies on the object
   * \param [in] body Where the body is, in the model's space
   * \returns \c true if the body stands in the object or on the seat
   */
  bool vrBodyInTheWay(
    const VrSceneFit&       fit,
    const VrVector3&        body);

  VrVector3 vrSceneToRoom(const VrSceneSpace& space, const VrVector3& point);

  VrVector3 vrRoomToScene(const VrSceneSpace& space, const VrVector3& point);

  /**
   * \brief Finds the camera that shows the scene to an eye
   *
   * \param [in] space Where the scene stands
   * \param [in] eye Eye pose in tracking space
   * \returns The eye's camera in the scene's space
   */
  VrGameCameraPose vrSceneCameraPose(const VrSceneSpace& space, const VrPose& eye);

  /**
   * \brief Finds the rectangle of a table's scene that can be pointed at
   *
   * The placement's X and Y axes are the scene's +X and +Y, and it faces
   * scene +Z, so that it is pointed at from above. A hit converts back to
   * the scene with vrSurfacePoint.
   *
   * \param [in] space Where the scene stands
   * \param [in] centre Centre of the rectangle, in the scene
   * \param [in] sizeX Size along scene +X, in scene units
   * \param [in] sizeY Size along scene +Y, in scene units
   */
  VrPanelPlacement vrSceneSurface(
    const VrSceneSpace&     space,
    const VrVector3&        centre,
          float             sizeX,
          float             sizeY);

  /**
   * \brief Finds the point of the scene's plane under a hit on its surface
   *
   * \param [in] centre Centre given to vrSceneSurface
   * \param [in] sizeX Size given to vrSceneSurface
   * \param [in] sizeY Size given to vrSceneSurface
   * \param [in] uv Hit on the surface, see VrPanelHit
   */
  VrVector3 vrSurfacePoint(
    const VrVector3&        centre,
          float             sizeX,
          float             sizeY,
    const VrVector2&        uv);

  /**
   * \brief Finds the rectangle a menu's camera sees across its view
   *
   * The camera is at the origin of the scene's space, looking along +X, as
   * the scenes of vrSceneSpaceInFront. A point of this rectangle at (u, v)
   * is seen by that camera at the same (u, v) of its image, so a laser on
   * it gives the position of the game's cursor.
   *
   * \param [in] space Where the scene stands
   * \param [in] distance Distance from the camera along its axis, in scene units
   * \param [in] frustum The camera's frustum
   */
  VrPanelPlacement vrSceneViewRectangle(
    const VrSceneSpace&     space,
          float             distance,
    const VrGameFrustum&    frustum);

  /**
   * \brief Finds the angle of the lock pin from the hand holding it
   *
   * The pin points from the keyhole at the hand. Too close to the keyhole
   * for a direction to be known, the previous angle stays.
   *
   * \param [in] lock Keyhole in tracking space, its +Z axis towards the player
   * \param [in] hand Position of the hand in tracking space
   * \param [in] previous Previous angle in degrees
   * \returns Angle in degrees, 0 is up and positive to the player's right,
   *    limited to the half turn the pin can reach
   */
  float vrLockPinAngle(const VrPose& lock, const VrVector3& hand, float previous);

  /**
   * \brief Finds the lock pin's position on the game's scale
   *
   * \param [in] angle Angle from vrLockPinAngle
   * \param [in] width Size of the scale: the game's pin slider
   * \returns Position of the pin slider, 0 at the far left
   */
  float vrLockPinSlider(float angle, float width);

  /**
   * \brief Left stick X that moves the pin slider to a position in one frame
   *
   * The game adds v^3 * 80 to the slider each frame, with a dead zone.
   *
   * \param [in] slider Position of the slider
   * \param [in] target Wanted position
   * \returns Stick value in [-1, 1], 0 when the slider is close enough
   */
  float vrLockPinStick(float slider, float target);

  /**
   * \brief Right stick X that turns the screwdriver
   *
   * \param [in] tension Amount in [0, 1]
   * \returns Stick value in [-1, 0], outside the dead zone unless \c tension is 0.
   *    The game reads a negative value as turning.
   */
  float vrLockTensionStick(float tension);

  /**
   * \brief The screwdriver in the left hand
   *
   * The trigger turns it at full speed. Holding the grip takes it: then the
   * twist of the wrist from where it was taken turns it, in either
   * direction, the further the faster.
   */
  class VrLockTension {

  public:

    /// Twist that has no effect, in radians
    constexpr static float DeadTwist = 0.15f;
    /// Twist at full speed, in radians
    constexpr static float FullTwist = 0.8f;

    /**
     * \param [in] trigger Whether the left trigger is pressed
     * \param [in] grip Whether the left grip is held
     * \param [in] hand Left hand's orientation
     * \returns Amount in [0, 1]
     */
    float update(bool trigger, bool grip, const VrQuaternion& hand);

  private:

    bool         m_held = false;
    VrQuaternion m_taken;

  };

  /**
   * \brief The slot machine's lever under a hand
   *
   * A pull starts with the grip closed on the lever's ball and ends when
   * the hand has come down far enough. The grip has to open before the next.
   */
  class VrLeverPull {

  public:

    /// Distance from the ball at which the grip takes the lever, in metres
    constexpr static float Reach    = 0.20f;
    /// Way down of a pull, in metres
    constexpr static float PullDown = 0.10f;

    /**
     * \param [in] grip Whether the hand's grip is held
     * \param [in] hand Hand's position in tracking space
     * \param [in] ball Lever's ball in tracking space
     * \returns Whether the lever was pulled in this update
     */
    bool update(bool grip, const VrVector3& hand, const VrVector3& ball);

    /// Whether the hand holds the lever
    bool holds() const { return m_holds; }

  private:

    bool  m_holds    = false;
    bool  m_gripWas  = false;
    bool  m_pulled   = false;
    float m_startY   = 0.0f;

  };

  /**
   * \brief A hand above a card table, in metres
   */
  struct VrTableHand {
    bool  active = false;
    /// Whether the hand is over the table's top
    bool  over   = false;
    /// Height above the felt
    float height = 0.0f;
    /// Position along the player's right
    float side   = 0.0f;
  };

  enum class VrTableSignal {
    None,
    /// A hand tapped the felt: hit
    Tap,
    /// A hand waved over the table: stand
    Wave,
  };

  /**
   * \brief Recognises the two signals players give at a real card table
   */
  class VrTableSignals {

  public:

    /// Height a tap starts above and ends below, in metres
    constexpr static float TapHeight = 0.05f;
    /// Speed downwards of a tap, in metres a second
    constexpr static float TapSpeed  = 0.5f;
    /// Height under which a hand can wave, in metres
    constexpr static float WaveHeight = 0.45f;
    /// Speed sideways that counts for a wave, in metres a second
    constexpr static float WaveSpeed = 0.5f;
    /// Changes of direction of a wave
    constexpr static uint32_t WaveTurns = 3;
    /// Time a change of direction counts for, in seconds
    constexpr static float WaveWindow = 0.6f;
    /// Time after a signal during which no other one is given, in seconds
    constexpr static float Cooldown  = 1.2f;

    VrTableSignal update(const std::array<VrTableHand, VrHandCount>& hands, float dt);

  private:

    struct HandTrack {
      bool     known      = false;
      float    height     = 0.0f;
      float    side       = 0.0f;
      int32_t  direction  = 0;
      uint32_t turns      = 0;
      float    sinceTurn  = 0.0f;
    };

    std::array<HandTrack, VrHandCount> m_hands;
    float m_cooldown = 0.0f;

  };

}
