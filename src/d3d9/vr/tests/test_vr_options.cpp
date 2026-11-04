#include "test.h"

#include "../vr_options.h"

using namespace dxvk;


TEST_CASE(options_default_to_snap_turn) {
  VrOptions options;
  CHECK(!options.smoothTurn);
  CHECK_NEAR(options.smoothTurnSpeed, 120.0f, 1e-6);
}


TEST_CASE(options_round_trip_through_the_file_format) {
  VrOptions written;
  written.smoothTurn = true;

  VrOptions read;
  vrParseOptions(vrFormatOptions(written), read);
  CHECK(read.smoothTurn);

  written.smoothTurn = false;
  read.smoothTurn = true;
  vrParseOptions(vrFormatOptions(written), read);
  CHECK(!read.smoothTurn);
}


TEST_CASE(options_parser_ignores_comments_sections_and_case) {
  VrOptions options;
  vrParseOptions("; comment\n# another\n[VR]\r\n  SMOOTHturn = 1 \r\nother=3\n", options);
  CHECK(options.smoothTurn);
}


TEST_CASE(options_parser_keeps_values_it_cannot_read) {
  VrOptions options;
  options.smoothTurn = true;

  vrParseOptions("", options);
  CHECK(options.smoothTurn);

  vrParseOptions("SmoothTurn=maybe\nSmoothTurn\n=1\n", options);
  CHECK(options.smoothTurn);

  vrParseOptions("SmoothTurn=0", options);
  CHECK(!options.smoothTurn);
}


TEST_CASE(options_round_trip_the_turn_speed) {
  VrOptions written;
  written.smoothTurnSpeed = 210.0f;

  VrOptions read;
  vrParseOptions(vrFormatOptions(written), read);
  CHECK_NEAR(read.smoothTurnSpeed, 210.0f, 1e-6);
}


TEST_CASE(options_parser_limits_the_turn_speed) {
  VrOptions options;
  vrParseOptions("SmoothTurnSpeed=5000", options);
  CHECK_NEAR(options.smoothTurnSpeed, 360.0f, 1e-6);

  vrParseOptions("SmoothTurnSpeed=1", options);
  CHECK_NEAR(options.smoothTurnSpeed, 30.0f, 1e-6);

  vrParseOptions("SmoothTurnSpeed=-4", options);
  CHECK_NEAR(options.smoothTurnSpeed, 30.0f, 1e-6);

  vrParseOptions("SmoothTurnSpeed=fast", options);
  CHECK_NEAR(options.smoothTurnSpeed, 30.0f, 1e-6);
}


TEST_CASE(turn_speed_slider_positions) {
  CHECK_NEAR(vrSmoothTurnStepToSpeed(0), 30.0f, 1e-6);
  CHECK_NEAR(vrSmoothTurnStepToSpeed(3), 120.0f, 1e-6);
  CHECK_NEAR(vrSmoothTurnStepToSpeed(VrSmoothTurnSpeedSteps - 1), 360.0f, 1e-6);
  CHECK_NEAR(vrSmoothTurnStepToSpeed(99), 360.0f, 1e-6);

  CHECK(vrSmoothTurnSpeedToStep(120.0f) == 3);
  CHECK(vrSmoothTurnSpeedToStep(134.0f) == 3);
  CHECK(vrSmoothTurnSpeedToStep(136.0f) == 4);
  CHECK(vrSmoothTurnSpeedToStep(1.0f) == 0);
  CHECK(vrSmoothTurnSpeedToStep(9000.0f) == VrSmoothTurnSpeedSteps - 1);
}


TEST_CASE(options_round_trip_the_pipboy_size) {
  VrOptions options;
  CHECK_NEAR(options.pipBoyScale, 1.0f, 1e-6);

  VrOptions written;
  written.pipBoyScale = 1.75f;

  VrOptions read;
  vrParseOptions(vrFormatOptions(written), read);
  CHECK_NEAR(read.pipBoyScale, 1.75f, 1e-6);
}


TEST_CASE(options_parser_limits_the_pipboy_size) {
  VrOptions options;
  vrParseOptions("PipBoyScale=9", options);
  CHECK_NEAR(options.pipBoyScale, 2.0f, 1e-6);

  vrParseOptions("PipBoyScale=0.5", options);
  CHECK_NEAR(options.pipBoyScale, 1.0f, 1e-6);

  vrParseOptions("PipBoyScale=big", options);
  CHECK_NEAR(options.pipBoyScale, 1.0f, 1e-6);
}


TEST_CASE(pipboy_size_positions) {
  CHECK_NEAR(vrPipBoyStepToScale(0), 1.0f, 1e-6);
  CHECK_NEAR(vrPipBoyStepToScale(2), 1.5f, 1e-6);
  CHECK_NEAR(vrPipBoyStepToScale(VrPipBoyScaleSteps - 1), 2.0f, 1e-6);
  CHECK_NEAR(vrPipBoyStepToScale(99), 2.0f, 1e-6);

  CHECK(vrPipBoyScaleToStep(1.0f) == 0);
  CHECK(vrPipBoyScaleToStep(1.3f) == 1);
  CHECK(vrPipBoyScaleToStep(0.2f) == 0);
  CHECK(vrPipBoyScaleToStep(5.0f) == VrPipBoyScaleSteps - 1);
}


TEST_CASE(options_round_trip_the_manual_reload) {
  VrOptions options;
  CHECK(options.manualReload);

  VrOptions written;
  written.manualReload = false;

  VrOptions read;
  vrParseOptions(vrFormatOptions(written), read);
  CHECK(!read.manualReload);

  vrParseOptions("ManualReload=1", read);
  CHECK(read.manualReload);
}


TEST_CASE(options_round_trip_the_physical_sneak) {
  VrOptions options;
  CHECK(!options.physicalSneak);

  VrOptions written;
  written.physicalSneak = true;

  VrOptions read;
  vrParseOptions(vrFormatOptions(written), read);
  CHECK(read.physicalSneak);

  vrParseOptions("physicalsneak=0", read);
  CHECK(!read.physicalSneak);
}


TEST_CASE(options_round_trip_the_roomscale) {
  VrOptions options;
  CHECK(!options.roomscale);

  VrOptions written;
  written.roomscale = true;

  VrOptions read;
  vrParseOptions(vrFormatOptions(written), read);
  CHECK(read.roomscale);

  vrParseOptions("roomscale=0", read);
  CHECK(!read.roomscale);
}
