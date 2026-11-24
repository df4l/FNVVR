#include <algorithm>
#include <cstring>
#include <vector>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_openxr_actions.h"
#include "vr_openxr_convert.h"
#include "vr_openxr_input.h"
#include "vr_openxr_loader.h"

namespace dxvk {

  namespace {

    const char* handName(VrHand hand) {
      return hand == VrHand::Left ? "left" : "right";
    }

    const char* handPath(VrHand hand) {
      return hand == VrHand::Left ? "/user/hand/left" : "/user/hand/right";
    }

  }


  struct VrOpenXrInput::Functions {
    PFN_xrResultToString                    resultToString                 = nullptr;
    PFN_xrStringToPath                      stringToPath                   = nullptr;
    PFN_xrPathToString                      pathToString                   = nullptr;
    PFN_xrCreateActionSet                   createActionSet                = nullptr;
    PFN_xrDestroyActionSet                  destroyActionSet               = nullptr;
    PFN_xrCreateAction                      createAction                   = nullptr;
    PFN_xrSuggestInteractionProfileBindings suggestBindings                = nullptr;
    PFN_xrAttachSessionActionSets           attachSessionActionSets        = nullptr;
    PFN_xrCreateActionSpace                 createActionSpace              = nullptr;
    PFN_xrDestroySpace                      destroySpace                   = nullptr;
    PFN_xrLocateSpace                       locateSpace                    = nullptr;
    PFN_xrSyncActions                       syncActions                    = nullptr;
    PFN_xrGetActionStateBoolean             getActionStateBoolean          = nullptr;
    PFN_xrGetActionStateFloat               getActionStateFloat            = nullptr;
    PFN_xrGetActionStateVector2f            getActionStateVector2f         = nullptr;
    PFN_xrGetActionStatePose                getActionStatePose             = nullptr;
    PFN_xrApplyHapticFeedback               applyHapticFeedback            = nullptr;
    PFN_xrGetCurrentInteractionProfile      getCurrentInteractionProfile   = nullptr;

    bool load(const VrOpenXrLoader& loader, XrInstance instance) {
      bool complete = true;

      auto load = [&] (auto& target, const char* name) {
        target = loader.get<std::remove_reference_t<decltype(target)>>(instance, name);

        if (!target) {
          Logger::err(str::format("VR: OpenXR function ", name, " is not available"));
          complete = false;
        }
      };

      load(resultToString, "xrResultToString");
      load(stringToPath, "xrStringToPath");
      load(pathToString, "xrPathToString");
      load(createActionSet, "xrCreateActionSet");
      load(destroyActionSet, "xrDestroyActionSet");
      load(createAction, "xrCreateAction");
      load(suggestBindings, "xrSuggestInteractionProfileBindings");
      load(attachSessionActionSets, "xrAttachSessionActionSets");
      load(createActionSpace, "xrCreateActionSpace");
      load(destroySpace, "xrDestroySpace");
      load(locateSpace, "xrLocateSpace");
      load(syncActions, "xrSyncActions");
      load(getActionStateBoolean, "xrGetActionStateBoolean");
      load(getActionStateFloat, "xrGetActionStateFloat");
      load(getActionStateVector2f, "xrGetActionStateVector2f");
      load(getActionStatePose, "xrGetActionStatePose");
      load(applyHapticFeedback, "xrApplyHapticFeedback");
      load(getCurrentInteractionProfile, "xrGetCurrentInteractionProfile");
      return complete;
    }
  };


  std::unique_ptr<VrOpenXrInput> VrOpenXrInput::create(const VrOpenXrLoader& loader, XrInstance instance, bool eyeGaze) {
    std::unique_ptr<VrOpenXrInput> input(new VrOpenXrInput(loader, instance, eyeGaze));

    if (!input->m_xr || !input->createActions() || !input->suggestBindings()) {
      Logger::err("VR: The OpenXR controller actions could not be set up, the controllers are not read");
      return nullptr;
    }

    // The controllers work without it
    if (input->m_eyeGaze && !input->suggestGazeBinding()) {
      Logger::warn("VR: The OpenXR eye gaze could not be bound, foveated rendering has no eye tracker");
      input->m_eyeGaze = false;
    }

    return input;
  }


  VrOpenXrInput::VrOpenXrInput(const VrOpenXrLoader& loader, XrInstance instance, bool eyeGaze)
  : m_xr(std::make_unique<Functions>()), m_instance(instance), m_eyeGaze(eyeGaze) {
    if (!m_xr->load(loader, instance))
      m_xr = nullptr;
  }


