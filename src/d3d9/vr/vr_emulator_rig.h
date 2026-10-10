#pragma once

#include "vr_math.h"
#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Input sampled from the keyboard and mouse for the emulator
   *
   * Movement axes are in [-1, 1]. Mouse deltas are in pixels since the
   * previous sample. Reading the actual devices is left to the caller,
   * so that the mapping below stays free of platform code.
   */
  struct VrEmulatorInput {
    float moveRight    = 0.0f;
    float moveUp       = 0.0f;
    float moveForward  = 0.0f;
    float mouseDeltaX  = 0.0f;
    float mouseDeltaY  = 0.0f;
    bool  leftTrigger  = false;
    bool  rightTrigger = false;
    bool  recenter     = false;
  };

  /**
   * \brief Tuning values for the emulated rig
   */
  struct VrEmulatorRigConfig {
    float standingHeight   = 1.7f;
    float moveSpeed        = 1.5f;
    float radiansPerPixel  = 0.0025f;
    float maxPitch         = 1.5f;
    VrVector3 leftHandOffset  = { -0.25f, -0.35f, -0.4f };
    VrVector3 rightHandOffset = {  0.25f, -0.35f, -0.4f };
  };

  /**
   * \brief Simulated head and hands driven by VrEmulatorInput
   *
   * The head moves in the horizontal plane relative to its yaw. The hands
   * are held at a fixed offset from the head and follow its yaw only,
   * which gives stable controllers for testing without a pointing device.
   */
  class VrEmulatorRig {

  public:

    explicit VrEmulatorRig(const VrEmulatorRigConfig& config = VrEmulatorRigConfig());

    /**
     * \brief Advances the simulation
     * \param [in] input Input sampled for this step
     * \param [in] dt Elapsed time in seconds
     */
    void update(const VrEmulatorInput& input, float dt);

    /**
     * \brief Overrides the head pose, e.g. from a scripted path
     *
     * Yaw and pitch are derived from the pose so that interactive input
     * continues from it.
     */
    void setHeadPose(const VrPose& pose);

    VrPose headPose() const;

    VrControllerState controllerState(VrHand hand) const;

  private:

    VrEmulatorRigConfig m_config;

    VrVector3 m_position = { };
    float     m_yaw   = 0.0f;
    float     m_pitch = 0.0f;

    VrPose handPose(VrHand hand) const;

  };

}
