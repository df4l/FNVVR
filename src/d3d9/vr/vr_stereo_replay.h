#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "../d3d9_include.h"

#include "../../util/util_time.h"

#include "vr_game_camera.h"
#include "vr_stereo_replay_math.h"

namespace dxvk {

  class D3D9DeviceEx;

  /**
   * \brief Draws the right eye from the left eye's commands
   *
   * Stereo replay, findings/performance.md §15 and §18 (D1), the
   * performance mode's first distance level. The game draws its world for
   * the left eye only. DXVK keeps the commands of the left eye's world
   * render, from the end of the shadow maps to the end of
   * TESMain::DrawWorldStandard, and runs them again in place of the right
   * eye's world render, with every vertex shader moving its clip position to
   * the right eye (D3D9DeviceEx::ReplayVrStereoCapture). The image space
   * passes are drawn as they are: they work in screen space. What comes
   * after the world render (interface, eye copy, the VR layer's own
   * drawings) is still drawn for each eye.
   *
   * While the left eye's world is kept, the game culls its objects with a
   * view that holds both eyes (vrComputeStereoCullingView), so that what only
   * the right eye sees is drawn too. Water reflections, water depth maps and
   * specular highlights, which would show the left eye's view, are turned
   * off by the performance mode's levers at the same level.
   *
   * A frame is drawn for both eyes as before when the left eye's world was
   * not drawn with the left eye's camera (checked against the renderer's
   * matrices), or when the right eye's world render does not match the
   * left eye's. One summary line is logged per minute of replay.
   *
   * Only used on the game's main thread.
   */
  class VrStereoReplay {

  public:

    static constexpr uint32_t NoEye = ~0u;

    ~VrStereoReplay();

    VrStereoReplay             (const VrStereoReplay&) = delete;
    VrStereoReplay& operator = (const VrStereoReplay&) = delete;

    /**
     * \brief Takes the game's call of its world render
     *
     * \param [in] device The game's device
     * \param [in] test What d3d9.vrStereoReplayTest asks for
     * \returns \c nullptr if a call or the device's support is missing
     */
    static std::unique_ptr<VrStereoReplay> install(IDirect3DDevice9* device, VrStereoReplayTest test);

    /**
     * \brief Whether d3d9.vrStereoReplayTest replays every frame
     */
    bool forced() const {
      return m_test != VrStereoReplayTest::None;
    }

    /**
     * \brief Sets what runs once the right eye's world is replayed
     *
     * Called before the right eye's interface pass, in place of its world.
     */
    void setOnReplay(std::function<void()> callback) {
      m_onReplay = std::move(callback);
    }

    /**
     * \brief Starts a stereo frame
     *
     * \param [in] active The right eye is to be drawn from the left eye's
     *    commands this frame
     * \param [in] eyes The eyes' cameras in game space, as given to the game
     */
    void beginFrame(bool active, const std::array<VrStereoEyeCamera, 2>& eyes);

    /**
     * \brief Whether the frame started by beginFrame replays the right eye
     */
    bool active() const {
      return m_active;
    }

    /**
     * \brief Starts drawing an eye
     * \param [in] eye 0 for the left eye, 1 for the right eye
     */
    void beginEye(uint32_t eye);

    /**
     * \brief Ends the eye started by beginEye
     */
    void endEye();

    /**
     * \brief Called once the shadow maps of the current eye are drawn
     */
    void afterShadows();

    /**
     * \brief Called before the image space passes of the current eye
     */
    void beforeScreenPasses();

    /**
     * \brief Ends a frame in which both eyes were drawn
     */
    void endFrame();

  private:

    /// Why a frame was drawn for both eyes
    enum class Refusal : uint32_t {
      NoCapture,
      Camera,
      Arguments,
      Count
    };

    struct Window {
      uint32_t frames   = 0;
      uint32_t replayed = 0;
      uint32_t widened  = 0;
      std::array<uint32_t, uint32_t(Refusal::Count)> refused = { };
    };

    VrStereoReplay(D3D9DeviceEx* device, VrStereoReplayTest test);

    D3D9DeviceEx*      m_device;
    VrStereoReplayTest m_test;

    std::array<VrStereoEyeCamera, 2> m_eyes = { };
    bool     m_active    = false;
    uint32_t m_eye       = NoEye;

    bool     m_capturing = false;
    bool     m_captured  = false;
    bool     m_hasMatrix = false;
    void*    m_capturedDestination = nullptr;
    uint32_t m_capturedPipBoyMode  = 0;
    std::array<float, 16> m_matrix = { };

    // The view the left eye's objects are culled with while it is kept
    bool             m_hasCullingView = false;
    VrGameCameraPose m_cullingPose    = { };
    VrGameFrustum    m_cullingFrustum = { };
    VrGameCamera     m_cullingCamera;

    std::function<void()> m_onReplay;

    Window   m_window;
    high_resolution_clock::time_point m_windowStart;
    bool     m_loggedCamera  = false;
    bool     m_loggedReplay  = false;
    bool     m_loggedCulling = false;
    std::string m_cameraReport;

    void endCapture(void* destination, uint32_t pipBoyMode);

    bool replay(void* destination, uint32_t pipBoyMode);

    void startScreenFrame();

    void refuse(Refusal reason);

    void logWindow();

    std::string describeCamera(const VrRendererCamera& camera) const;

    void logFirstReplay() const;

    bool widensCulling(void* camera);

    static void __fastcall drawWorldHook(void* main, void* unused,
      void* destination, uint32_t pipBoyMode);

    static void __cdecl accumulateHook(void* camera, void* scene, void* culler);

  };

}
