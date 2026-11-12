#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

#include "../../util/log/log.h"
#include "../../util/util_env.h"
#include "../../util/util_string.h"

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif

#include <openvr/openvr.hpp>

#include "vr_eye_transition.h"
#include "vr_math.h"
#include "vr_module_path.h"
#include "vr_openvr_backend.h"
#include "vr_openvr_convert.h"

namespace dxvk {

  namespace {

    // A copy next to the game takes precedence. Proton installs its own
    // 32-bit build as openvr_api_dxvk.dll in syswow64, for its DXVK.
    constexpr std::array<const char*, 2> RuntimeLibraryNames = {
      "openvr_api.dll",
      "openvr_api_dxvk.dll",
    };

    constexpr int64_t NanosecondsPerSecond = 1000000000;

    // Used when the headset does not report its refresh rate
    constexpr float FallbackRefreshRate = 90.0f;

    // Recent frames whose compositor timing is logged with each window
    constexpr uint32_t CompositorHistory = 90;

    // FILETIME units, 100 ns, per millisecond
    constexpr double FileTimeUnitsPerMs = 10000.0;

    uint64_t fileTimeValue(const FILETIME& time) {
      return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    }

    // CPU time used so far by the calling thread and by the whole process.
    // Windows counts it in clock ticks, so only a sum over many frames is
    // meaningful.
    VrCpuTimes readCpuTimes() {
      VrCpuTimes times = { };
      FILETIME creation, exit, kernel, user;

      if (GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user)) {
        times.threadKernel = fileTimeValue(kernel);
        times.threadUser   = fileTimeValue(user);
      }

      if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        times.process = fileTimeValue(kernel) + fileTimeValue(user);

      return times;
    }

    // Identifies the panel overlay within SteamVR, and names it in its UI
    // Overlay key and name of each panel, indexed by VrPanelId. The sort
    // order follows the index, so the HUD is drawn over the menu panel, the
    // hand panels, which are nearer, over the HUD, and the console's
    // terminal, nearer than the menu it is opened from, over all of them.
    constexpr std::array<const char*, VrPanelCount> PanelOverlayKeys  = {
      "fnvvr.panel", "fnvvr.hud.head", "fnvvr.hud.wrist", "fnvvr.hud.prompt", "fnvvr.hud.charge",
      "fnvvr.hud.ammo", "fnvvr.quickwheel", "fnvvr.terminal",
      "fnvvr.caravan.values" };
    constexpr std::array<const char*, VrPanelCount> PanelOverlayNames = {
      "Fallout: New Vegas", "Fallout: New Vegas HUD", "Fallout: New Vegas wrist HUD",
      "Fallout: New Vegas interaction prompt", "Fallout: New Vegas power attack charge",
      "Fallout: New Vegas ammunition", "Fallout: New Vegas quick wheel", "Fallout: New Vegas console",
      "Fallout: New Vegas Caravan values" };

    // Overlay key and name of each hand's laser, indexed by VrHand. Lasers
    // are drawn over the panels.
    constexpr std::array<const char*, VrHandCount> PointerOverlayKeys  = { "fnvvr.pointer.left", "fnvvr.pointer.right" };
    constexpr std::array<const char*, VrHandCount> PointerOverlayNames = { "Left laser", "Right laser" };
    constexpr uint32_t PointerSortOrder = VrPanelCount;

    // Opaque value the runtime gives back with the keyboard's events
    constexpr uint64_t KeyboardUserValue = 0x464E5652; // "FNVR"

    // The laser's image: a few texels across, with soft edges, in the
    // amber of the Pip-Boy's screen. Its height is stretched to the
    // laser's length with the texel aspect.
    constexpr uint32_t PointerImageWidth  = 8;
    constexpr uint32_t PointerImageHeight = 8;
    constexpr float    PointerWidth       = 0.005f;
    constexpr uint8_t  PointerColor[3]    = { 0xFF, 0xB6, 0x42 };
    constexpr float    PointerActiveAlpha   = 0.9f;
    constexpr float    PointerInactiveAlpha = 0.35f;

    VrPose poseFromOpenVr(const vr::HmdMatrix34_t& matrix) {
      return vrPoseFromMatrix34(matrix.m);
    }

    /**
     * \brief Calls IVRSystem::GetEyeToHeadTransform
     *
     * The runtime is built with MSVC, which passes \c this in ECX and the
     * address of a returned struct on the stack. 32-bit GCC swaps the two
     * for virtual methods returning a struct, so the method is called
     * through the vtable with MSVC's convention instead.
     */
    vr::HmdMatrix34_t getEyeToHeadTransform(vr::IVRSystem* system, vr::EVREye eye) {
#if defined(__i386__) && !defined(_MSC_VER)
      // The method's slot comes from the header's own vtable: in the
      // Itanium ABI a pointer to a virtual method holds its vtable offset
      // plus one
      struct VirtualMethodPointer {
        ptrdiff_t offset;
        ptrdiff_t adjust;
      };

      auto method = &vr::IVRSystem::GetEyeToHeadTransform;
      VirtualMethodPointer pointer;
      std::memcpy(&pointer, &method, sizeof(pointer));
      size_t slot = size_t(pointer.offset - 1) / sizeof(void*);

      using Fn = void (__thiscall*)(vr::IVRSystem*, vr::HmdMatrix34_t*, vr::EVREye);

      vr::HmdMatrix34_t result = { };
      auto vtable = *reinterpret_cast<void* const* const*>(system);
      reinterpret_cast<Fn>(vtable[slot])(system, &result, eye);
      return result;
#else
      return system->GetEyeToHeadTransform(eye);
#endif
    }

    std::vector<std::string> splitExtensions(const std::string& list) {
      std::vector<std::string> result;
      std::stringstream stream(list);
      std::string name;

      while (std::getline(stream, name, ' ')) {
        if (!name.empty())
          result.push_back(name);
      }

      return result;
    }

    std::string trimTerminator(std::vector<char> buffer, uint32_t length) {
      // The reported length includes the terminating zero
      size_t size = std::min<size_t>(length, buffer.size());

      while (size && buffer[size - 1] == '\0')
        size--;

      return std::string(buffer.data(), size);
    }

