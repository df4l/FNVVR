#pragma once

#include <cstdint>
#include <memory>

namespace dxvk {

  /**
   * \brief Keeps the world passes after the left eye's from repeating its work
   *
   * The game draws its world once per frame and, while it draws it, moves a
   * few things on by the frame's time step: fading objects, the fade of
   * lights and their shadows, the eye's adaptation to light, the black
   * fader of a loading screen, the water's noise and wading ripples. It
   * also does work there that does not depend on the view: the water
   * sounds, the wading actors, the water's noise map. In VR the world is
   * drawn for each eye, and for a scope's lens before them, so all of this
   * ran two or three times per frame: fades and the eye's adaptation went
   * two to three times as fast, and the wading ripples were struck twice.
   *
   * The left eye's pass does it all. During the other passes ("repeated"
   * here), the shader timers' step and the step the fades read are zero,
   * and the work that does not depend on the view is skipped. See
   * findings/stereo-passes.md.
   */
  class VrRepeatedPasses {

  public:

    ~VrRepeatedPasses();

    /**
     * \brief Takes the game's code these passes need
     *
     * \returns \c nullptr if the executable is not the supported version.
     *    Each piece that is not found is logged and left as the game has it.
     */
    static std::unique_ptr<VrRepeatedPasses> install();

    /**
     * \brief Called once per frame of the main loop, before the world is drawn
     */
    void beginFrame();

    /**
     * \brief Starts a world pass that the left eye's pass already did
     *
     * On the game's thread. Passes do not nest.
     */
    void beginRepeat();

    /**
     * \brief Ends the pass started by \ref beginRepeat
     */
    void endRepeat();

  private:

    VrRepeatedPasses() = default;

    bool  m_timeStepFound = false;
    bool  m_fadeStepFound = false;
    bool  m_repeating     = false;
    float m_savedTimeStep = 0.0f;

    void installFadeStep();

    void installSkips();

    static float __fastcall realTimeHook(void* timer, void* unused);

    static void __cdecl screenshotHook(const char* name);

    static void __fastcall waterActorsHook(void* water, void* unused);

    static void __fastcall waterSoundsHook(void* water, void* unused);

    static void __fastcall displaceStencilHook(void* water, void* unused);

    static uintptr_t __fastcall displaceAddHook(void* array, void* unused, void* item);

    static void __fastcall displaceRenderHook(void* manager, void* unused,
      uint32_t effect, void* renderer, void* source, void* target, void* parameter, uint32_t flag);

    static void __fastcall noiseRenderHook(void* manager, void* unused,
      uint32_t effect, void* renderer, void* source, void* target, void* parameter, uint32_t flag);

  };

}
