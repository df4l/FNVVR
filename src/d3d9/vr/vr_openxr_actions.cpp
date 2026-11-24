#include <algorithm>

#include "vr_math.h"
#include "vr_openxr_actions.h"

namespace dxvk {

  namespace {

    // Curl of the index while it rests on the trigger without pulling it,
    // and of the thumb while it rests on a button
    constexpr float IndexRestCurl = 0.3f;
    constexpr float ThumbDownCurl = 0.8f;

    float clamp01(float v) {
      return std::min(std::max(v, 0.0f), 1.0f);
    }

  }


  const char* vrOpenXrActionSetName(VrInputContext context) {
    return context == VrInputContext::Menu ? "menu" : "game";
  }


  const std::vector<VrXrDigitalAction>& vrOpenXrDigitalActions() {
    static const std::vector<VrXrDigitalAction> actions = {
      { VrAction::Attack,           "attack",        "Attack",                          "/user/hand/right/input/trigger/value"    },
      { VrAction::TwoHandGrip,      "two_hand_grip", "Hold the weapon with both hands", "/user/hand/left/input/squeeze/value"     },
      { VrAction::Activate,         "activate",      "Activate",                        "/user/hand/right/input/a/click"          },
      { VrAction::Jump,             "jump",          "Jump",                            "/user/hand/right/input/b/click"          },
      { VrAction::Reload,           "reload",        "Reload",                          "/user/hand/left/input/x/click"           },
      { VrAction::Sneak,            "sneak",         "Sneak",                           "/user/hand/left/input/thumbstick/click"  },
      { VrAction::PauseHold,        "pause_hold",    "Pause menu (hold), wait (tap)",   "/user/hand/left/input/y/click"           },
      { VrAction::Vats,             "vats",          "V.A.T.S.",                        "/user/hand/right/input/thumbstick/click" },
      // The left menu button reaches the application through OpenXR, unlike
      // through Steam Link where SteamVR's dashboard takes it
      { VrAction::Pause,            "pause",         "Pause menu",                      "/user/hand/left/input/menu/click"        },
      { VrAction::Grab,             "grab",          "Grab",                            "/user/hand/right/input/squeeze/value"    },
      { VrAction::Wait,             "wait",          "Wait",                            nullptr                                   },
      { VrAction::Magazine,         "magazine",      "Take out, take and insert a magazine (left hand)", "/user/hand/left/input/trigger/value" },
      { VrAction::MenuSelect,       "select",        "Select",                          "/user/hand/right/input/a/click"          },
      { VrAction::MenuBack,         "back",          "Back",                            "/user/hand/right/input/b/click"          },
      { VrAction::MenuAlternate,    "alternate",     "Alternate action",                "/user/hand/left/input/x/click"           },
      { VrAction::MenuOption,       "option",        "Option",                          "/user/hand/left/input/y/click"           },
      { VrAction::MenuPrevious,     "previous",      "Previous tab",                    "/user/hand/left/input/squeeze/value"     },
      { VrAction::MenuNext,         "next",          "Next tab",                        "/user/hand/right/input/squeeze/value"    },
      { VrAction::MenuLeftTrigger,  "left_trigger",  "Left trigger (pointer: click)",   "/user/hand/left/input/trigger/value"     },
      { VrAction::MenuRightTrigger, "right_trigger", "Right trigger (pointer: click)",  "/user/hand/right/input/trigger/value"    },
    };

    return actions;
  }


  VrInputContext vrOpenXrActionContext(VrAction action) {
    return uint32_t(action) >= uint32_t(VrAction::MenuSelect)
      ? VrInputContext::Menu
      : VrInputContext::Game;
  }


  VrXrStickAction vrOpenXrStickAction(VrInputContext context, bool turn) {
    if (context == VrInputContext::Menu) {
      return turn
        ? VrXrStickAction { "secondary", "Secondary stick", "/user/hand/right/input/thumbstick" }
        : VrXrStickAction { "navigate",  "Navigate",        "/user/hand/left/input/thumbstick"  };
    }

    return turn
      ? VrXrStickAction { "turn", "Turn", "/user/hand/right/input/thumbstick" }
      : VrXrStickAction { "move", "Move", "/user/hand/left/input/thumbstick"  };
  }


  std::vector<const char*> vrOpenXrTouchThumbPaths(VrHand hand) {
    if (hand == VrHand::Left) {
      return { "/user/hand/left/input/thumbstick/touch", "/user/hand/left/input/thumbrest/touch",
               "/user/hand/left/input/x/touch", "/user/hand/left/input/y/touch" };
    }

    return { "/user/hand/right/input/thumbstick/touch", "/user/hand/right/input/thumbrest/touch",
             "/user/hand/right/input/a/touch", "/user/hand/right/input/b/touch" };
  }


  VrPose vrOpenXrTouchRawPoseInGrip(VrHand hand) {
    // The grip pose relative to the raw pose: tilted about +X, ahead along +Z
    // and, mirrored per hand, 7 mm sideways
    constexpr float TiltRadians = 20.6f * 3.14159265f / 180.0f;

    VrPose gripInRaw;
    gripInRaw.orientation = vrQuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, TiltRadians);
    gripInRaw.position    = { hand == VrHand::Left ? 0.007f : -0.007f, -0.00182941f, 0.1019482f };
    return vrInverse(gripInRaw);
  }


  VrFingerCurls vrOpenXrTouchFingerCurls(const VrXrTouchFingers& fingers) {
    float trigger = clamp01(fingers.trigger);
    float squeeze = clamp01(fingers.squeeze);

    VrFingerCurls curls = { };
    curls[uint32_t(VrFinger::Thumb)]  = fingers.thumbTouched ? ThumbDownCurl : 0.0f;
    curls[uint32_t(VrFinger::Index)]  = fingers.triggerTouched || trigger > 0.0f
      ? IndexRestCurl + (1.0f - IndexRestCurl) * trigger
      : 0.0f;
    curls[uint32_t(VrFinger::Middle)] = squeeze;
    curls[uint32_t(VrFinger::Ring)]   = squeeze;
    curls[uint32_t(VrFinger::Little)] = squeeze;
    return curls;
  }

}
