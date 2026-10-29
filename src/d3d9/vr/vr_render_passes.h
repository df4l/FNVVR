#pragma once

#include <functional>
#include <memory>

#include "../../util/util_time.h"

#include "vr_game_camera.h"
#include "vr_render_profile.h"

namespace dxvk {

  /**
   * \brief How the game's render passes are drawn per eye
   *
   * Set by \c d3d9.vrShareShadows and \c d3d9.vrHandsDepthTest.
   */
  struct VrRenderPassConfig {
    /// Shadow maps are drawn for the left eye only and reused by the right
    bool shareShadows = true;
    /// The hands and the weapon are hidden by the world in front of them
    bool handsDepthTest = true;
  };

  /**
   * \brief Times the game's render passes per eye and shares some of them
   *
   * The game draws a whole frame per eye (see VrStereoRenderer), so every
   * pass of its world render runs twice, including the ones that do not
   * depend on the eye (findings/render.md, "Passes drawn per eye"):
   *
   * - the actors' shadow maps are drawn from the lights; the eye only
   *   decides which shadows are kept, so they are drawn once, for the left
   *   eye, with the world camera moved to the head and widened to the view
   *   of both eyes, and the right eye uses the same maps;
   * - water reflections are not shared: the water shader reads them at the
   *   pixel's position on the eye's screen, so each eye needs its own.
   *
   * The main loop's calls to the shadow and water passes, the scene, the
   * first-person model and the image space effects are redirected and
   * timed; a summary per eye is logged every few hundred frames.
   *
   * The game clears the depth buffer before it draws the first-person
   * model, so the arms and the weapon are drawn over everything. In VR
   * they sit at the controllers in the world, so the clear is skipped: an
   * object held in the hand hides the fingers behind it, and a wall hides
   * a hand pushed into it.
   *
   * Only used on the game's main thread.
   */
  class VrRenderPasses {

  public:

    /**
     * \brief Gives the view the shared passes are drawn for
     *
     * Returns \c false to keep the left eye's camera.
     */
    using ViewFn = std::function<bool (VrGameCameraPose& pose, VrGameFrustum& frustum)>;

    static constexpr uint32_t NoEye = ~0u;

    ~VrRenderPasses();

    /**
     * \brief Redirects the world render's calls
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrRenderPasses> install(const VrRenderPassConfig& config, ViewFn view);

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
     * \brief Ends a frame in which both eyes were drawn
     */
    void endFrame();

  private:

    VrRenderPasses(const VrRenderPassConfig& config, ViewFn view);

    VrRenderPassConfig m_config;
    ViewFn             m_view;
    VrGameCamera       m_camera;
    VrRenderProfile    m_profile;

    uint32_t m_eye = NoEye;
    high_resolution_clock::time_point m_eyeStart;

    bool     m_shadowsDrawnForLeft = false;
    uint32_t m_shadowsReused       = 0;

    template<typename Fn>
    uintptr_t runPass(VrRenderPass pass, Fn&& fn);

    template<typename Fn>
    uintptr_t runShadows(Fn&& fn);

    bool sharesShadows() const;

    void logWindow();

    static uintptr_t __fastcall waterHook(void* main, void* unused);

    static uintptr_t __fastcall shadowsHook(void* main, void* unused);

    static uintptr_t __fastcall sceneHook(void* main, void* unused,
      uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d);

    static uintptr_t __fastcall firstPersonHook(void* main, void* unused,
      uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d);

    static uintptr_t __fastcall imageSpaceHook(void* main, void* unused,
      uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d);

    static void __fastcall firstPersonClearHook(void* renderer, void* unused,
      const void* rect, uint32_t flags);

  };

}
