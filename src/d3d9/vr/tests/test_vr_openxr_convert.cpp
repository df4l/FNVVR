#include "test.h"

#include "../vr_openxr_convert.h"

using namespace dxvk;


TEST_CASE(openxr_pose_round_trip_keeps_values) {
  VrPose pose;
  pose.orientation = { 0.1f, -0.2f, 0.3f, 0.927f };
  pose.position    = { 1.5f, 1.7f, -0.4f };

  VrPose back = vrPoseFromXr(vrPoseToXr(pose));

  CHECK_NEAR(back.orientation.x, 0.1, 1e-6);
  CHECK_NEAR(back.orientation.y, -0.2, 1e-6);
  CHECK_NEAR(back.orientation.z, 0.3, 1e-6);
  CHECK_NEAR(back.orientation.w, 0.927, 1e-6);
  CHECK_NEAR(back.position.x, 1.5, 1e-6);
  CHECK_NEAR(back.position.y, 1.7, 1e-6);
  CHECK_NEAR(back.position.z, -0.4, 1e-6);
}


TEST_CASE(openxr_fov_keeps_signs) {
  XrFovf fov = { -0.9f, 0.8f, 0.85f, -0.95f };
  VrFov converted = vrFovFromXr(fov);

  CHECK_NEAR(converted.angleLeft, -0.9, 1e-6);
  CHECK_NEAR(converted.angleRight, 0.8, 1e-6);
  CHECK_NEAR(converted.angleUp, 0.85, 1e-6);
  CHECK_NEAR(converted.angleDown, -0.95, 1e-6);

  XrFovf back = vrFovToXr(converted);
  CHECK_NEAR(back.angleLeft, -0.9, 1e-6);
  CHECK_NEAR(back.angleDown, -0.95, 1e-6);
}


TEST_CASE(openxr_extension_list_is_split_on_spaces) {
  auto names = vrSplitXrExtensionList("VK_KHR_external_memory  VK_KHR_dedicated_allocation VK_WINE_openxr_device_extensions ");

  CHECK(names.size() == 3);
  CHECK(names[0] == "VK_KHR_external_memory");
  CHECK(names[2] == "VK_WINE_openxr_device_extensions");
  CHECK(vrSplitXrExtensionList("").empty());
}


TEST_CASE(openxr_swapchain_keeps_channel_order_in_srgb) {
  std::vector<int64_t> formats = { VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
    VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM };

  VrXrSwapchainFormat bgra = vrChooseXrSwapchainFormat(formats, VK_FORMAT_B8G8R8A8_UNORM);
  CHECK(bgra.format == VK_FORMAT_B8G8R8A8_SRGB);
  CHECK(bgra.srgb);

  VrXrSwapchainFormat rgba = vrChooseXrSwapchainFormat(formats, VK_FORMAT_R8G8B8A8_UNORM);
  CHECK(rgba.format == VK_FORMAT_R8G8B8A8_SRGB);
  CHECK(rgba.srgb);
}


TEST_CASE(openxr_swapchain_falls_back_to_the_same_format) {
  // No sRGB variant: the bytes are still copied as they are
  std::vector<int64_t> formats = { VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM };

  VrXrSwapchainFormat bgra = vrChooseXrSwapchainFormat(formats, VK_FORMAT_B8G8R8A8_UNORM);
  CHECK(bgra.format == VK_FORMAT_B8G8R8A8_UNORM);
  CHECK(!bgra.srgb);

  // A different channel order cannot be copied byte for byte
  std::vector<int64_t> rgbaOnly = { VK_FORMAT_R8G8B8A8_SRGB };
  CHECK(vrChooseXrSwapchainFormat(rgbaOnly, VK_FORMAT_B8G8R8A8_UNORM).format == VK_FORMAT_UNDEFINED);
  CHECK(vrChooseXrSwapchainFormat(formats, VK_FORMAT_R16G16B16A16_SFLOAT).format == VK_FORMAT_UNDEFINED);
}


TEST_CASE(openxr_quad_height_follows_image_aspect) {
  XrExtent2Df size = vrXrQuadSize(2.0f, { 1280, 720 });
  CHECK_NEAR(size.width, 2.0, 1e-6);
  CHECK_NEAR(size.height, 1.125, 1e-6);

  CHECK_NEAR(vrXrQuadSize(1.0f, { 0, 0 }).height, 0.0, 1e-6);
}


TEST_CASE(openxr_reference_space_prefers_stage_then_local_floor) {
  std::vector<XrReferenceSpaceType> all = { XR_REFERENCE_SPACE_TYPE_VIEW, XR_REFERENCE_SPACE_TYPE_LOCAL,
    XR_REFERENCE_SPACE_TYPE_STAGE, XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT };
  CHECK(vrChooseXrReferenceSpace(all, true) == XR_REFERENCE_SPACE_TYPE_STAGE);

  std::vector<XrReferenceSpaceType> noStage = { XR_REFERENCE_SPACE_TYPE_VIEW, XR_REFERENCE_SPACE_TYPE_LOCAL,
    XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT };
  CHECK(vrChooseXrReferenceSpace(noStage, true) == XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT);

  // The local floor space needs its extension
  CHECK(vrChooseXrReferenceSpace(noStage, false) == XR_REFERENCE_SPACE_TYPE_LOCAL);
}


TEST_CASE(openxr_session_runs_only_once_begun) {
  CHECK(vrSessionStateFromXr(XR_SESSION_STATE_IDLE, false) == VrSessionState::Idle);
  CHECK(vrSessionStateFromXr(XR_SESSION_STATE_READY, false) == VrSessionState::Ready);
  CHECK(vrSessionStateFromXr(XR_SESSION_STATE_READY, true) == VrSessionState::Running);
  CHECK(vrSessionStateFromXr(XR_SESSION_STATE_FOCUSED, true) == VrSessionState::Running);
  CHECK(vrSessionStateFromXr(XR_SESSION_STATE_VISIBLE, false) == VrSessionState::Idle);
  CHECK(vrSessionStateFromXr(XR_SESSION_STATE_STOPPING, true) == VrSessionState::Stopping);
  CHECK(vrSessionStateFromXr(XR_SESSION_STATE_EXITING, false) == VrSessionState::Stopping);
  CHECK(vrSessionStateFromXr(XR_SESSION_STATE_LOSS_PENDING, true) == VrSessionState::Lost);
}
