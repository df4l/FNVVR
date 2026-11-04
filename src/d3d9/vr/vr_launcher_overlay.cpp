#include <algorithm>
#include <cctype>
#include <chrono>
#include <vector>

#include "../../util/log/log.h"
#include "../../util/util_env.h"
#include "../../util/util_string.h"

#include "vr_backend_factory.h"
#include "vr_launcher_overlay.h"
#include "vr_launcher_windows.h"
#include "vr_math.h"

namespace dxvk {

  namespace {

    constexpr const char* LauncherExecutable = "falloutnvlauncher.exe";

    /// Time between two pictures of the windows, in seconds
    constexpr float CaptureInterval = 1.0f / 15.0f;

    /// A runtime that does not wait for a frame is slowed down to this, in seconds
    constexpr float MinFrameTime = 1.0f / 120.0f;

    /// Time between two lines of statistics in the log, in seconds
    constexpr float StatsInterval = 10.0f;

    using Clock = std::chrono::high_resolution_clock;

    float secondsSince(Clock::time_point start) {
      return std::chrono::duration<float>(Clock::now() - start).count();
    }

  }


  bool VrLauncherOverlay::isLauncherProcess() {
    std::string name = env::getExeName();
    std::transform(name.begin(), name.end(), name.begin(),
      [] (unsigned char c) { return char(std::tolower(c)); });
    return name == LauncherExecutable;
  }


  VrLauncherOverlay::VrLauncherOverlay(const std::string& backend, float distance, float menuPanelWidth)
  : m_backendName(backend), m_distance(distance), m_menuPanelWidth(menuPanelWidth) { }


  void VrLauncherOverlay::start() {
    if (m_started)
      return;

    m_started      = true;
    m_windowThread = GetCurrentThreadId();

    VrLauncherThreadCall::install();

    HMODULE module = nullptr;

    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&VrLauncherOverlay::threadMain), &module)) {
      Logger::err("VR: Launcher: The DLL could not stay loaded, the launcher stays on the desktop");
      return;
    }

    HANDLE thread = CreateThread(nullptr, 0, &VrLauncherOverlay::threadMain, this, 0, nullptr);

    if (!thread) {
      Logger::err("VR: Launcher: The thread could not start, the launcher stays on the desktop");
      return;
    }

    CloseHandle(thread);
  }


  DWORD WINAPI VrLauncherOverlay::threadMain(void* overlay) {
    static_cast<VrLauncherOverlay*>(overlay)->run();
    return 0;
  }


  void VrLauncherOverlay::run() {
    std::unique_ptr<IVRBackend> backend = vrCreateBackend(m_backendName);

    if (!backend || !backend->beginPanelSession()) {
      Logger::warn("VR: Launcher: No VR session, the launcher stays on the desktop");
      return;
    }

    Logger::info("VR: Launcher: Shown in the headset");
    backend->setInputContext(VrInputContext::Menu);

    VrLauncherCapture     capture(m_windowThread);
    VrLauncherMouseRouter router(m_windowThread);
    VrLauncherThreadCall  windowThread(m_windowThread);
    VrLauncherMouse       mouse;
    VrMenuPointer         pointer;
    VrPanelPlacement      placement;

    std::vector<VrLauncherMouseEvent> pendingEvents;

    bool  placed       = false;
    bool  panelShown   = false;
    float captureTimer = CaptureInterval;
    auto  lastFrame    = Clock::now();

    uint32_t statFrames   = 0;
    float    statWait     = 0.0f;
    float    statWindows  = 0.0f;
    float    statCaptures = 0.0f;
    auto     statStart    = Clock::now();

    while (backend->sessionState() == VrSessionState::Running) {
      auto waitStart = Clock::now();

      VrFrameTiming timing = backend->waitFrame();
      VrInputState  input  = backend->pollInput(timing.predictedDisplayTime);

      statWait += secondsSince(waitStart);

      float dt = secondsSince(lastFrame);

      if (dt < MinFrameTime) {
        Sleep(DWORD((MinFrameTime - dt) * 1000.0f) + 1);
        dt = secondsSince(lastFrame);
      }

      lastFrame = Clock::now();

      if (!placed && input.isHeadTracked) {
        placement = vrLauncherPlacement(vrComputePanelPose(input.headPose, m_distance), m_menuPanelWidth);
        placed    = true;
      }

      VrPointerInput pointerInput;
      pointerInput.controllers = input.controllers;
      pointerInput.panel       = panelShown ? &placement : nullptr;
      pointerInput.dt          = dt;

      if (input.hasActions) {
        pointerInput.triggers[uint32_t(VrHand::Left)]  = input.actions.isPressed(VrAction::MenuLeftTrigger);
        pointerInput.triggers[uint32_t(VrHand::Right)] = input.actions.isPressed(VrAction::MenuRightTrigger);
        pointerInput.wheel = input.actions.turn.y;
      }

      VrPointerState state = pointer.update(pointerInput);

      for (uint32_t i = 0; i < VrHandCount; i++) {
        const VrPointerBeam& beam = state.beams[i];

        if (!beam.visible) {
          backend->hidePointer(VrHand(i));
          continue;
        }

        VrPointerSubmission submission;
        submission.origin = input.controllers[i].aimPose;
        submission.length = beam.length;
        submission.active = beam.active;
        backend->showPointer(VrHand(i), submission);
      }

      for (const VrLauncherMouseEvent& event : mouse.update(state, capture.canvas()))
        pendingEvents.push_back(event);

      captureTimer += dt;

      bool captureDue = placed && captureTimer >= CaptureInterval;
      bool changed    = false;

      // One visit to the launcher's thread per frame, for all that needs it
      if (captureDue || !pendingEvents.empty()) {
        auto windowsStart = Clock::now();

        bool ran = windowThread.run([&] {
          if (captureDue)
            changed = capture.update();

          for (const VrLauncherMouseEvent& event : pendingEvents)
            router.send(event);
        });

        statWindows += secondsSince(windowsStart);

        if (ran) {
          pendingEvents.clear();

          if (captureDue) {
            captureTimer = 0.0f;
            statCaptures += 1.0f;
          }
        } else {
          captureDue = false;
        }
      }

      if (captureDue) {
        if (!capture.hasWindows() && panelShown) {
          backend->hidePanel(VrPanelId::Menu);
          panelShown = false;
        }

        if (capture.hasWindows() && (changed || !panelShown)) {
          VrPanelPixels pixels;
          pixels.rgba   = capture.rgba();
          pixels.width  = VrLauncherCanvasWidth;
          pixels.height = VrLauncherCanvasHeight;
          pixels.pose   = placement.pose;
          pixels.metres = placement.width;

          panelShown = backend->submitPanelPixels(VrPanelId::Menu, pixels);
        }
      }

      statFrames += 1;

      float statTime = secondsSince(statStart);

      if (statTime >= StatsInterval) {
        Logger::info(str::format("VR: Launcher: ", float(statFrames) / statTime, " frames per second, ",
          statCaptures / statTime, " pictures per second, average per frame: waiting for the runtime ",
          statWait * 1000.0f / float(statFrames), " ms, on the launcher's thread ",
          statWindows * 1000.0f / float(statFrames), " ms"));

        statFrames   = 0;
        statWait     = 0.0f;
        statWindows  = 0.0f;
        statCaptures = 0.0f;
        statStart    = Clock::now();
      }
    }

    Logger::info("VR: Launcher: The VR session ended, the launcher stays on the desktop");
    backend->endSession();
  }

}
