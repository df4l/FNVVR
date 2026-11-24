#include <algorithm>
#include <cmath>
#include <sstream>

#include "vr_math.h"
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



  namespace {

    VrVector3 cross(const VrVector3& a, const VrVector3& b) {
      return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    VrVector3 normalize(const VrVector3& v) {
      float length = vrLength(v);
      return length > 0.0f ? v * (1.0f / length) : v;
    }

    /**
     * \brief Rotation whose matrix has the given orthonormal columns
     */
    VrQuaternion quaternionFromBasis(const VrVector3& x, const VrVector3& y, const VrVector3& z) {
      float trace = x.x + y.y + z.z;
      VrQuaternion q;

      if (trace > 0.0f) {
        float s = 0.5f / std::sqrt(trace + 1.0f);
        q.w = 0.25f / s;
        q.x = (y.z - z.y) * s;
        q.y = (z.x - x.z) * s;
        q.z = (x.y - y.x) * s;
      } else if (x.x > y.y && x.x > z.z) {
        float s = 2.0f * std::sqrt(1.0f + x.x - y.y - z.z);
        q.w = (y.z - z.y) / s;
        q.x = 0.25f * s;
        q.y = (y.x + x.y) / s;
        q.z = (z.x + x.z) / s;
      } else if (y.y > z.z) {
        float s = 2.0f * std::sqrt(1.0f + y.y - x.x - z.z);
        q.w = (z.x - x.z) / s;
        q.x = (y.x + x.y) / s;
        q.y = 0.25f * s;
        q.z = (z.y + y.z) / s;
      } else {
        float s = 2.0f * std::sqrt(1.0f + z.z - x.x - y.y);
        q.w = (x.y - y.x) / s;
        q.x = (z.x + x.z) / s;
        q.y = (z.y + y.z) / s;
        q.z = 0.25f * s;
      }

      return vrNormalize(q);
    }

  }


  VrPose vrXrBeamPose(const VrPose& origin, float length, const VrVector3& eye) {
    VrVector3 along  = normalize(vrRotate(origin.orientation, { 0.0f, 0.0f, -1.0f }));
    VrVector3 centre = origin.position + along * (0.5f * length);

    // The quad's normal is the direction to the eye, without its part
    // along the laser. Looking along the laser, any normal will do.
    VrVector3 toEye  = eye - centre;
    VrVector3 normal = toEye - along * vrDot(toEye, along);

    if (vrLength(normal) < 1e-4f)
      normal = vrRotate(origin.orientation, { 0.0f, 1.0f, 0.0f });

    normal = normalize(normal);

    VrPose pose;
    pose.position    = centre;
    pose.orientation = quaternionFromBasis(cross(along, normal), along, normal);
    return pose;
  }


  bool vrGazeFromXrDirection(const VrQuaternion& eye, const VrVector3& direction, VrGaze& gaze) {
    VrVector3 local = vrRotate(vrConjugate(eye), direction);

    // The view looks along -Z
    if (local.z > -1e-3f)
      return false;

    gaze.tanX = local.x / -local.z;
    gaze.tanY = local.y / -local.z;
    return true;
  }


  float vrXrLinearFromSrgb(float value) {
    value = std::clamp(value, 0.0f, 1.0f);

    return value <= 0.04045f
      ? value / 12.92f
      : std::pow((value + 0.055f) / 1.055f, 2.4f);
  }


  void VrXrFade::start(double now, float seconds, float opacity) {
    m_from     = this->opacity(now);
    m_to       = std::clamp(opacity, 0.0f, 1.0f);
    m_start    = now;
    m_duration = std::max(seconds, 0.0f);
  }


  float VrXrFade::opacity(double now) const {
    if (m_duration <= 0.0f || now >= m_start + double(m_duration))
      return m_to;

    float t = float(std::max(now - m_start, 0.0) / double(m_duration));
    return m_from + (m_to - m_from) * t;
  }

}
