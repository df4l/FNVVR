#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "vr_render_profile.h"

namespace dxvk {

  /**
   * \brief Parts of a stereo frame timed on the GPU outside of the eyes
   */
  enum class VrGpuFramePart : uint32_t {
    /// The whole stereo frame, from before the scope to the eye submission
    Frame,
    /// The world drawn a third time for the scope's lens
    Scope,
    /// Faders, HUD composition and panels, after both eyes
    Panels,
  };

  constexpr uint32_t VrGpuFramePartCount = uint32_t(VrGpuFramePart::Panels) + 1;

  /**
   * \brief What a pair of GPU timestamps measures
   *
   * A pass of one eye or a part of the whole frame, packed in one value so
   * that a frame's timestamps can be kept in a flat list.
   */
  class VrGpuSection {

  public:

    static VrGpuSection pass(uint32_t eye, VrRenderPass pass) {
      return VrGpuSection(VrGpuFramePartCount + eye * VrRenderPassCount + uint32_t(pass));
    }

    static VrGpuSection part(VrGpuFramePart part) {
      return VrGpuSection(uint32_t(part));
    }

    bool isPart() const { return m_value < VrGpuFramePartCount; }

    VrGpuFramePart part() const { return VrGpuFramePart(m_value); }

    uint32_t eye() const { return (m_value - VrGpuFramePartCount) / VrRenderPassCount; }

    VrRenderPass pass() const { return VrRenderPass((m_value - VrGpuFramePartCount) % VrRenderPassCount); }

  private:

    explicit VrGpuSection(uint32_t value) : m_value(value) { }

    uint32_t m_value;

  };

  /**
   * \brief Milliseconds between two GPU timestamps
   *
   * \param [in] begin First timestamp, in ticks
   * \param [in] end Second timestamp, in ticks
   * \param [in] frequency Ticks per second
   * \returns The time, or a negative value if the pair is not usable
   */
  double vrGpuTimestampMs(uint64_t begin, uint64_t end, uint64_t frequency);

  /**
   * \brief GPU time of the passes and parts of the frame, over a window
   *
   * Frames are added once all of their timestamps are known, a few frames
   * after they were drawn. Times of a section measured several times in a
   * frame are summed.
   */
  class VrGpuProfile {

  public:

    explicit VrGpuProfile(uint32_t windowFrames);

    void add(VrGpuSection section, double ms);

    /**
     * \brief Closes the current frame
     * \returns \c true once the window holds all of its frames
     */
    bool endFrame();

    void reset();

    uint32_t frames() const { return m_eyes.frames(); }

    const VrRenderProfile& eyes() const { return m_eyes; }

    double averageMs(VrGpuFramePart part) const;

    double maxMs(VrGpuFramePart part) const;

    /**
     * \brief Average time per frame of the frame outside of the eyes, the
     *    scope and the panels
     *
     * GPU idle time inside the frame counts here, as in every section.
     */
    double otherMs() const;

    /**
     * \brief One log line for the window
     *
     * \param [in] untimed Frames drawn but not timed in the window
     */
    std::string describe(uint32_t untimed) const;

  private:

    struct PartStats {
      double sum = 0.0;
      double max = 0.0;
    };

    uint32_t        m_windowFrames;
    VrRenderProfile m_eyes;

    std::array<double,    VrGpuFramePartCount> m_current = { };
    std::array<PartStats, VrGpuFramePartCount> m_parts   = { };

  };

}
