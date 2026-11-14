#pragma once

#include <array>
#include <cstdint>

namespace dxvk {

  /**
   * \brief Parts of the game's world render timed per eye
   */
  enum class VrRenderPass : uint32_t {
    /// The whole frame drawn for the eye (Main::Swap)
    Eye,
    /// Shadow maps of the actors and the Pip-Boy's offscreen menu
    Shadows,
    /// Water reflections
    Water,
    /// The world itself
    Scene,
    /// The first-person model
    FirstPerson,
    /// Image space effects (HDR, bloom, depth of field, ...)
    ImageSpace,
    /// The interface drawn into the headset's panel textures (GPU timings only)
    Interface,
    /// The finished 3D image copied into the eye texture (GPU timings only)
    EyeCopy,
  };

  constexpr uint32_t VrRenderPassCount = uint32_t(VrRenderPass::EyeCopy) + 1;

  /**
   * \brief Time of the render passes per eye, over a window of frames
   *
   * Used for the CPU time of the passes and for their GPU time.
   *
   * Times are added while an eye is drawn and summed per frame, so that a
   * pass the game runs more than once in a frame counts once. A frame is a
   * stereo frame: both eyes. A pass that did not run in a frame (or was
   * skipped) adds nothing and is not counted in its frames.
   */
  class VrRenderProfile {

  public:

    static constexpr uint32_t EyeCount = 2;

    /// Time of each pass of each eye in one frame, in milliseconds
    using EyePasses = std::array<std::array<double, VrRenderPassCount>, EyeCount>;

    /**
     * \param [in] windowFrames Frames covered by a window
     */
    explicit VrRenderProfile(uint32_t windowFrames);

    /**
     * \brief Adds time spent in a pass of the current frame
     *
     * \param [in] eye 0 for the left eye, 1 for the right eye
     * \param [in] pass The pass
     * \param [in] ms Time in milliseconds
     */
    void add(uint32_t eye, VrRenderPass pass, double ms);

    /**
     * \brief Closes the current frame
     * \returns \c true once the window holds all of its frames
     */
    bool endFrame();

    void reset();

    uint32_t frames() const { return m_frames; }

    /**
     * \brief Average time of a pass per frame, frames without it included
     */
    double averageMs(uint32_t eye, VrRenderPass pass) const;

    /**
     * \brief Longest time of a pass in one frame
     */
    double maxMs(uint32_t eye, VrRenderPass pass) const;

    /**
     * \brief Number of frames in which a pass ran
     */
    uint32_t passFrames(uint32_t eye, VrRenderPass pass) const;

    /**
     * \brief Average time per frame of the eye outside the timed passes
     */
    double otherMs(uint32_t eye) const;

    /**
     * \brief Times of the frame closed last by endFrame
     */
    const EyePasses& lastFrame() const { return m_last; }

  private:

    struct PassStats {
      double   sum    = 0.0;
      double   max    = 0.0;
      uint32_t frames = 0;
    };

    uint32_t  m_windowFrames;
    uint32_t  m_frames = 0;
    EyePasses m_current = { };
    EyePasses m_last    = { };

    std::array<std::array<PassStats, VrRenderPassCount>, EyeCount> m_stats = { };

    const PassStats& stats(uint32_t eye, VrRenderPass pass) const {
      return m_stats[eye][uint32_t(pass)];
    }

  };

}
