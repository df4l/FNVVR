#include <algorithm>
#include <sstream>

#include "vr_openxr_convert.h"

namespace dxvk {

  VrPose vrPoseFromXr(const XrPosef& pose) {
    VrPose result;
    result.orientation = { pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w };
    result.position    = { pose.position.x, pose.position.y, pose.position.z };
    return result;
  }


  XrPosef vrPoseToXr(const VrPose& pose) {
    XrPosef result;
    result.orientation = { pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w };
    result.position    = { pose.position.x, pose.position.y, pose.position.z };
    return result;
  }


  VrFov vrFovFromXr(const XrFovf& fov) {
    VrFov result;
    result.angleLeft  = fov.angleLeft;
    result.angleRight = fov.angleRight;
    result.angleUp    = fov.angleUp;
    result.angleDown  = fov.angleDown;
    return result;
  }


  XrFovf vrFovToXr(const VrFov& fov) {
    XrFovf result;
    result.angleLeft  = fov.angleLeft;
    result.angleRight = fov.angleRight;
    result.angleUp    = fov.angleUp;
    result.angleDown  = fov.angleDown;
    return result;
  }


  std::vector<std::string> vrSplitXrExtensionList(const std::string& list) {
    std::vector<std::string> names;
    std::istringstream stream(list);
    std::string name;

    while (stream >> name)
      names.push_back(name);

    return names;
  }


  VrXrSwapchainFormat vrChooseXrSwapchainFormat(const std::vector<int64_t>& formats, VkFormat source) {
    VkFormat srgb = VK_FORMAT_UNDEFINED;

    switch (source) {
      case VK_FORMAT_B8G8R8A8_UNORM:
      case VK_FORMAT_B8G8R8A8_SRGB:
        srgb = VK_FORMAT_B8G8R8A8_SRGB;
        break;

      case VK_FORMAT_R8G8B8A8_UNORM:
      case VK_FORMAT_R8G8B8A8_SRGB:
        srgb = VK_FORMAT_R8G8B8A8_SRGB;
        break;

      default:
        break;
    }

    auto offers = [&formats] (VkFormat format) {
      return std::find(formats.begin(), formats.end(), int64_t(format)) != formats.end();
    };

    VrXrSwapchainFormat result;

    if (srgb != VK_FORMAT_UNDEFINED && offers(srgb)) {
      result.format = srgb;
      result.srgb   = true;
    } else if (offers(source)) {
      result.format = source;
      result.srgb   = source == srgb;
    }

    return result;
  }


  XrExtent2Df vrXrQuadSize(float width, VrExtent image) {
    XrExtent2Df size = { width, 0.0f };

    if (image.width)
      size.height = width * float(image.height) / float(image.width);

    return size;
  }


  XrReferenceSpaceType vrChooseXrReferenceSpace(const std::vector<XrReferenceSpaceType>& spaces, bool localFloor) {
    auto offers = [&spaces] (XrReferenceSpaceType type) {
      return std::find(spaces.begin(), spaces.end(), type) != spaces.end();
    };

    if (offers(XR_REFERENCE_SPACE_TYPE_STAGE))
      return XR_REFERENCE_SPACE_TYPE_STAGE;

    if (localFloor && offers(XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT))
      return XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT;

    return XR_REFERENCE_SPACE_TYPE_LOCAL;
  }


  VrSessionState vrSessionStateFromXr(XrSessionState state, bool running) {
    switch (state) {
      case XR_SESSION_STATE_READY:
        return running ? VrSessionState::Running : VrSessionState::Ready;

      case XR_SESSION_STATE_SYNCHRONIZED:
      case XR_SESSION_STATE_VISIBLE:
      case XR_SESSION_STATE_FOCUSED:
        return running ? VrSessionState::Running : VrSessionState::Idle;

      case XR_SESSION_STATE_STOPPING:
      case XR_SESSION_STATE_EXITING:
        return VrSessionState::Stopping;

      case XR_SESSION_STATE_LOSS_PENDING:
        return VrSessionState::Lost;

      default:
        return VrSessionState::Idle;
    }
  }

}
