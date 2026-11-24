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

  /**
   * \brief Pose of the quad layer a laser is drawn with
   *
   * The quad is centred halfway along the laser, its +Y axis along the
   * laser and its face turned towards the eye as far as the laser allows,
   * so it is seen across its width from anywhere.
   *
   * \param [in] origin Start of the laser, which goes along its -Z axis
   * \param [in] length Length of the laser, in metres
   * \param [in] eye Position of the head, in the same space
   */
  VrPose vrXrBeamPose(const VrPose& origin, float length, const VrVector3& eye);

  /**
   * \brief Where one eye looks, from a gaze direction
   *
   * OpenXR gives one gaze for both eyes; each eye is taken to look along it.
   *
   * \param [in] eye Orientation of the eye's view
   * \param [in] direction Gaze direction, in the same space
   * \param [out] gaze Tangents of the gaze, right and up, in the eye's view
   * \returns \c false if the gaze does not point ahead of the eye
   */
  bool vrGazeFromXrDirection(const VrQuaternion& eye, const VrVector3& direction, VrGaze& gaze);

  /**
   * \brief Linear value of an sRGB-encoded colour channel in [0, 1]
   *
   * A swapchain image in an sRGB format is filled with linear values.
   */
  float vrXrLinearFromSrgb(float value);

  /**
   * \brief Black drawn over the view, fading in or out over time
   *
   * OpenXR has no fade of its own: the backend draws a black quad with this
   * opacity over the eyes. Times are in seconds, on any steady clock.
   */
  class VrXrFade {

  public:

    /**
     * \brief Fades from the opacity shown now to another one
     *
     * \param [in] now Current time
     * \param [in] seconds Duration, 0 for at once
     * \param [in] opacity Opacity reached at the end, in [0, 1]
     */
    void start(double now, float seconds, float opacity);

    /**
     * \brief Opacity of the black at a time
     */
    float opacity(double now) const;

  private:

    double m_start    = 0.0;
    float  m_duration = 0.0f;
    float  m_from     = 0.0f;
    float  m_to       = 0.0f;

  };

}
