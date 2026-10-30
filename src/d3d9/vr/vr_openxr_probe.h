#pragma once

namespace dxvk {

  /**
   * \brief Checks that an OpenXR runtime answers, and logs what it reports
   *
   * Loads the OpenXR loader, creates an instance with XR_KHR_vulkan_enable
   * and logs the runtime, the headset, the recommended eye image size and
   * the Vulkan extensions and versions the runtime requires. The instance is
   * destroyed before returning: no session is started.
   *
   * \returns Whether an instance and a headset system were obtained
   */
  bool vrProbeOpenXr();

}