    std::string headsetString(vr::IVRSystem* system, vr::TrackedDeviceProperty property) {
      vr::ETrackedPropertyError error = vr::TrackedProp_Success;
      uint32_t length = system->GetStringTrackedDeviceProperty(
        vr::k_unTrackedDeviceIndex_Hmd, property, nullptr, 0, &error);

      if (!length)
        return "(unknown)";

      std::vector<char> buffer(length);
      length = system->GetStringTrackedDeviceProperty(
        vr::k_unTrackedDeviceIndex_Hmd, property, buffer.data(), length, &error);

      return error == vr::TrackedProp_Success ? trimTerminator(std::move(buffer), length) : "(unknown)";
    }

  }


  /**
   * \brief The loaded openvr_api.dll and the interfaces taken from it
   */
  struct VrOpenVrBackend::Runtime {
    using InitFn      = vr::IVRSystem* (VR_CALLTYPE*)(vr::EVRInitError*, vr::EVRApplicationType);
    using ShutdownFn  = void (VR_CALLTYPE*)();
    using InterfaceFn = void* (VR_CALLTYPE*)(const char*, vr::EVRInitError*);

    HMODULE            library    = nullptr;
    ShutdownFn         shutdown   = nullptr;
    vr::IVRSystem*     system     = nullptr;
    vr::IVRCompositor* compositor = nullptr;
    vr::IVROverlay*    overlay    = nullptr;
    vr::IVRInput*      input      = nullptr;
    bool               initialized = false;
  };


  VrOpenVrBackend::VrOpenVrBackend() { }


  VrOpenVrBackend::~VrOpenVrBackend() {
    endSession();
    shutdownRuntime();
  }


  const char* VrOpenVrBackend::name() const {
    return "OpenVR";
  }


  bool VrOpenVrBackend::initializeRuntime() {
    if (m_runtime)
      return m_runtime->initialized;

    m_runtime = std::make_unique<Runtime>();
    const char* libraryName = nullptr;

    for (const char* name : RuntimeLibraryNames) {
      m_runtime->library = LoadLibraryA(name);

      if (m_runtime->library) {
        libraryName = name;
        break;
      }
    }

    if (!m_runtime->library) {
      Logger::err("VR: No OpenVR runtime library was found, copy the 32-bit openvr_api.dll next to the game");
      return false;
    }

    Logger::info(str::format("VR: Loaded ", libraryName));

    auto init = reinterpret_cast<Runtime::InitFn>(GetProcAddress(m_runtime->library, "VR_InitInternal"));
    auto getInterface = reinterpret_cast<Runtime::InterfaceFn>(GetProcAddress(m_runtime->library, "VR_GetGenericInterface"));
    m_runtime->shutdown = reinterpret_cast<Runtime::ShutdownFn>(GetProcAddress(m_runtime->library, "VR_ShutdownInternal"));

    if (!init || !getInterface || !m_runtime->shutdown) {
      Logger::err(str::format("VR: ", libraryName, " does not export the OpenVR entry points"));
      return false;
    }

    vr::EVRInitError error = vr::VRInitError_None;
    init(&error, vr::VRApplication_Scene);

    if (error != vr::VRInitError_None) {
      Logger::err(str::format("VR: OpenVR could not be initialized, error ", int32_t(error),
        ". Is SteamVR installed and the headset connected?"));
      return false;
    }

    m_runtime->initialized = true;

    m_runtime->system = static_cast<vr::IVRSystem*>(getInterface(vr::IVRSystem_Version, &error));

    if (error == vr::VRInitError_None)
      m_runtime->compositor = static_cast<vr::IVRCompositor*>(getInterface(vr::IVRCompositor_Version, &error));

    if (error != vr::VRInitError_None || !m_runtime->system || !m_runtime->compositor) {
      Logger::err(str::format("VR: The OpenVR interfaces are not available, error ", int32_t(error)));
      m_runtime->system     = nullptr;
      m_runtime->compositor = nullptr;
      return false;
    }

    // Only the panel needs the overlay interface, VR works without it
    m_runtime->overlay = static_cast<vr::IVROverlay*>(getInterface(vr::IVROverlay_Version, &error));

    if (error != vr::VRInitError_None || !m_runtime->overlay) {
      Logger::warn(str::format("VR: The OpenVR overlay interface is not available, error ", int32_t(error),
        ". Menus will not be shown in the headset"));
      m_runtime->overlay = nullptr;
    }

    // Controllers need the input interface and the action manifest, the
    // headset works without them
    m_runtime->input = static_cast<vr::IVRInput*>(getInterface(vr::IVRInput_Version, &error));

    if (error != vr::VRInitError_None || !m_runtime->input) {
      Logger::warn(str::format("VR: The OpenVR input interface is not available, error ", int32_t(error),
        ". The controllers will do nothing"));
      m_runtime->input = nullptr;
    }

    m_inputReady = initializeInput();

    Logger::info("VR: OpenVR initialized");
    logEyeGeometry();
    return true;
  }


  void VrOpenVrBackend::logEyeGeometry() {
    constexpr float DegreesPerRadian = 57.2957795f;
    constexpr float MillimetresPerMetre = 1000.0f;

    VrExtent extent = recommendedEyeExtent();

    Logger::info(str::format("VR: Headset ", headsetString(m_runtime->system, vr::Prop_ModelNumber_String),
      " by ", headsetString(m_runtime->system, vr::Prop_ManufacturerName_String),
      ", recommended eye size ", extent.width, "x", extent.height));

    Logger::info(str::format("VR: Tracking system ", headsetString(m_runtime->system, vr::Prop_TrackingSystemName_String),
      ", display ", m_runtime->system->GetFloatTrackedDeviceProperty(
        vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float), " Hz, IPD ",
      m_runtime->system->GetFloatTrackedDeviceProperty(
        vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_UserIpdMeters_Float) * MillimetresPerMetre, " mm"));

    for (uint32_t i = 0; i < VrEyeCount; i++) {
      vr::EVREye eye = i == uint32_t(VrEye::Left) ? vr::Eye_Left : vr::Eye_Right;

      float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
      m_runtime->system->GetProjectionRaw(eye, &left, &right, &top, &bottom);
      VrFov fov = vrFovFromProjectionRaw(left, right, top, bottom);

      // Tilt of the eye's display relative to the head, zero unless the
      // displays are canted
      VrPose eyeInHead = poseFromOpenVr(getEyeToHeadTransform(m_runtime->system, eye));
      VrVector3 forward = vrRotate(eyeInHead.orientation, VrVector3 { 0.0f, 0.0f, -1.0f });
      VrVector3 up      = vrRotate(eyeInHead.orientation, VrVector3 { 0.0f, 1.0f, 0.0f });

      float yaw   = std::atan2(-forward.x, -forward.z);
      float pitch = std::asin(std::clamp(forward.y, -1.0f, 1.0f));
      float roll  = std::atan2(-up.x, up.y);

      Logger::info(str::format("VR: ", eye == vr::Eye_Left ? "Left" : "Right", " eye: raw projection left ", left,
        " right ", right, " top ", top, " bottom ", bottom,
        " (", (fov.angleRight - fov.angleLeft) * DegreesPerRadian, "x",
        (fov.angleUp - fov.angleDown) * DegreesPerRadian, " degrees, aspect ",
        (right - left) / (bottom - top), "), offset from the head ",
        eyeInHead.position.x * MillimetresPerMetre, ", ",
        eyeInHead.position.y * MillimetresPerMetre, ", ",
        eyeInHead.position.z * MillimetresPerMetre, " mm, yaw ",
        yaw * DegreesPerRadian, " pitch ", pitch * DegreesPerRadian,
        " roll ", roll * DegreesPerRadian, " degrees"));
    }
  }


  void VrOpenVrBackend::measureFrame(
          high_resolution_clock::time_point waitStart,
          high_resolution_clock::time_point posesReady) {
    const FrameClock& frame = m_frameClock;

    if (frame.isSubmitted && m_statsRunning) {
      auto ms = [] (high_resolution_clock::time_point from, high_resolution_clock::time_point to) {
        return std::chrono::duration<double, std::milli>(to - from).count();
      };

      VrFrameCycle cycle = { };
      cycle[uint32_t(VrFrameStep::Interval)]   = ms(frame.posesReady,    posesReady);
      cycle[uint32_t(VrFrameStep::Render)]     = ms(frame.posesReady,    frame.submitStart) - frame.syncMs;
      cycle[uint32_t(VrFrameStep::Sync)]       = frame.syncMs;
      cycle[uint32_t(VrFrameStep::Transition)] = ms(frame.submitStart,   frame.imagesReady);
      cycle[uint32_t(VrFrameStep::Submit)]     = ms(frame.imagesReady,   frame.eyesSubmitted);
      cycle[uint32_t(VrFrameStep::HandOff)]    = frame.handOffMs >= 0.0
        ? frame.handOffMs : ms(frame.eyesSubmitted, frame.submitted);
      cycle[uint32_t(VrFrameStep::After)]      = ms(frame.submitted,     waitStart);
      cycle[uint32_t(VrFrameStep::Panels)]     = frame.panelMs;
      cycle[uint32_t(VrFrameStep::Wait)]       = ms(waitStart,           posesReady);

      if (m_stats.add(cycle))
        logFrameStats();
    } else if (frame.isSubmitted) {
      startStatsWindow();
    } else {
      m_statsRunning = false;
    }

    m_frameClock = FrameClock();
    m_frameClock.posesReady = posesReady;
  }


  void VrOpenVrBackend::startStatsWindow() {
    vr::Compositor_CumulativeStats stats = { };
    m_runtime->compositor->GetCumulativeStats(&stats, sizeof(stats));

    m_statsPid     = stats.m_nPid;
    m_presented    = stats.m_nNumFramePresents;
    m_reprojected  = stats.m_nNumReprojectedFrames;
    m_dropped      = stats.m_nNumDroppedFrames;
    m_statsRunning = true;
    m_stats.reset();
    m_statsCpu     = readCpuTimes();
  }


  void VrOpenVrBackend::logFrameStats() {
    auto fixed = [] (double value) {
      std::ostringstream stream;
      stream << std::fixed << std::setprecision(2) << value;
      return stream.str();
    };

    auto pair = [&] (double average, double max) {
      return fixed(average) + "/" + fixed(max);
    };

    auto step = [&] (VrFrameStep step) {
      return pair(m_stats.average(step), m_stats.step(step).max);
    };

    double seconds = m_stats.seconds();

    Logger::info(str::format("VR: ", m_stats.frames(), " frames in ", fixed(seconds),
      " s, ", seconds > 0.0 ? std::lround(double(m_stats.frames()) / seconds) : 0, " fps, ",
      m_stats.lateFrames(), " late by a display period or more, ", m_stats.busyFrames(),
      " busy for longer than a period. Average/longest ms: frame ", step(VrFrameStep::Interval),
      ", eye rendering ", step(VrFrameStep::Render), ", DXVK sync ", step(VrFrameStep::Sync),
      ", layout transition ", step(VrFrameStep::Transition), ", submission ", step(VrFrameStep::Submit),
      ", hand-off ", step(VrFrameStep::HandOff), ", after submission ", step(VrFrameStep::After),
      " (panels ", step(VrFrameStep::Panels), "), waiting for the runtime ", step(VrFrameStep::Wait)));

    // What the compositor did over the window. Its counts restart when
    // another process connects, so they are only compared within one.
    vr::Compositor_CumulativeStats stats = { };
    m_runtime->compositor->GetCumulativeStats(&stats, sizeof(stats));

    std::string counts = "counts restarted by the runtime";

    if (stats.m_nPid == m_statsPid
     && stats.m_nNumFramePresents     >= m_presented
     && stats.m_nNumReprojectedFrames >= m_reprojected
     && stats.m_nNumDroppedFrames     >= m_dropped) {
      counts = str::format(stats.m_nNumFramePresents - m_presented, " presents, ",
        stats.m_nNumReprojectedFrames - m_reprojected, " reprojected, ",
        stats.m_nNumDroppedFrames - m_dropped, " dropped");
    }

    // The most recent frames in detail, read one at a time: some runtimes
    // return a single frame from GetFrameTimings
    uint32_t count = 0;
    uint32_t repeated = 0, misPresented = 0, reprojected = 0, dropped = 0;
    VrFrameStepStats sceneGpu, totalGpu, submitCpu, frameReady, idle;
    frameReady.max = -std::numeric_limits<double>::infinity();

    auto add = [] (VrFrameStepStats& stats, float value) {
      stats.sum += value;
      stats.max  = std::max(stats.max, double(value));
    };

    for (uint32_t i = 0; i < CompositorHistory; i++) {
      vr::Compositor_FrameTiming timing = { };
      timing.m_nSize = sizeof(timing);

      if (!m_runtime->compositor->GetFrameTiming(&timing, i))
        break;

      count++;
      repeated     += timing.m_nNumFramePresents > 1 ? 1 : 0;
      misPresented += timing.m_nNumMisPresented ? 1 : 0;
      dropped      += timing.m_nNumDroppedFrames;
      reprojected  += (timing.m_nReprojectionFlags
        & (vr::VRCompositor_ReprojectionReason_Cpu | vr::VRCompositor_ReprojectionReason_Gpu)) ? 1 : 0;

      add(sceneGpu,   timing.m_flPreSubmitGpuMs);
      add(totalGpu,   timing.m_flTotalRenderGpuMs);
      add(submitCpu,  timing.m_flSubmitFrameMs);
      add(frameReady, timing.m_flNewFrameReadyMs);
      add(idle,       timing.m_flCompositorIdleCpuMs);
    }

    std::string recent = "no frame timing";

    if (count) {
      auto stat = [&] (const VrFrameStepStats& stats) {
        return pair(stats.sum / double(count), stats.max);
      };

      recent = str::format("last ", count, " frames: ", repeated, " shown more than once, ",
        misPresented, " on another vsync than predicted, ", reprojected, " reprojected, ", dropped,
        " repeats of the previous frame; average/longest ms: scene GPU ", stat(sceneGpu),
        ", total GPU ", stat(totalGpu), ", in Submit ", stat(submitCpu), ", second eye submitted ",
        stat(frameReady), " after vsync, compositor idle ", stat(idle));
    }

    Logger::info(str::format("VR: Compositor: ", counts, " in the window; ", recent));

    // Whether the game thread computes or waits during the time the steps
    // above attribute to it, and how much the other threads work
    VrCpuTimes cpu = readCpuTimes();
    double windowMs = seconds * 1000.0;
    double threadMs = double((cpu.threadKernel + cpu.threadUser)
      - (m_statsCpu.threadKernel + m_statsCpu.threadUser)) / FileTimeUnitsPerMs;
    double kernelMs = double(cpu.threadKernel - m_statsCpu.threadKernel) / FileTimeUnitsPerMs;
    double processMs = double(cpu.process - m_statsCpu.process) / FileTimeUnitsPerMs;

    if (windowMs > 0.0 && threadMs > 0.0) {
      Logger::info(str::format("VR: CPU time in the window: game thread ",
        fixed(threadMs / double(m_stats.frames())), " ms per frame (",
        std::lround(100.0 * threadMs / windowMs), " % of the time, ",
        std::lround(100.0 * kernelMs / threadMs), " % of it in the kernel), whole process ",
        fixed(processMs / windowMs), " cores busy on average"));
    }

    startStatsWindow();
  }


  std::string VrOpenVrBackend::manifestPath() {
    std::wstring path = vrModuleDirectory();

    if (path.empty())
      return std::string();

    path += L"fnvvr\\actions.json";

    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
      Logger::warn(str::format("VR: The action manifest ", str::fromws(path.c_str()),
        " was not found. The controllers will do nothing"));
      return std::string();
    }

    return str::fromws(path.c_str());
  }


  bool VrOpenVrBackend::initializeInput() {
    vr::IVRInput* input = m_runtime->input;

    if (!input)
      return false;

    std::string manifest = manifestPath();

    if (manifest.empty())
      return false;

    vr::EVRInputError error = input->SetActionManifestPath(manifest.c_str());

    if (error != vr::VRInputError_None) {
      Logger::err(str::format("VR: SteamVR rejected the action manifest ", manifest, ", error ", int32_t(error)));
      return false;
    }

    bool found = true;

    auto action = [&] (const char* path, uint64_t& handle) {
      vr::EVRInputError result = input->GetActionHandle(path, &handle);

      if (result != vr::VRInputError_None) {
        Logger::err(str::format("VR: The action ", path, " is unknown to SteamVR, error ", int32_t(result)));
        found = false;
      }
    };

    for (uint32_t i = 0; i < 2; i++) {
      if (input->GetActionSetHandle(VrOpenVrActionSets[i], &m_actions.sets[i]) != vr::VRInputError_None) {
        Logger::err(str::format("VR: The action set ", VrOpenVrActionSets[i], " is unknown to SteamVR"));
        found = false;
      }

      action(vrOpenVrStickPath(VrInputContext(i), false), m_actions.move[i]);
      action(vrOpenVrStickPath(VrInputContext(i), true),  m_actions.turn[i]);
    }

    for (uint32_t i = 0; i < VrActionCount; i++)
      action(vrOpenVrActionPath(VrAction(i)), m_actions.digital[i]);

    for (uint32_t i = 0; i < VrHandCount; i++)
      action(vrOpenVrHapticPath(VrHand(i)), m_actions.haptic[i]);

    if (input->GetActionSetHandle(VrOpenVrHandsActionSet, &m_actions.hands) != vr::VRInputError_None) {
      Logger::err(str::format("VR: The action set ", VrOpenVrHandsActionSet, " is unknown to SteamVR"));
      found = false;
    }

    for (uint32_t i = 0; i < VrHandCount; i++) {
      action(vrOpenVrAimPath(VrHand(i)), m_actions.aim[i]);
      action(vrOpenVrSkeletonPath(VrHand(i)), m_actions.skeleton[i]);
    }

    if (found)
      Logger::info(str::format("VR: Controller actions loaded from ", manifest));

    return found;
  }


  void VrOpenVrBackend::shutdownRuntime() {
    if (!m_runtime)
      return;

    if (m_runtime->initialized)
      m_runtime->shutdown();

    if (m_runtime->library)
      FreeLibrary(m_runtime->library);

    m_runtime = nullptr;
  }


  VrVulkanRequirements VrOpenVrBackend::queryVulkanRequirements() {
    VrVulkanRequirements requirements;

    if (!initializeRuntime() || !m_runtime->compositor)
      return requirements;

    uint32_t length = m_runtime->compositor->GetVulkanInstanceExtensionsRequired(nullptr, 0);
    std::vector<char> buffer(length);
    length = m_runtime->compositor->GetVulkanInstanceExtensionsRequired(buffer.data(), length);

    requirements.instanceExtensions = splitExtensions(trimTerminator(std::move(buffer), length));
    return requirements;
  }


  std::vector<std::string> VrOpenVrBackend::queryDeviceExtensions(VkPhysicalDevice physicalDevice) {
    if (!m_runtime || !m_runtime->compositor)
      return { };

    auto* device = reinterpret_cast<VkPhysicalDevice_T*>(physicalDevice);

    uint32_t length = m_runtime->compositor->GetVulkanDeviceExtensionsRequired(device, nullptr, 0);
    std::vector<char> buffer(length);
    length = m_runtime->compositor->GetVulkanDeviceExtensionsRequired(device, buffer.data(), length);

    return splitExtensions(trimTerminator(std::move(buffer), length));
  }


  VkPhysicalDevice VrOpenVrBackend::selectPhysicalDevice(VkInstance instance) {
    if (!m_runtime || !m_runtime->system)
      return VK_NULL_HANDLE;

    uint64_t device = 0;
    m_runtime->system->GetOutputDevice(&device, vr::TextureType_Vulkan,
      reinterpret_cast<VkInstance_T*>(instance));

    return reinterpret_cast<VkPhysicalDevice>(device);
  }


  bool VrOpenVrBackend::beginSession(const VrGraphicsBinding& binding) {
    if (!initializeRuntime() || !m_runtime->compositor)
      return false;

    m_transition = VrEyeTransition::create(binding);

    if (!m_transition)
      return false;

    m_binding = binding;

    // Poses are reported relative to the floor, with the origin where the
    // room set-up of the user puts it
    m_runtime->compositor->SetTrackingSpace(vr::TrackingUniverseStanding);

    // See the class description: frames are handed off by handOffFrame
    m_runtime->compositor->SetExplicitTimingMode(
      vr::VRCompositorTimingMode_Explicit_ApplicationPerformsPostPresentHandoff);
    m_frameStarted = false;

    if (binding.asyncHandOff && binding.lockQueue && binding.releaseQueue)
      startHandOffThread();

    startSession();
    return true;
  }


  bool VrOpenVrBackend::beginPanelSession() {
    if (!initializeRuntime() || !m_runtime->compositor)
      return false;

    m_runtime->compositor->SetTrackingSpace(vr::TrackingUniverseStanding);
    startSession();
    return true;
  }


  void VrOpenVrBackend::startSession() {
    float rate = m_runtime->system->GetFloatTrackedDeviceProperty(
      vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float);

    if (rate <= 0.0f)
      rate = FallbackRefreshRate;

    m_periodNs = int64_t(double(NanosecondsPerSecond) / double(rate));
    m_stats        = VrFrameStats(uint32_t(rate * float(StatsPeriod)), double(m_periodNs) * 1e-6);
    m_statsRunning = false;
    m_frameClock   = FrameClock();
    m_state    = VrSessionState::Running;
  }


  void VrOpenVrBackend::endSession() {
    stopHandOffThread();
    destroyPanels();
    destroyPointers();
    m_transition = nullptr;

    if (m_state != VrSessionState::Idle)
      m_state = VrSessionState::Idle;
  }


  VrSessionState VrOpenVrBackend::sessionState() const {
    return m_state;
  }


  VrExtent VrOpenVrBackend::recommendedEyeExtent() const {
    VrExtent extent;

    if (m_runtime && m_runtime->system)
      m_runtime->system->GetRecommendedRenderTargetSize(&extent.width, &extent.height);

    return extent;
  }


  void VrOpenVrBackend::pollEvents() {
    vr::VREvent_t event;

    while (m_runtime->system->PollNextEvent(&event, sizeof(event))) {
      if (event.eventType == vr::VREvent_Quit && m_state == VrSessionState::Running) {
        Logger::info("VR: SteamVR asked the application to quit");
        m_state = VrSessionState::Stopping;
      }

      if (event.eventType == vr::VREvent_IpdChanged)
        Logger::info(str::format("VR: The IPD changed to ", event.data.ipd.ipdMeters * 1000.0f, " mm"));

      // The keyboard's own events, not those of a keyboard shown for an overlay
      bool ownKeyboard = m_keyboardShown && event.data.keyboard.overlayHandle == vr::k_ulOverlayHandleInvalid;

      if (event.eventType == vr::VREvent_KeyboardCharInput && ownKeyboard) {
        const char* input = event.data.keyboard.cNewInput;
        m_keyboardTyped.append(input, strnlen(input, sizeof(event.data.keyboard.cNewInput)));
      }

      // Done closes the keyboard too, its close event follows
      if (event.eventType == vr::VREvent_KeyboardDone && m_keyboardShown) {
        m_keyboardShown = false;
        m_keyboardEvent = VrKeyboardEvent::Done;
      }

      if (event.eventType == vr::VREvent_KeyboardClosed_Global && m_keyboardShown
       && event.data.keyboard.overlayHandle == vr::k_ulOverlayHandleInvalid) {
        m_keyboardShown = false;

        if (m_keyboardEvent == VrKeyboardEvent::None)
          m_keyboardEvent = VrKeyboardEvent::Closed;
      }
    }
  }


  uint32_t VrOpenVrBackend::controllerIndex(VrHand hand) const {
    return m_runtime->system->GetTrackedDeviceIndexForControllerRole(
      hand == VrHand::Left ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand);
  }


  VrFrameTiming VrOpenVrBackend::waitFrame() {
    VrFrameTiming timing;
    timing.predictedPeriod = m_periodNs;

    if (m_state != VrSessionState::Running)
      return timing;

    pollEvents();

    auto waitStart = high_resolution_clock::now();

    // WaitGetPoses must follow the previous frame's hand-off
    finishHandOff();

    // Blocks until the compositor wants the next frame. The poses are
    // converted here and handed out by pollInput. In the explicit timing
    // mode the call does not use the queue, so it needs no lock.
    std::vector<vr::TrackedDevicePose_t> poses(vr::k_unMaxTrackedDeviceCount);
    vr::EVRCompositorError error = m_runtime->compositor->WaitGetPoses(
      poses.data(), uint32_t(poses.size()), nullptr, 0);

    if (error != vr::VRCompositorError_None) {
      if (!m_loggedWait) {
        Logger::err(str::format("VR: WaitGetPoses failed, error ", int32_t(error)));
        m_loggedWait = true;
      }

      m_frameClock   = FrameClock();
      m_statsRunning = false;
      return timing;
    }

    measureFrame(waitStart, high_resolution_clock::now());
    m_frameStarted = true;
    m_poses = VrInputState();

    const vr::TrackedDevicePose_t& head = poses[vr::k_unTrackedDeviceIndex_Hmd];
    m_poses.isHeadTracked = head.bPoseIsValid;

    if (!m_loggedWait) {
      Logger::info(str::format("VR: First poses received, head tracked: ",
        head.bPoseIsValid ? "yes" : "no"));
      m_loggedWait = true;
    }

    if (head.bPoseIsValid)
      m_poses.headPose = poseFromOpenVr(head.mDeviceToAbsoluteTracking);

    for (uint32_t i = 0; i < VrHandCount; i++) {
      uint32_t index = controllerIndex(VrHand(i));

      if (index >= poses.size() || !poses[index].bPoseIsValid)
        continue;

      VrControllerState& controller = m_poses.controllers[i];
      controller.isActive = true;
      controller.gripPose = poseFromOpenVr(poses[index].mDeviceToAbsoluteTracking);
      controller.aimPose  = controller.gripPose;
    }

    // The runtime predicts the poses itself, so the display time is only a
    // frame counter for the callers
    timing.shouldRender         = m_runtime->compositor->CanRenderScene();
    timing.paced                = true;
    timing.predictedDisplayTime = ++m_frameCounter;
    return timing;
  }


  void VrOpenVrBackend::beginFrame() {
    if (m_state != VrSessionState::Running || !m_frameStarted)
      return;

    // Records the GPU time the frame starts at, which the compositor's
    // timing and its choice to reproject rely on
    vr::EVRCompositorError error = m_runtime->compositor->SubmitExplicitTimingData();

    if (error != vr::VRCompositorError_None && !m_loggedTiming) {
      Logger::err(str::format("VR: SubmitExplicitTimingData failed, error ", int32_t(error)));
      m_loggedTiming = true;
    }
  }


  void VrOpenVrBackend::handOffFrame() {
    m_runtime->compositor->PostPresentHandoff();
    m_frameStarted = false;
  }


  void VrOpenVrBackend::handOffSubmittedFrame() {
    if (!m_handOffThread.joinable()) {
      m_frameClock.handOffMs = -1.0;
      handOffFrame();
      return;
    }

    std::unique_lock<dxvk::mutex> lock(m_handOffMutex);
    m_handOffPending = true;
    m_frameStarted   = false;
    m_handOffCond.notify_all();
  }


  void VrOpenVrBackend::finishHandOff() {
    if (!m_handOffThread.joinable())
      return;

    std::unique_lock<dxvk::mutex> lock(m_handOffMutex);
    m_handOffCond.wait(lock, [this] { return !m_handOffPending; });
    m_frameClock.handOffMs = m_handOffMs;
  }


  void VrOpenVrBackend::startHandOffThread() {
    if (m_handOffThread.joinable())
      return;

    m_handOffStop    = false;
    m_handOffPending = false;
    m_handOffThread  = dxvk::thread([this] { runHandOffThread(); });
    Logger::info("VR: Frames are handed off to the compositor on a thread of their own");
  }


  void VrOpenVrBackend::stopHandOffThread() {
    if (!m_handOffThread.joinable())
      return;

    { std::unique_lock<dxvk::mutex> lock(m_handOffMutex);
      m_handOffStop = true;
      m_handOffCond.notify_all();
    }

    m_handOffThread.join();
  }


  void VrOpenVrBackend::runHandOffThread() {
    env::setThreadName("vr-handoff");

    std::unique_lock<dxvk::mutex> lock(m_handOffMutex);

    while (true) {
      m_handOffCond.wait(lock, [this] { return m_handOffPending || m_handOffStop; });

      // A pending frame is handed off before stopping, WaitGetPoses or the
      // end of the session expects it
      if (!m_handOffPending)
        return;

      lock.unlock();

      // The runtime may use the game's queue, which DXVK shares between threads
      m_binding.lockQueue();
      auto start = high_resolution_clock::now();
      m_runtime->compositor->PostPresentHandoff();
      auto end = high_resolution_clock::now();
      m_binding.releaseQueue();

      lock.lock();
      m_handOffMs      = std::chrono::duration<double, std::milli>(end - start).count();
      m_handOffPending = false;
      m_handOffCond.notify_all();
    }
  }


  VrInputState VrOpenVrBackend::pollInput(int64_t displayTime) {
    VrInputState input = m_poses;

    if (m_inputReady)
      readActions(input);

    return input;
  }


  void VrOpenVrBackend::setInputContext(VrInputContext context) {
    m_context = context;
  }


  void VrOpenVrBackend::readActions(VrInputState& input) {
    vr::IVRInput* vrInput = m_runtime->input;
    uint32_t context = uint32_t(m_context);

    // The hand poses are read in every context
    vr::VRActiveActionSet_t active[2] = { };
    active[0].ulActionSet          = m_actions.sets[context];
    active[0].ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle;
    active[1].ulActionSet          = m_actions.hands;
    active[1].ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle;

    vr::EVRInputError error = vrInput->UpdateActionState(active, sizeof(active[0]), 2);

    if (error != vr::VRInputError_None) {
      if (!m_loggedInputError) {
        Logger::err(str::format("VR: Reading the controller actions failed, error ", int32_t(error)));
        m_loggedInputError = true;
      }

      return;
    }

    // Actions only mean something while a controller drives them
    input.hasActions = input.controllers[0].isActive || input.controllers[1].isActive;

    for (uint32_t i = 0; i < VrActionCount; i++) {
      if (uint32_t(vrOpenVrActionContext(VrAction(i))) != context)
        continue;

      vr::InputDigitalActionData_t data = { };

      if (vrInput->GetDigitalActionData(m_actions.digital[i], &data, sizeof(data), vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None)
        input.actions.setPressed(VrAction(i), data.bActive && data.bState);
    }

    auto readStick = [vrInput] (uint64_t handle) {
      vr::InputAnalogActionData_t data = { };
      VrVector2 result;

      if (vrInput->GetAnalogActionData(handle, &data, sizeof(data), vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None && data.bActive)
        result = { data.x, data.y };

      return result;
    };

    input.actions.move = readStick(m_actions.move[context]);
    input.actions.turn = readStick(m_actions.turn[context]);

    // The aim pose is where the controller points from. Without it the
    // controller's own pose is used, which points slightly differently.
    float predictedSeconds = secondsToPhotons();

    for (uint32_t i = 0; i < VrHandCount; i++) {
      VrControllerState& controller = input.controllers[i];
      vr::InputPoseActionData_t data = { };

      if (controller.isActive
       && vrInput->GetPoseActionDataRelativeToNow(m_actions.aim[i], vr::TrackingUniverseStanding, predictedSeconds,
            &data, sizeof(data), vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None
       && data.bActive && data.pose.bPoseIsValid)
        controller.aimPose = poseFromOpenVr(data.pose.mDeviceToAbsoluteTracking);
    }

    // The runtime's hand animation smooths the controller's finger sensors
    for (uint32_t i = 0; i < VrHandCount; i++) {
      VrControllerState& controller = input.controllers[i];
      vr::InputSkeletalActionData_t skeleton = { };
      vr::VRSkeletalSummaryData_t summary = { };

      controller.hasFingers = controller.isActive
        && vrInput->GetSkeletalActionData(m_actions.skeleton[i], &skeleton, sizeof(skeleton)) == vr::VRInputError_None
        && skeleton.bActive
        && vrInput->GetSkeletalSummaryData(m_actions.skeleton[i], vr::VRSummaryType_FromAnimation, &summary) == vr::VRInputError_None;

      for (uint32_t f = 0; controller.hasFingers && f < VrFingerCount; f++)
        controller.fingerCurl[f] = std::max(0.0f, std::min(1.0f, summary.flFingerCurl[f]));

      if (controller.hasFingers && !m_loggedFingers[i]) {
        Logger::info(str::format("VR: The ", i == uint32_t(VrHand::Left) ? "left" : "right",
          " hand's finger curls are read from SteamVR's hand skeleton"));
        m_loggedFingers[i] = true;
      }
    }
  }


  float VrOpenVrBackend::secondsToPhotons() const {
    vr::IVRSystem* system = m_runtime->system;

    float sinceVsync = 0.0f;
    uint64_t frame = 0;
    system->GetTimeSinceLastVsync(&sinceVsync, &frame);

    float toPhotons = system->GetFloatTrackedDeviceProperty(
      vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SecondsFromVsyncToPhotons_Float);

    float period = float(double(m_periodNs) * 1e-9);
    return std::max(0.0f, period - sinceVsync + toPhotons);
  }


  std::array<VrEyeView, VrEyeCount> VrOpenVrBackend::locateViews(int64_t displayTime) {
    std::array<VrEyeView, VrEyeCount> views;

    if (!m_runtime || !m_runtime->system)
      return views;

    for (uint32_t i = 0; i < VrEyeCount; i++) {
      vr::EVREye eye = i == uint32_t(VrEye::Left) ? vr::Eye_Left : vr::Eye_Right;

      VrPose eyeInHead = poseFromOpenVr(getEyeToHeadTransform(m_runtime->system, eye));
      views[i].pose = vrCompose(m_poses.headPose, eyeInHead);

      float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
      m_runtime->system->GetProjectionRaw(eye, &left, &right, &top, &bottom);
      views[i].fov = vrFovFromProjectionRaw(left, right, top, bottom);
    }

    return views;
  }


  bool VrOpenVrBackend::submitFrame(const VrFrameSubmission& frame) {
    if (m_state != VrSessionState::Running || !m_transition)
      return false;

    // OpenVR takes whole images, not layers of an array
    for (const VrEyeImage& eye : frame.images) {
      if (eye.arrayLayer != 0 || eye.image == VK_NULL_HANDLE)
        return false;
    }

    if (!m_loggedSubmit) {
      Logger::info(str::format("VR: Submitting the first frame, eye image ",
        frame.images[0].extent.width, "x", frame.images[0].extent.height,
        ", format ", uint32_t(frame.images[0].format), ", layout ", uint32_t(frame.images[0].layout)));
    }

    m_frameClock.submitStart = high_resolution_clock::now();
    m_frameClock.syncMs      = frame.syncMs;

    if (!m_transition->toTransferSource(frame.images.data(), VrEyeCount))
      return false;

    m_frameClock.imagesReady = high_resolution_clock::now();

    if (!m_loggedSubmit)
      Logger::info("VR: Eye images ready for the compositor");

    bool accepted = true;

    for (uint32_t i = 0; i < VrEyeCount; i++) {
      vr::VRVulkanTextureData_t data = { };
      fillTextureData(frame.images[i], data);

      // The game writes final display values, so the image is gamma encoded
      vr::Texture_t texture = { &data, vr::TextureType_Vulkan, vr::ColorSpace_Gamma };

      vr::EVRCompositorError error = m_runtime->compositor->Submit(
        i == uint32_t(VrEye::Left) ? vr::Eye_Left : vr::Eye_Right, &texture, nullptr, vr::Submit_Default);

      if (error != vr::VRCompositorError_None) {
        Logger::err(str::format("VR: The compositor rejected an eye image, error ", int32_t(error)));
        accepted = false;
      }
    }

    m_frameClock.eyesSubmitted = high_resolution_clock::now();

    // Both eyes are in, the compositor may start on the frame
    handOffSubmittedFrame();

    m_frameClock.submitted   = high_resolution_clock::now();
    m_frameClock.isSubmitted = true;

    if (!m_loggedSubmit) {
      Logger::info("VR: First frame handed to the compositor");
      m_loggedSubmit = true;
    }

    // Always put the images back, DXVK still owns them
    return m_transition->restore(frame.images.data(), VrEyeCount) && accepted;
  }


  void VrOpenVrBackend::fillTextureData(const VrEyeImage& image, vr::VRVulkanTextureData_t& data) const {
    data.m_nImage            = uint64_t(image.image);
    data.m_pDevice           = reinterpret_cast<VkDevice_T*>(m_binding.device);
    data.m_pPhysicalDevice   = reinterpret_cast<VkPhysicalDevice_T*>(m_binding.physicalDevice);
    data.m_pInstance         = reinterpret_cast<VkInstance_T*>(m_binding.instance);
    data.m_pQueue            = reinterpret_cast<VkQueue_T*>(m_binding.queue);
    data.m_nQueueFamilyIndex = m_binding.queueFamilyIndex;
    data.m_nWidth            = image.extent.width;
    data.m_nHeight           = image.extent.height;
    data.m_nFormat           = uint32_t(image.format);
    data.m_nSampleCount      = 1;
  }


  void VrOpenVrBackend::submitEmptyFrame(int64_t displayTime) {
    // The implicit mode hands off every frame in WaitGetPoses, submitted
    // or not, and the explicit mode does the same here
    if (m_state == VrSessionState::Running && m_frameStarted)
      handOffFrame();

    m_frameClock.isSubmitted = false;
  }


  bool VrOpenVrBackend::submitPanel(VrPanelId id, const VrPanelSubmission& panel) {
    if (m_state != VrSessionState::Running || !m_transition || !createPanel(id))
      return false;

    if (panel.image.arrayLayer != 0 || panel.image.image == VK_NULL_HANDLE)
      return false;

    vr::IVROverlay* overlay = m_runtime->overlay;
    Panel& state = m_panels[uint32_t(id)];

    vr::HmdMatrix34_t transform = { };
    vrPoseToMatrix34(panel.pose, transform.m);

    if (panel.anchor == VrPanelAnchor::Head)
      overlay->SetOverlayTransformTrackedDeviceRelative(state.handle, vr::k_unTrackedDeviceIndex_Hmd, &transform);
    else
      overlay->SetOverlayTransformAbsolute(state.handle, vr::TrackingUniverseStanding, &transform);

    overlay->SetOverlayWidthInMeters(state.handle, panel.width);

    if (panel.premultiplied != state.premultiplied) {
      overlay->SetOverlayFlag(state.handle, vr::VROverlayFlags_IsPremultiplied, panel.premultiplied);
      state.premultiplied = panel.premultiplied;
    }

    auto start = high_resolution_clock::now();

    if (!m_transition->toTransferSource(&panel.image, 1))
      return false;

    vr::VRVulkanTextureData_t data = { };
    fillTextureData(panel.image, data);

    vr::Texture_t texture = { &data, vr::TextureType_Vulkan, vr::ColorSpace_Gamma };
    vr::EVROverlayError error = overlay->SetOverlayTexture(state.handle, &texture);

    m_frameClock.panelMs += panel.syncMs
      + std::chrono::duration<double, std::milli>(high_resolution_clock::now() - start).count();

    if (error == vr::VROverlayError_None && !state.visible) {
      error = overlay->ShowOverlay(state.handle);
      state.visible = error == vr::VROverlayError_None;
    }

    if (!state.logged) {
      if (error == vr::VROverlayError_None) {
        Logger::info(str::format("VR: Showing the panel ", PanelOverlayKeys[uint32_t(id)], ", image ",
          panel.image.extent.width, "x", panel.image.extent.height, ", ", panel.width, " m wide"));
      } else {
        Logger::err(str::format("VR: The image of the panel ", PanelOverlayKeys[uint32_t(id)],
          " was rejected, error ", int32_t(error)));
      }

      state.logged = true;
    }

    return m_transition->restore(&panel.image, 1) && error == vr::VROverlayError_None;
  }


  void VrOpenVrBackend::hidePanel(VrPanelId id) {
    Panel& state = m_panels[uint32_t(id)];

    if (!state.visible)
      return;

    m_runtime->overlay->HideOverlay(state.handle);
    state.visible = false;
  }


  bool VrOpenVrBackend::submitPanelPixels(VrPanelId id, const VrPanelPixels& panel) {
    if (m_state != VrSessionState::Running || !panel.rgba || !createPanel(id))
      return false;

    vr::IVROverlay* overlay = m_runtime->overlay;
    Panel& state = m_panels[uint32_t(id)];

    vr::HmdMatrix34_t transform = { };
    vrPoseToMatrix34(panel.pose, transform.m);

    overlay->SetOverlayTransformAbsolute(state.handle, vr::TrackingUniverseStanding, &transform);
    overlay->SetOverlayWidthInMeters(state.handle, panel.metres);

    // SteamVR's interface takes a non-const pointer, and only reads the pixels
    vr::EVROverlayError error = overlay->SetOverlayRaw(state.handle,
      const_cast<uint8_t*>(panel.rgba), panel.width, panel.height, 4);

    if (error == vr::VROverlayError_None && !state.visible) {
      error = overlay->ShowOverlay(state.handle);
      state.visible = error == vr::VROverlayError_None;
    }

    if (error != vr::VROverlayError_None)
      Logger::err(str::format("VR: The image of the panel ", PanelOverlayKeys[uint32_t(id)],
        " was refused, error ", int32_t(error)));

    return error == vr::VROverlayError_None;
  }


  bool VrOpenVrBackend::createPanel(VrPanelId id) {
    Panel& state = m_panels[uint32_t(id)];

    if (state.handle)
      return true;

    if (state.failed || !m_runtime || !m_runtime->overlay)
      return false;

    vr::VROverlayHandle_t handle = vr::k_ulOverlayHandleInvalid;
    vr::EVROverlayError error = m_runtime->overlay->CreateOverlay(
      PanelOverlayKeys[uint32_t(id)], PanelOverlayNames[uint32_t(id)], &handle);

    if (error != vr::VROverlayError_None || handle == vr::k_ulOverlayHandleInvalid) {
      Logger::err(str::format("VR: The overlay of the panel ", PanelOverlayKeys[uint32_t(id)],
        " could not be created, error ", int32_t(error)));
      state.failed = true;
      return false;
    }

    m_runtime->overlay->SetOverlaySortOrder(handle, uint32_t(id));
    state.handle = handle;
    return true;
  }


  void VrOpenVrBackend::destroyPanels() {
    for (Panel& state : m_panels) {
      if (state.handle)
        m_runtime->overlay->DestroyOverlay(state.handle);

      state.handle  = 0;
      state.visible = false;
      state.premultiplied = false;
    }
  }


  void VrOpenVrBackend::showPointer(VrHand hand, const VrPointerSubmission& pointer) {
    if (m_state != VrSessionState::Running || pointer.length <= 0.0f || !createPointer(hand))
      return;

    vr::IVROverlay* overlay = m_runtime->overlay;
    Pointer& state = m_pointers[uint32_t(hand)];

    vr::HmdMatrix34_t transform = { };
    vrComputeBeamTransform(pointer.origin, pointer.length, m_poses.headPose.position, transform.m);

    // The overlay is PointerWidth wide, and its height follows the image's
    // shape and the texel aspect
    float aspect = PointerWidth * float(PointerImageHeight) / (float(PointerImageWidth) * pointer.length);

    overlay->SetOverlayTransformAbsolute(state.handle, vr::TrackingUniverseStanding, &transform);
    overlay->SetOverlayTexelAspect(state.handle, aspect);
    overlay->SetOverlayAlpha(state.handle, pointer.active ? PointerActiveAlpha : PointerInactiveAlpha);

    if (!state.visible)
      state.visible = overlay->ShowOverlay(state.handle) == vr::VROverlayError_None;
  }


  void VrOpenVrBackend::hidePointer(VrHand hand) {
    Pointer& state = m_pointers[uint32_t(hand)];

    if (!state.visible)
      return;

    m_runtime->overlay->HideOverlay(state.handle);
    state.visible = false;
  }


  bool VrOpenVrBackend::createPointer(VrHand hand) {
    Pointer& state = m_pointers[uint32_t(hand)];

    if (state.handle)
      return true;

    if (state.failed || !m_runtime || !m_runtime->overlay)
      return false;

    vr::IVROverlay* overlay = m_runtime->overlay;
    vr::VROverlayHandle_t handle = vr::k_ulOverlayHandleInvalid;
    vr::EVROverlayError error = overlay->CreateOverlay(
      PointerOverlayKeys[uint32_t(hand)], PointerOverlayNames[uint32_t(hand)], &handle);

    if (error != vr::VROverlayError_None || handle == vr::k_ulOverlayHandleInvalid) {
      Logger::err(str::format("VR: The overlay of the laser ", PointerOverlayKeys[uint32_t(hand)],
        " could not be created, error ", int32_t(error)));
      state.failed = true;
      return false;
    }

    if (m_pointerImage.empty()) {
      m_pointerImage.resize(PointerImageWidth * PointerImageHeight * 4);

      for (uint32_t y = 0; y < PointerImageHeight; y++) {
        for (uint32_t x = 0; x < PointerImageWidth; x++) {
          // Opaque in the middle, fading towards both edges
          float across = (float(x) + 0.5f) / float(PointerImageWidth) * 2.0f - 1.0f;
          float alpha  = std::max(0.0f, 1.0f - across * across);

          uint8_t* texel = &m_pointerImage[(y * PointerImageWidth + x) * 4];
          texel[0] = PointerColor[0];
          texel[1] = PointerColor[1];
          texel[2] = PointerColor[2];
          texel[3] = uint8_t(std::lround(alpha * 255.0f));
        }
      }
    }

    overlay->SetOverlaySortOrder(handle, PointerSortOrder);
    overlay->SetOverlayWidthInMeters(handle, PointerWidth);
    error = overlay->SetOverlayRaw(handle, m_pointerImage.data(), PointerImageWidth, PointerImageHeight, 4);

    if (error != vr::VROverlayError_None) {
      Logger::err(str::format("VR: The image of the laser ", PointerOverlayKeys[uint32_t(hand)],
        " was rejected, error ", int32_t(error)));
      overlay->DestroyOverlay(handle);
      state.failed = true;
      return false;
    }

    Logger::info(str::format("VR: Created the laser ", PointerOverlayKeys[uint32_t(hand)]));
    state.handle = handle;
    return true;
  }


  void VrOpenVrBackend::destroyPointers() {
    for (Pointer& state : m_pointers) {
      if (state.handle)
        m_runtime->overlay->DestroyOverlay(state.handle);

      state.handle  = 0;
      state.visible = false;
    }
  }


  void VrOpenVrBackend::applyHaptic(VrHand hand, float amplitude, int64_t durationNs) {
    if (!m_inputReady)
      return;

    // Frequency the Touch controllers vibrate at by default
    constexpr float FrequencyHz = 160.0f;

    float seconds  = float(std::max<int64_t>(0, durationNs)) * 1e-9f;
    float strength = std::max(0.0f, std::min(1.0f, amplitude));

    if (seconds > 0.0f && strength > 0.0f)
      m_runtime->input->TriggerHapticVibrationAction(m_actions.haptic[uint32_t(hand)], 0.0f, seconds, FrequencyHz, strength,
        vr::k_ulInvalidInputValueHandle);
  }


  void VrOpenVrBackend::fadeView(float seconds, float opacity) {
    if (m_state != VrSessionState::Running || !m_runtime || !m_runtime->compositor)
      return;

    m_runtime->compositor->FadeToColor(std::max(0.0f, seconds), 0.0f, 0.0f, 0.0f,
      std::max(0.0f, std::min(1.0f, opacity)));
  }


  bool VrOpenVrBackend::showKeyboard(const VrKeyboardRequest& request) {
    if (m_state != VrSessionState::Running || !m_runtime || !m_runtime->overlay)
      return false;

    // Minimal: each key is sent as it is pressed, and the application's
    // field shows the text. Not modal, so that a click beside it does not
    // close it.
    vr::EVROverlayError error = m_runtime->overlay->ShowKeyboard(
      vr::k_EGamepadTextInputModeNormal, vr::k_EGamepadTextInputLineModeSingleLine, vr::KeyboardFlag_Minimal,
      request.description.c_str(), request.maxLength, "", KeyboardUserValue);

    if (error != vr::VROverlayError_None) {
      Logger::warn(str::format("VR: SteamVR could not show its keyboard, error ", int32_t(error)));
      return false;
    }

    m_keyboardShown = true;
    m_keyboardEvent = VrKeyboardEvent::None;
    m_keyboardTyped.clear();
    return true;
  }


  void VrOpenVrBackend::hideKeyboard() {
    if (!m_keyboardShown)
      return;

    m_keyboardShown = false;
    m_keyboardEvent = VrKeyboardEvent::None;
    m_runtime->overlay->HideKeyboard();
  }


  VrKeyboardEvent VrOpenVrBackend::takeKeyboardEvent(std::string& typed) {
    VrKeyboardEvent event = m_keyboardEvent;
    m_keyboardEvent = VrKeyboardEvent::None;

    typed = std::move(m_keyboardTyped);
    m_keyboardTyped.clear();
    return event;
  }

}
