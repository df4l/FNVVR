#include "test.h"

#include "../vr_emulator_backend.h"

using namespace dxvk;

namespace {

  constexpr float Pi = 3.14159265358979f;

  class CountingSink : public IVRFrameSink {

  public:

    void onFrame(const VrFrameSubmission& frame) override {
      count++;
      lastDisplayTime = frame.displayTime;
    }

    void onPanel(VrPanelId id, const VrPanelSubmission& panel) override {
      panelCount++;
      lastPanelId    = id;
      lastPanelWidth = panel.width;
    }

    int     count = 0;
    int64_t lastDisplayTime = 0;
    int     panelCount = 0;
    VrPanelId lastPanelId = VrPanelId::Menu;
    float   lastPanelWidth = 0.0f;

  };

  VrEmulatorBackend makeRunningBackend() {
    VrEmulatorBackend backend;
    backend.beginSession(VrGraphicsBinding());
    return backend;
  }

}


TEST_CASE(session_lifecycle) {
  VrEmulatorBackend backend;
  CHECK(backend.sessionState() == VrSessionState::Idle);
  CHECK(!backend.waitFrame().shouldRender);

  CHECK(backend.beginSession(VrGraphicsBinding()));
  CHECK(backend.sessionState() == VrSessionState::Running);
  CHECK(backend.waitFrame().shouldRender);

  backend.endSession();
  CHECK(backend.sessionState() == VrSessionState::Idle);
  CHECK(!backend.waitFrame().shouldRender);
}


TEST_CASE(frame_pacing_advances_one_period_per_frame) {
  VrEmulatorConfig config;
  config.refreshRate = 100;

  VrEmulatorBackend backend(config);
  backend.beginSession(VrGraphicsBinding());

  VrFrameTiming first  = backend.waitFrame();
  VrFrameTiming second = backend.waitFrame();

  CHECK(first.predictedPeriod == 10000000);
  CHECK(second.predictedDisplayTime - first.predictedDisplayTime == 10000000);
}


TEST_CASE(views_are_separated_by_the_ipd) {
  VrEmulatorConfig config;
  config.ipdMeters = 0.064f;

  VrEmulatorBackend backend(config);
  backend.beginSession(VrGraphicsBinding());

  auto views = backend.locateViews(0);
  float separation = views[uint32_t(VrEye::Right)].pose.position.x
                   - views[uint32_t(VrEye::Left)].pose.position.x;

  CHECK_NEAR(separation, 0.064f, 1e-5);
}


TEST_CASE(forward_input_moves_along_view_direction) {
  VrEmulatorBackend backend = makeRunningBackend();
  VrInputState start = backend.pollInput(0);

  VrEmulatorInput input;
  input.moveForward = 1.0f;
  backend.setInput(input);

  VrInputState moved = backend.pollInput(1000000000);

  // One second at the default speed, heading -Z
  CHECK_NEAR(moved.headPose.position.z, start.headPose.position.z - 1.5f, 1e-4);
  CHECK_NEAR(moved.headPose.position.x, start.headPose.position.x, 1e-4);
}


TEST_CASE(mouse_turns_head_and_movement_follows_heading) {
  VrEmulatorRigConfig rigConfig;
  rigConfig.radiansPerPixel = Pi * 0.5f / 100.0f;

  VrEmulatorConfig config;
  config.rig = rigConfig;

  VrEmulatorBackend backend(config);
  backend.beginSession(VrGraphicsBinding());
  backend.pollInput(0);

  // Quarter turn to the right
  VrEmulatorInput turn;
  turn.mouseDeltaX = 100.0f;
  turn.moveForward = 1.0f;
  backend.setInput(turn);

  VrInputState state = backend.pollInput(1000000000);

  // Facing +X after turning right, so the move goes along +X
  CHECK_NEAR(state.headPose.position.x, 1.5f, 1e-3);
  CHECK_NEAR(state.headPose.position.z, 0.0f, 1e-3);

  // Mouse movement is consumed, the next frame does not turn again
  VrInputState again = backend.pollInput(2000000000);
  CHECK_NEAR(again.headPose.position.z, 0.0f, 1e-3);
}


TEST_CASE(pitch_is_clamped) {
  VrEmulatorBackend backend = makeRunningBackend();
  backend.pollInput(0);

  VrEmulatorInput look;
  look.mouseDeltaY = -1000000.0f;
  backend.setInput(look);

  VrInputState state = backend.pollInput(11111111);
  VrVector3 forward = vrRotate(state.headPose.orientation, { 0.0f, 0.0f, -1.0f });
  CHECK(forward.y < 1.0f);
  CHECK(forward.y > 0.9f);
}


TEST_CASE(recenter_restores_start_pose) {
  VrEmulatorBackend backend = makeRunningBackend();
  VrInputState start = backend.pollInput(0);

  VrEmulatorInput move;
  move.moveForward = 1.0f;
  move.mouseDeltaX = 50.0f;
  backend.setInput(move);
  backend.pollInput(1000000000);

  VrEmulatorInput recenter;
  recenter.recenter = true;
  backend.setInput(recenter);

  VrInputState state = backend.pollInput(1011111111);
  CHECK_NEAR(state.headPose.position.x, start.headPose.position.x, 1e-3);
  CHECK_NEAR(state.headPose.position.z, start.headPose.position.z, 1e-3);
}