  VrOpenXrInput::~VrOpenXrInput() {
    detach();

    if (!m_xr)
      return;

    // Destroying a set destroys its actions
    for (const Context& context : m_contexts) {
      if (context.set != XR_NULL_HANDLE)
        m_xr->destroyActionSet(context.set);
    }

    if (m_handsSet != XR_NULL_HANDLE)
      m_xr->destroyActionSet(m_handsSet);
  }


  bool VrOpenXrInput::check(XrResult result, const char* what) const {
    if (XR_SUCCEEDED(result))
      return true;

    char buffer[XR_MAX_RESULT_STRING_SIZE] = { };

    if (XR_FAILED(m_xr->resultToString(m_instance, result, buffer)))
      std::strncpy(buffer, str::format(int32_t(result)).c_str(), sizeof(buffer) - 1);

    Logger::err(str::format("VR: OpenXR ", what, " failed: ", buffer));
    return false;
  }


  XrPath VrOpenXrInput::path(const char* string) const {
    XrPath result = XR_NULL_PATH;

    if (!check(m_xr->stringToPath(m_instance, string, &result), string))
      return XR_NULL_PATH;

    return result;
  }


  XrActionSet VrOpenXrInput::createActionSet(const char* name, const char* label) {
    XrActionSetCreateInfo info = { XR_TYPE_ACTION_SET_CREATE_INFO };
    std::strncpy(info.actionSetName, name, XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::strncpy(info.localizedActionSetName, label, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);

    XrActionSet set = XR_NULL_HANDLE;

    if (!check(m_xr->createActionSet(m_instance, &info, &set), "xrCreateActionSet"))
      return XR_NULL_HANDLE;

    return set;
  }


  XrAction VrOpenXrInput::createAction(XrActionSet set, XrActionType type, const char* name, const char* label, bool perHand) {
    std::array<XrPath, VrHandCount> hands = { m_hands[0].path, m_hands[1].path };

    XrActionCreateInfo info = { XR_TYPE_ACTION_CREATE_INFO };
    std::strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    std::strncpy(info.localizedActionName, label, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    info.actionType = type;

    if (perHand) {
      info.countSubactionPaths = VrHandCount;
      info.subactionPaths      = hands.data();
    }

    XrAction action = XR_NULL_HANDLE;

    if (!check(m_xr->createAction(set, &info, &action), str::format("xrCreateAction (", name, ")").c_str()))
      return XR_NULL_HANDLE;

    return action;
  }


  bool VrOpenXrInput::createActions() {
    for (uint32_t i = 0; i < VrHandCount; i++) {
      m_hands[i].path = path(handPath(VrHand(i)));

      if (m_hands[i].path == XR_NULL_PATH)
        return false;
    }

    for (uint32_t c = 0; c < m_contexts.size(); c++) {
      VrInputContext context = VrInputContext(c);
      Context& actions = m_contexts[c];

      actions.set = createActionSet(vrOpenXrActionSetName(context), context == VrInputContext::Menu ? "Menus" : "Game");

      if (actions.set == XR_NULL_HANDLE)
        return false;

      VrXrStickAction move = vrOpenXrStickAction(context, false);
      VrXrStickAction turn = vrOpenXrStickAction(context, true);
      actions.move = createAction(actions.set, XR_ACTION_TYPE_VECTOR2F_INPUT, move.name, move.label, false);
      actions.turn = createAction(actions.set, XR_ACTION_TYPE_VECTOR2F_INPUT, turn.name, turn.label, false);

      if (actions.move == XR_NULL_HANDLE || actions.turn == XR_NULL_HANDLE)
        return false;
    }

    for (const VrXrDigitalAction& action : vrOpenXrDigitalActions()) {
      XrActionSet set = m_contexts[uint32_t(vrOpenXrActionContext(action.action))].set;
      XrAction& handle = m_digital[uint32_t(action.action)];
      handle = createAction(set, XR_ACTION_TYPE_BOOLEAN_INPUT, action.name, action.label, false);

      if (handle == XR_NULL_HANDLE)
        return false;
    }

    m_handsSet = createActionSet("hands", "Hands");

    if (m_handsSet == XR_NULL_HANDLE)
      return false;

    m_gripPose     = createAction(m_handsSet, XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Hand", true);
    m_aimPose      = createAction(m_handsSet, XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Hand pointer", true);
    m_haptic       = createAction(m_handsSet, XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Vibration", true);
    m_trigger      = createAction(m_handsSet, XR_ACTION_TYPE_FLOAT_INPUT, "index_curl", "Index finger", true);
    m_triggerTouch = createAction(m_handsSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "index_touch", "Index finger on the trigger", true);
    m_squeeze      = createAction(m_handsSet, XR_ACTION_TYPE_FLOAT_INPUT, "grip_curl", "Other fingers", true);
    m_thumbTouch   = createAction(m_handsSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumb_touch", "Thumb down", true);

    if (m_eyeGaze) {
      m_gazePose = createAction(m_handsSet, XR_ACTION_TYPE_POSE_INPUT, "eye_gaze", "Eye gaze", false);
      m_eyeGaze  = m_gazePose != XR_NULL_HANDLE;
    }

    return m_gripPose != XR_NULL_HANDLE && m_aimPose != XR_NULL_HANDLE && m_haptic != XR_NULL_HANDLE
        && m_trigger != XR_NULL_HANDLE && m_triggerTouch != XR_NULL_HANDLE
        && m_squeeze != XR_NULL_HANDLE && m_thumbTouch != XR_NULL_HANDLE;
  }


  bool VrOpenXrInput::suggestBindings() {
    std::vector<XrActionSuggestedBinding> bindings;

    auto bind = [this, &bindings] (XrAction action, const char* string) {
      XrPath binding = path(string);

      if (binding != XR_NULL_PATH)
        bindings.push_back({ action, binding });
    };

    for (uint32_t c = 0; c < m_contexts.size(); c++) {
      bind(m_contexts[c].move, vrOpenXrStickAction(VrInputContext(c), false).touchPath);
      bind(m_contexts[c].turn, vrOpenXrStickAction(VrInputContext(c), true).touchPath);
    }

    for (const VrXrDigitalAction& action : vrOpenXrDigitalActions()) {
      if (action.touchPath)
        bind(m_digital[uint32_t(action.action)], action.touchPath);
    }

    for (uint32_t i = 0; i < VrHandCount; i++) {
      std::string hand = handPath(VrHand(i));
      bind(m_gripPose,     (hand + "/input/grip/pose").c_str());
      bind(m_aimPose,      (hand + "/input/aim/pose").c_str());
      bind(m_haptic,       (hand + "/output/haptic").c_str());
      bind(m_trigger,      (hand + "/input/trigger/value").c_str());
      bind(m_triggerTouch, (hand + "/input/trigger/touch").c_str());
      bind(m_squeeze,      (hand + "/input/squeeze/value").c_str());

      for (const char* thumb : vrOpenXrTouchThumbPaths(VrHand(i)))
        bind(m_thumbTouch, thumb);
    }

    XrInteractionProfileSuggestedBinding suggested = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    suggested.interactionProfile     = path(VrXrTouchProfile);
    suggested.countSuggestedBindings = uint32_t(bindings.size());
    suggested.suggestedBindings      = bindings.data();

    if (suggested.interactionProfile == XR_NULL_PATH
     || !check(m_xr->suggestBindings(m_instance, &suggested), "xrSuggestInteractionProfileBindings (Touch)"))
      return false;

    Logger::info(str::format("VR: OpenXR bindings suggested for the Touch controllers, ", bindings.size(), " inputs"));
    return true;
  }


  bool VrOpenXrInput::suggestGazeBinding() {
    XrActionSuggestedBinding binding = { m_gazePose, path("/user/eyes_ext/input/gaze_ext/pose") };

    XrInteractionProfileSuggestedBinding suggested = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    suggested.interactionProfile     = path("/interaction_profiles/ext/eye_gaze_interaction");
    suggested.countSuggestedBindings = 1;
    suggested.suggestedBindings      = &binding;

    if (binding.binding == XR_NULL_PATH || suggested.interactionProfile == XR_NULL_PATH)
      return false;

    if (!check(m_xr->suggestBindings(m_instance, &suggested), "xrSuggestInteractionProfileBindings (eye gaze)"))
      return false;

    Logger::info("VR: OpenXR binding suggested for the eye gaze");
    return true;
  }


  bool VrOpenXrInput::attach(XrSession session) {
    detach();

    std::array<XrActionSet, 3> sets = { m_contexts[0].set, m_contexts[1].set, m_handsSet };

    XrSessionActionSetsAttachInfo info = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    info.countActionSets = uint32_t(sets.size());
    info.actionSets      = sets.data();

    if (!check(m_xr->attachSessionActionSets(session, &info), "xrAttachSessionActionSets"))
      return false;

    m_session = session;

    for (uint32_t i = 0; i < VrHandCount; i++) {
      m_hands[i].gripSpace = createHandSpace(m_gripPose, VrHand(i), vrOpenXrTouchRawPoseInGrip(VrHand(i)));
      m_hands[i].aimSpace  = createHandSpace(m_aimPose, VrHand(i), VrPose());
    }

    if (m_eyeGaze) {
      XrActionSpaceCreateInfo info = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
      info.action            = m_gazePose;
      info.poseInActionSpace = vrPoseToXr(VrPose());

      if (!check(m_xr->createActionSpace(m_session, &info, &m_gazeSpace), "xrCreateActionSpace (eye gaze)"))
        m_gazeSpace = XR_NULL_HANDLE;
    }

    return true;
  }


  void VrOpenXrInput::detach() {
    if (!m_xr)
      return;

    for (Hand& hand : m_hands) {
      if (hand.gripSpace != XR_NULL_HANDLE)
        m_xr->destroySpace(hand.gripSpace);

      if (hand.aimSpace != XR_NULL_HANDLE)
        m_xr->destroySpace(hand.aimSpace);

      hand.gripSpace = XR_NULL_HANDLE;
      hand.aimSpace  = XR_NULL_HANDLE;
    }

    if (m_gazeSpace != XR_NULL_HANDLE)
      m_xr->destroySpace(m_gazeSpace);

    m_gazeSpace = XR_NULL_HANDLE;
    m_session   = XR_NULL_HANDLE;
  }


  XrSpace VrOpenXrInput::createHandSpace(XrAction action, VrHand hand, const VrPose& offset) {
    XrActionSpaceCreateInfo info = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
    info.action            = action;
    info.subactionPath     = m_hands[uint32_t(hand)].path;
    info.poseInActionSpace = vrPoseToXr(offset);

    XrSpace space = XR_NULL_HANDLE;

    if (!check(m_xr->createActionSpace(m_session, &info, &space), "xrCreateActionSpace"))
      return XR_NULL_HANDLE;

    return space;
  }


  bool VrOpenXrInput::readBoolean(XrAction action, XrPath hand) const {
    XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
    info.action        = action;
    info.subactionPath = hand;

    XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };
    return XR_SUCCEEDED(m_xr->getActionStateBoolean(m_session, &info, &state))
      && state.isActive && state.currentState;
  }


  float VrOpenXrInput::readFloat(XrAction action, XrPath hand) const {
    XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
    info.action        = action;
    info.subactionPath = hand;

    XrActionStateFloat state = { XR_TYPE_ACTION_STATE_FLOAT };

    if (XR_FAILED(m_xr->getActionStateFloat(m_session, &info, &state)) || !state.isActive)
      return 0.0f;

    return state.currentState;
  }


  VrVector2 VrOpenXrInput::readStick(XrAction action) const {
    XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
    info.action = action;

    XrActionStateVector2f state = { XR_TYPE_ACTION_STATE_VECTOR2F };

    if (XR_FAILED(m_xr->getActionStateVector2f(m_session, &info, &state)) || !state.isActive)
      return VrVector2();

    return { state.currentState.x, state.currentState.y };
  }


  bool VrOpenXrInput::locate(XrSpace handSpace, XrSpace space, XrTime time, VrPose& pose) const {
    if (handSpace == XR_NULL_HANDLE)
      return false;

    XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };

    if (XR_FAILED(m_xr->locateSpace(handSpace, space, time, &location)))
      return false;

    const XrSpaceLocationFlags valid = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;

    if ((location.locationFlags & valid) != valid)
      return false;

    pose = vrPoseFromXr(location.pose);
    return true;
  }


  void VrOpenXrInput::read(VrInputContext context, XrSpace space, XrTime displayTime, VrInputState& input) {
    if (m_session == XR_NULL_HANDLE)
      return;

    const Context& actions = m_contexts[uint32_t(context)];

    std::array<XrActiveActionSet, 2> active = { };
    active[0].actionSet = actions.set;
    active[1].actionSet = m_handsSet;

    XrActionsSyncInfo sync = { XR_TYPE_ACTIONS_SYNC_INFO };
    sync.countActiveActionSets = uint32_t(active.size());
    sync.activeActionSets      = active.data();

    // XR_SESSION_NOT_FOCUSED: the runtime's own interface has the controllers,
    // every action then reads as inactive
    XrResult result = m_xr->syncActions(m_session, &sync);

    if (XR_FAILED(result)) {
      if (!m_loggedSync) {
        check(result, "xrSyncActions");
        m_loggedSync = true;
      }

      return;
    }

    for (uint32_t i = 0; i < VrHandCount; i++) {
      Hand& hand = m_hands[i];
      VrControllerState& controller = input.controllers[i];

      controller.isActive = locate(hand.gripSpace, space, displayTime, controller.gripPose);

      if (!controller.isActive)
        continue;

      // Without an aim pose the controller's own pose points
      if (!locate(hand.aimSpace, space, displayTime, controller.aimPose))
        controller.aimPose = controller.gripPose;

      VrXrTouchFingers fingers;
      fingers.trigger        = readFloat(m_trigger, hand.path);
      fingers.triggerTouched = readBoolean(m_triggerTouch, hand.path);
      fingers.squeeze        = readFloat(m_squeeze, hand.path);
      fingers.thumbTouched   = readBoolean(m_thumbTouch, hand.path);

      controller.hasFingers = true;
      controller.fingerCurl = vrOpenXrTouchFingerCurls(fingers);

      if (!hand.loggedPose) {
        Logger::info(str::format("VR: The OpenXR ", handName(VrHand(i)), " controller is tracked"));
        hand.loggedPose = true;
      }
    }

    // Actions only mean something while a controller drives them
    input.hasActions = input.controllers[0].isActive || input.controllers[1].isActive;

    for (const VrXrDigitalAction& action : vrOpenXrDigitalActions()) {
      if (vrOpenXrActionContext(action.action) == context)
        input.actions.setPressed(action.action, readBoolean(m_digital[uint32_t(action.action)], XR_NULL_PATH));
    }

    input.actions.move = readStick(actions.move);
    input.actions.turn = readStick(actions.turn);
  }


  bool VrOpenXrInput::readGaze(XrSpace space, XrTime displayTime, VrPose& gaze) const {
    if (m_gazeSpace == XR_NULL_HANDLE)
      return false;

    XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
    info.action = m_gazePose;

    XrActionStatePose state = { XR_TYPE_ACTION_STATE_POSE };

    if (XR_FAILED(m_xr->getActionStatePose(m_session, &info, &state)) || !state.isActive)
      return false;

    // Only the orientation is needed: the gaze is taken from both eyes
    XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };

    if (XR_FAILED(m_xr->locateSpace(m_gazeSpace, space, displayTime, &location))
     || !(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT))
      return false;

    gaze = vrPoseFromXr(location.pose);
    return true;
  }


  void VrOpenXrInput::vibrate(VrHand hand, float amplitude, int64_t durationNs) {
    if (m_session == XR_NULL_HANDLE || amplitude <= 0.0f || durationNs <= 0)
      return;

    XrHapticActionInfo info = { XR_TYPE_HAPTIC_ACTION_INFO };
    info.action        = m_haptic;
    info.subactionPath = m_hands[uint32_t(hand)].path;

    XrHapticVibration vibration = { XR_TYPE_HAPTIC_VIBRATION };
    vibration.duration  = XrDuration(durationNs);
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
    vibration.amplitude = std::min(amplitude, 1.0f);

    m_xr->applyHapticFeedback(m_session, &info, reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
  }


  void VrOpenXrInput::logInteractionProfiles() {
    if (m_session == XR_NULL_HANDLE)
      return;

    for (uint32_t i = 0; i < VrHandCount; i++) {
      XrInteractionProfileState state = { XR_TYPE_INTERACTION_PROFILE_STATE };

      if (XR_FAILED(m_xr->getCurrentInteractionProfile(m_session, m_hands[i].path, &state)))
        continue;

      char name[XR_MAX_PATH_LENGTH] = { };
      uint32_t length = 0;

      if (state.interactionProfile == XR_NULL_PATH)
        std::strncpy(name, "none", sizeof(name) - 1);
      else if (XR_FAILED(m_xr->pathToString(m_instance, state.interactionProfile, sizeof(name), &length, name)))
        std::strncpy(name, "unknown", sizeof(name) - 1);

      Logger::info(str::format("VR: OpenXR ", handName(VrHand(i)), " controller bound as ", name));
    }
  }

}
