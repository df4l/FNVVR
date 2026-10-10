#pragma once

namespace dxvk {

  /**
   * \brief How the turning stick turns the player
   *
   * Set by \c d3d9.vrSmoothTurn, \c d3d9.vrSnapTurnAngle and
   * \c d3d9.vrSmoothTurnSpeed in dxvk.conf. Snap turning is the default
   * because it is more comfortable for most people.
   */
  struct VrTurnConfig {
    /// Turn continuously instead of by steps
    bool  smooth      = false;
    /// Angle of one step, in degrees
    float snapAngle   = 30.0f;
    /// Speed of continuous turning at full deflection, in degrees per second
    float smoothSpeed = 120.0f;
  };

  /**
   * \brief Turns the horizontal deflection of the turning stick into turns
   *
   * A snap turn happens once when the stick is pushed past a threshold, and
   * the stick has to come back near the centre before the next one. Smooth
   * turning scales with the deflection beyond a dead zone.
   */
  class VrTurnInput {

  public:

    /// Deflection that triggers a snap turn
    constexpr static float SnapPress   = 0.7f;
    /// Deflection under which the next snap turn is allowed
    constexpr static float SnapRelease = 0.3f;
    /// Dead zone of smooth turning
    constexpr static float DeadZone    = 0.15f;

    explicit VrTurnInput(const VrTurnConfig& config);

    /**
     * \brief Samples the stick
     *
     * \param [in] stickX Horizontal deflection in [-1, 1], positive right
     * \param [in] dt Time since the previous sample, in seconds
     * \returns Change of heading in radians, positive clockwise (right)
     */
    float update(float stickX, float dt);

    /**
     * \brief Forgets the stick's state, for example after a menu
     *
     * A stick still held when turning resumes does not snap until it was
     * released.
     */
    void reset();

  private:

    VrTurnConfig m_config;
    bool         m_armed = false;

  };

}