TEST_CASE(controllers_follow_head_and_report_triggers) {
  VrEmulatorBackend backend = makeRunningBackend();

  VrEmulatorInput input;
  input.rightTrigger = true;
  backend.setInput(input);

  VrInputState state = backend.pollInput(11111111);
  const VrControllerState& left  = state.controllers[uint32_t(VrHand::Left)];
  const VrControllerState& right = state.controllers[uint32_t(VrHand::Right)];

  CHECK(left.isActive);
  CHECK(right.isActive);
  CHECK(left.gripPose.position.x < state.headPose.position.x);
  CHECK(right.gripPose.position.x > state.headPose.position.x);
  CHECK(state.hasActions);
  CHECK(state.actions.isPressed(VrAction::Attack));
  CHECK(!state.actions.isPressed(VrAction::Aim));
}


TEST_CASE(triggers_drive_menu_actions_in_menu_context) {
  VrEmulatorBackend backend = makeRunningBackend();
  backend.setInputContext(VrInputContext::Menu);

  VrEmulatorInput input;
  input.leftTrigger = true;
  backend.setInput(input);

  VrInputState state = backend.pollInput(11111111);
  CHECK(state.actions.isPressed(VrAction::MenuBack));
  CHECK(!state.actions.isPressed(VrAction::Aim));
  CHECK(!state.actions.isPressed(VrAction::MenuSelect));
}


TEST_CASE(head_script_interpolates_between_keyframes) {
  VrEmulatorBackend backend = makeRunningBackend();

  VrPoseKeyframe a, b;
  a.timeNs = 0;
  a.pose.position = { 0.0f, 1.7f, 0.0f };
  b.timeNs = 1000000000;
  b.pose.position = { 2.0f, 1.7f, 0.0f };
  backend.setHeadScript({ a, b });

  CHECK_NEAR(backend.pollInput(500000000).headPose.position.x, 1.0f, 1e-4);
  CHECK_NEAR(backend.pollInput(2000000000).headPose.position.x, 2.0f, 1e-4);
}


TEST_CASE(submitted_frames_reach_the_sink) {
  VrEmulatorBackend backend = makeRunningBackend();
  CountingSink sink;
  backend.setFrameSink(&sink);

  VrFrameSubmission frame;
  frame.displayTime = 1234;
  CHECK(backend.submitFrame(frame));
  backend.submitEmptyFrame(5678);

  CHECK(sink.count == 1);
  CHECK(sink.lastDisplayTime == 1234);
  CHECK(backend.submittedFrameCount() == 1);
  CHECK(backend.emptyFrameCount() == 1);

  backend.endSession();
  CHECK(!backend.submitFrame(frame));
}


TEST_CASE(haptics_are_recorded_per_hand) {
  VrEmulatorBackend backend = makeRunningBackend();
  backend.applyHaptic(VrHand::Right, 0.75f, 100000000);

  CHECK_NEAR(backend.lastHapticAmplitude(VrHand::Right), 0.75f, 1e-6);
  CHECK_NEAR(backend.lastHapticAmplitude(VrHand::Left), 0.0f, 1e-6);
}


TEST_CASE(input_source_is_sampled_each_poll) {
  VrEmulatorBackend backend = makeRunningBackend();

  backend.setInputSource([] {
    VrEmulatorInput input;
    input.moveForward = 1.0f;
    return input;
  });

  backend.pollInput(backend.waitFrame().predictedDisplayTime);
  const float first = backend.pollInput(backend.waitFrame().predictedDisplayTime).headPose.position.z;
  const float second = backend.pollInput(backend.waitFrame().predictedDisplayTime).headPose.position.z;

  CHECK(first < 0.0f);
  CHECK(second < first);
}


TEST_CASE(panel_stays_visible_until_hidden) {
  VrEmulatorBackend backend;
  CountingSink sink;
  backend.setFrameSink(&sink);

  VrPanelSubmission panel;
  panel.width = 2.0f;

  // No session yet
  CHECK(!backend.submitPanel(VrPanelId::Menu, panel));
  CHECK(!backend.isPanelVisible(VrPanelId::Menu));

  backend.beginSession(VrGraphicsBinding());
  CHECK(backend.submitPanel(VrPanelId::Menu, panel));
  CHECK(backend.submitPanel(VrPanelId::Menu, panel));
  CHECK(backend.isPanelVisible(VrPanelId::Menu));
  CHECK(backend.submittedPanelCount() == 2);
  CHECK(sink.panelCount == 2);
  CHECK_NEAR(sink.lastPanelWidth, 2.0f, 1e-6);

  // Panels are independent of the frame loop
  CHECK(backend.submittedFrameCount() == 0);

  backend.hidePanel(VrPanelId::Menu);
  CHECK(!backend.isPanelVisible(VrPanelId::Menu));
}


TEST_CASE(panels_are_shown_and_hidden_separately) {
  VrEmulatorBackend backend;
  CountingSink sink;
  backend.setFrameSink(&sink);
  backend.beginSession(VrGraphicsBinding());

  VrPanelSubmission hud;
  hud.width  = 1.0f;
  hud.anchor = VrPanelAnchor::Head;

  CHECK(backend.submitPanel(VrPanelId::HudHead, hud));
  CHECK(sink.lastPanelId == VrPanelId::HudHead);
  CHECK(backend.isPanelVisible(VrPanelId::HudHead));
  CHECK(!backend.isPanelVisible(VrPanelId::Menu));

  CHECK(backend.submitPanel(VrPanelId::Menu, VrPanelSubmission()));
  backend.hidePanel(VrPanelId::HudHead);
  CHECK(!backend.isPanelVisible(VrPanelId::HudHead));
  CHECK(backend.isPanelVisible(VrPanelId::Menu));

  backend.endSession();
  CHECK(!backend.isPanelVisible(VrPanelId::Menu));
}
