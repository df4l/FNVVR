#pragma once

#include <cstdint>
#include <vector>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Digital action of the OpenXR backend and its Touch binding
   *
   * The actions mirror the OpenVR action manifest (fnvvr\actions.json): same
   * names, same sets, and the bindings of bindings_oculus_touch.json.
   */
  struct VrXrDigitalAction {
    VrAction    action;
    /// Action name inside its set, as in the OpenVR manifest
    const char* name;
    /// Name shown by the runtime
    const char* label;
    /// Path on the Touch controller, or \c nullptr when unbound
    const char* touchPath;
  };

  /**
   * \brief Stick action of a context and its Touch binding
   */
  struct VrXrStickAction {
    const char* name;
    const char* label;
    const char* touchPath;
  };

  /**
   * \brief Interaction profile the bindings are suggested for
   */
  constexpr const char* VrXrTouchProfile = "/interaction_profiles/oculus/touch_controller";

  /**
   * \brief Name of the action set of a context ("game" or "menu")
   */
  const char* vrOpenXrActionSetName(VrInputContext context);

  /**
   * \brief The digital actions, one per VrAction, in the enum's order
   */
  const std::vector<VrXrDigitalAction>& vrOpenXrDigitalActions();

  /**
   * \brief Context in which an action is read
   */
  VrInputContext vrOpenXrActionContext(VrAction action);

  /**
   * \brief Stick action of a context
   *
   * \param [in] turn The turning (Game) or secondary (Menu) stick, else the
   *    moving or navigating one
   */
  VrXrStickAction vrOpenXrStickAction(VrInputContext context, bool turn);

  /**
   * \brief Paths on a Touch controller the thumb rests on
   *
   * A touch on any of them means the thumb is down.
   */
  std::vector<const char*> vrOpenXrTouchThumbPaths(VrHand hand);

  /**
   * \brief OpenVR's raw controller pose, relative to OpenXR's grip pose
   *
   * The game's hands and weapons were placed against the pose OpenVR gives
   * for a Touch controller, which sits 10 cm ahead of OpenXR's grip and is
   * tilted 20.6 degrees. Offsets as measured by xrizer's Touch profile
   * (findings/openxr-backend.md §6).
   */
  VrPose vrOpenXrTouchRawPoseInGrip(VrHand hand);

  /**
   * \brief Finger state read from a Touch controller's sensors
   */
  struct VrXrTouchFingers {
    float trigger        = 0.0f;
    bool  triggerTouched = false;
    float squeeze        = 0.0f;
    bool  thumbTouched   = false;
  };

  /**
   * \brief How far each finger is curled, from the Touch sensors
   *
   * OpenXR has no hand skeleton for controllers. As SteamVR's hand animation
   * does, the index follows the trigger and rests on it while touching it,
   * the other three fingers follow the grip and the thumb is down while it
   * touches a button, the stick or the thumb rest.
   */
  VrFingerCurls vrOpenXrTouchFingerCurls(const VrXrTouchFingers& fingers);

}
