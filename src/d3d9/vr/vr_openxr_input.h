#pragma once

#include <array>
#include <memory>

#include <openxr/openxr.h>

#include "vr_types.h"

namespace dxvk {

  class VrOpenXrLoader;

  /**
   * \brief Controllers of the OpenXR backend
   *
   * Three action sets: \c game and \c menu hold the digital actions and
   * sticks of each VrInputContext (only the current one is synchronised),
   * \c hands the poses, the finger sensors and the vibration of both hands,
   * synchronised in every context. Bindings are suggested for the Touch
   * profile only; runtimes map other controllers to it themselves.
   *
   * Hand poses follow OpenVR's: the controller pose is OpenVR's raw pose,
   * placed from OpenXR's grip pose (vrOpenXrTouchRawPoseInGrip), and the aim
   * pose is OpenXR's.
   */
  class VrOpenXrInput {

  public:

    /**
     * \brief Creates the actions and suggests their bindings
     * \returns The input, or \c nullptr if the runtime refused the actions
     */
    static std::unique_ptr<VrOpenXrInput> create(const VrOpenXrLoader& loader, XrInstance instance);

    ~VrOpenXrInput();

    VrOpenXrInput(const VrOpenXrInput&) = delete;
    VrOpenXrInput& operator = (const VrOpenXrInput&) = delete;

    /**
     * \brief Attaches the action sets to a new session
     *
     * Must be called once per session, before its first frame.
     */
    bool attach(XrSession session);

    /**
     * \brief Destroys what belongs to the session, before it is destroyed
     */
    void detach();

    /**
     * \brief Reads the controllers for a display time
     *
     * \param [in] context Which set of actions to read
     * \param [in] space Space the poses are given in
     * \param [in] displayTime Time the poses are predicted for
     * \param [in,out] input State the controllers are written into
     */
    void read(VrInputContext context, XrSpace space, XrTime displayTime, VrInputState& input);

    /**
     * \brief Vibrates a controller
     */
    void vibrate(VrHand hand, float amplitude, int64_t durationNs);

    /**
     * \brief Logs the controllers the runtime bound, after it said they changed
     */
    void logInteractionProfiles();

  private:

    struct Functions;

    struct Context {
      XrActionSet set  = XR_NULL_HANDLE;
      XrAction    move = XR_NULL_HANDLE;
      XrAction    turn = XR_NULL_HANDLE;
    };

    struct Hand {
      XrPath  path       = XR_NULL_PATH;
      XrSpace gripSpace  = XR_NULL_HANDLE;
      XrSpace aimSpace   = XR_NULL_HANDLE;
      bool    loggedPose = false;
    };

    VrOpenXrInput(const VrOpenXrLoader& loader, XrInstance instance);

    bool createActions();

    bool suggestBindings();

    XrActionSet createActionSet(const char* name, const char* label);

    XrAction createAction(XrActionSet set, XrActionType type, const char* name, const char* label, bool perHand);

    XrPath path(const char* string) const;

    XrSpace createHandSpace(XrAction action, VrHand hand, const VrPose& offset);

    bool readBoolean(XrAction action, XrPath hand) const;

    float readFloat(XrAction action, XrPath hand) const;

    VrVector2 readStick(XrAction action) const;

    bool locate(XrSpace handSpace, XrSpace space, XrTime time, VrPose& pose) const;

    bool check(XrResult result, const char* what) const;

    std::unique_ptr<Functions>                   m_xr;
    XrInstance                                   m_instance = XR_NULL_HANDLE;
    XrSession                                    m_session  = XR_NULL_HANDLE;

    std::array<Context, 2>                       m_contexts;
    std::array<XrAction, VrActionCount>          m_digital  = { };

    XrActionSet                                  m_handsSet      = XR_NULL_HANDLE;
    XrAction                                     m_gripPose      = XR_NULL_HANDLE;
    XrAction                                     m_aimPose       = XR_NULL_HANDLE;
    XrAction                                     m_haptic        = XR_NULL_HANDLE;
    XrAction                                     m_trigger       = XR_NULL_HANDLE;
    XrAction                                     m_triggerTouch  = XR_NULL_HANDLE;
    XrAction                                     m_squeeze       = XR_NULL_HANDLE;
    XrAction                                     m_thumbTouch    = XR_NULL_HANDLE;
    std::array<Hand, VrHandCount>                m_hands;

    bool                                         m_loggedSync    = false;

  };

}
