#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <openxr/openxr.h>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Pose from OpenXR
   *
   * The VR layer uses OpenXR's axes, so only the types change.
   */
  VrPose vrPoseFromXr(const XrPosef& pose);

  XrPosef vrPoseToXr(const VrPose& pose);

  VrFov vrFovFromXr(const XrFovf& fov);

  XrFovf vrFovToXr(const VrFov& fov);

  /**
   * \brief Splits a space-separated list of Vulkan extension names
   */
  std::vector<std::string> vrSplitXrExtensionList(const std::string& list);

  /**
   * \brief Format of a swapchain the game's images are copied into
   */
  struct VrXrSwapchainFormat {
    VkFormat format = VK_FORMAT_UNDEFINED;
    /// The runtime reads the image as sRGB-encoded, as the game wrote it
    bool     srgb   = false;
  };

  /**
   * \brief Picks the swapchain format for images of a given format
   *
   * The images are copied byte for byte, so the swapchain keeps their
   * channel order. The game writes display values: the sRGB variant of the
   * format tells the runtime so, whereas a UNORM swapchain would be read as
   * linear and look washed out.
   *
   * \param [in] formats Formats the runtime offers, in its order of preference
   * \param [in] source Format of the images to copy
   * \returns The format, or VK_FORMAT_UNDEFINED if none fits
   */
  VrXrSwapchainFormat vrChooseXrSwapchainFormat(const std::vector<int64_t>& formats, VkFormat source);

  /**
   * \brief Size of a quad layer showing an image at a width in metres
   */
  XrExtent2Df vrXrQuadSize(float width, VrExtent image);

  /**
   * \brief Reference space the poses are given in
   *
   * The stage puts the floor at y = 0, like OpenVR's standing space. Without
   * it, the local floor space does the same; the local space only has the
   * floor at an unknown height.
   *
   * \param [in] spaces Reference spaces the runtime offers
   * \param [in] localFloor Whether XR_EXT_local_floor is enabled
   */
  XrReferenceSpaceType vrChooseXrReferenceSpace(const std::vector<XrReferenceSpaceType>& spaces, bool localFloor);

  /**
   * \brief Session state the rest of the VR layer sees
   *
   * \param [in] state OpenXR's state
   * \param [in] running Whether xrBeginSession was called and not ended
   */
  VrSessionState vrSessionStateFromXr(XrSessionState state, bool running);

}
