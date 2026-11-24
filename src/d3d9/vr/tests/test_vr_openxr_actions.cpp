#include <cmath>
#include <cstring>
#include <set>
#include <string>

#include "test.h"

#include "../vr_math.h"
#include "../vr_openvr_convert.h"
#include "../vr_openxr_actions.h"

using namespace dxvk;

namespace {

  // OpenXR names: lower case letters, digits, '-', '_' and '.'
  bool isValidXrName(const char* name) {
    if (!name || !*name)
      return false;

    for (const char* c = name; *c; c++) {
      bool valid = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' || *c == '.';

      if (!valid)
        return false;
    }

    return true;
  }

  // Inputs of /interaction_profiles/oculus/touch_controller (OpenXR 1.0)
  bool isTouchPath(const std::string& path) {
    static const std::set<std::string> paths = {
      "/user/hand/left/input/x/click", "/user/hand/left/input/x/touch",
      "/user/hand/left/input/y/click", "/user/hand/left/input/y/touch",
      "/user/hand/left/input/menu/click",
      "/user/hand/right/input/a/click", "/user/hand/right/input/a/touch",
      "/user/hand/right/input/b/click", "/user/hand/right/input/b/touch",
    };

    if (paths.count(path))
      return true;

    for (const char* hand : { "/user/hand/left/input/", "/user/hand/right/input/" }) {
      for (const char* input : { "squeeze/value", "trigger/value", "trigger/touch", "thumbstick",
                                 "thumbstick/click", "thumbstick/touch", "thumbrest/touch" }) {
        if (path == std::string(hand) + input)
          return true;
      }
    }

    return false;
  }

}


TEST_CASE(openxr_digital_actions_follow_the_enum) {
  const auto& actions = vrOpenXrDigitalActions();
  CHECK(actions.size() == VrActionCount);

  for (uint32_t i = 0; i < actions.size(); i++)
    CHECK(uint32_t(actions[i].action) == i);
}


TEST_CASE(openxr_action_names_match_the_openvr_manifest) {
  std::set<std::string> names[2];

  for (const VrXrDigitalAction& action : vrOpenXrDigitalActions()) {
    CHECK(isValidXrName(action.name));
    CHECK(action.label && *action.label);

    VrInputContext context = vrOpenXrActionContext(action.action);
    CHECK(context == vrOpenVrActionContext(action.action));

    std::string expected = std::string("/actions/") + vrOpenXrActionSetName(context) + "/in/" + action.name;
    CHECK(expected == vrOpenVrActionPath(action.action));

    CHECK(names[uint32_t(context)].insert(action.name).second);
  }

  for (uint32_t c = 0; c < 2; c++) {
    for (bool turn : { false, true }) {
      VrXrStickAction stick = vrOpenXrStickAction(VrInputContext(c), turn);
      CHECK(isValidXrName(stick.name));
      CHECK(names[c].insert(stick.name).second);

      std::string expected = std::string("/actions/") + vrOpenXrActionSetName(VrInputContext(c)) + "/in/" + stick.name;
      CHECK(expected == vrOpenVrStickPath(VrInputContext(c), turn));
    }
  }
}


TEST_CASE(openxr_touch_bindings_use_touch_inputs) {
  std::set<std::string> bound[2];

  for (const VrXrDigitalAction& action : vrOpenXrDigitalActions()) {
    if (!action.touchPath)
      continue;

    CHECK(isTouchPath(action.touchPath));

    // One input drives one action of a set
    CHECK(bound[uint32_t(vrOpenXrActionContext(action.action))].insert(action.touchPath).second);
  }

  for (uint32_t c = 0; c < 2; c++) {
    CHECK(isTouchPath(vrOpenXrStickAction(VrInputContext(c), false).touchPath));
    CHECK(isTouchPath(vrOpenXrStickAction(VrInputContext(c), true).touchPath));
  }

  for (VrHand hand : { VrHand::Left, VrHand::Right }) {
    for (const char* path : vrOpenXrTouchThumbPaths(hand)) {
      CHECK(isTouchPath(path));
      CHECK(std::strstr(path, hand == VrHand::Left ? "/left/" : "/right/") != nullptr);
    }
  }
}


TEST_CASE(openxr_attack_and_pointer_clicks_are_on_the_triggers) {
  const auto& actions = vrOpenXrDigitalActions();
  CHECK(std::string(actions[uint32_t(VrAction::Attack)].touchPath) == "/user/hand/right/input/trigger/value");
  CHECK(std::string(actions[uint32_t(VrAction::MenuRightTrigger)].touchPath) == "/user/hand/right/input/trigger/value");
  CHECK(std::string(actions[uint32_t(VrAction::MenuLeftTrigger)].touchPath) == "/user/hand/left/input/trigger/value");
  CHECK(std::string(actions[uint32_t(VrAction::PauseHold)].touchPath) == "/user/hand/left/input/y/click");
}


TEST_CASE(openxr_raw_pose_sits_ahead_of_the_grip) {
  for (VrHand hand : { VrHand::Left, VrHand::Right }) {
    VrPose raw = vrOpenXrTouchRawPoseInGrip(hand);

    // About 10 cm forward (-Z) and 3 cm down from the grip's centre
    CHECK_NEAR(raw.position.z, -0.0961, 1e-3);
    CHECK_NEAR(raw.position.y, -0.0342, 1e-3);
    CHECK_NEAR(raw.position.x, hand == VrHand::Left ? -0.007 : 0.007, 1e-4);

    // Back to the grip: tilted up 20.6 degrees about +X
    VrPose grip = vrInverse(raw);
    VrVector3 forward = vrRotate(grip.orientation, { 0.0f, 0.0f, -1.0f });
    CHECK_NEAR(forward.y, std::sin(20.6 * 3.14159265 / 180.0), 1e-4);
    CHECK_NEAR(grip.position.z, 0.1019482, 1e-5);
  }
}


TEST_CASE(openxr_finger_curls_follow_the_sensors) {
  VrXrTouchFingers open;
  VrFingerCurls curls = vrOpenXrTouchFingerCurls(open);

  for (float curl : curls)
    CHECK_NEAR(curl, 0.0, 1e-6);

  VrXrTouchFingers resting;
  resting.triggerTouched = true;
  resting.thumbTouched   = true;
  curls = vrOpenXrTouchFingerCurls(resting);
  CHECK(curls[uint32_t(VrFinger::Index)] > 0.0f && curls[uint32_t(VrFinger::Index)] < 0.5f);
  CHECK(curls[uint32_t(VrFinger::Thumb)] > 0.5f);
  CHECK_NEAR(curls[uint32_t(VrFinger::Middle)], 0.0, 1e-6);

  VrXrTouchFingers fist;
  fist.trigger = 1.0f;
  fist.squeeze = 1.5f;
  curls = vrOpenXrTouchFingerCurls(fist);
  CHECK_NEAR(curls[uint32_t(VrFinger::Index)], 1.0, 1e-6);
  CHECK_NEAR(curls[uint32_t(VrFinger::Little)], 1.0, 1e-6);
}
