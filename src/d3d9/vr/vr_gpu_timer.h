#pragma once

#include <array>
#include <memory>
#include <vector>

#include "../d3d9_include.h"

#include "../../util/com/com_pointer.h"

#include "vr_gpu_profile.h"

namespace dxvk {

  /**
   * \brief Measures how long the GPU spends in each part of a stereo frame
   *
   * Writes Direct3D 9 timestamp queries (Vulkan timestamps in DXVK) into
   * the game's command stream before and after each timed section, and
   * reads them without waiting a few frames later, once the GPU has run
   * them. A summary is logged every few hundred frames
   * (\c d3d9.vrGpuTimings).
   *
   * A section's time runs from the GPU reaching its first command to the
   * GPU finishing its last one, so it includes the time the GPU waited for
   * the CPU inside it. In a GPU-bound frame the GPU is always behind and
   * that wait is close to zero.
   *
   * Only used on the game's main thread.
   */
  class VrGpuTimer {

  public:

    static constexpr uint32_t NoToken = ~0u;

    /**
     * \returns \c nullptr if the device has no timestamp queries
     */
    static std::unique_ptr<VrGpuTimer> create(IDirect3DDevice9* device);

    /**
     * \brief Starts timing a stereo frame
     *
     * The frame is not timed when all the frames kept are still waiting
     * for the GPU.
     */
    void beginFrame();

    /**
     * \brief Ends the frame started by beginFrame
     *
     * Reads the frames the GPU has finished and logs the summary of a
     * full window.
     */
    void endFrame();

    /**
     * \brief Drops the frame started by beginFrame
     */
    void cancelFrame();

    /**
     * \brief Starts a section of the current frame
     * \returns What to give to end, or \c NoToken outside of a timed frame
     */
    uint32_t begin(VrGpuSection section);

    /**
     * \brief Ends a section started by begin
     */
    void end(uint32_t token);

  private:

    static constexpr uint32_t NoQuery   = ~0u;
    static constexpr uint32_t SlotCount = 8;

    struct Mark {
      VrGpuSection section;
      uint32_t     begin;
      uint32_t     end;
    };

    struct Slot {
      std::vector<Com<IDirect3DQuery9>> queries;
      std::vector<uint64_t>             values;
      std::vector<Mark>                 marks;
      uint32_t used    = 0;
      uint32_t waits   = 0;
      bool     pending = false;
    };

    VrGpuTimer(IDirect3DDevice9* device, uint64_t frequency);

    IDirect3DDevice9* m_device;
    uint64_t          m_frequency;
    VrGpuProfile      m_profile;

    std::array<Slot, SlotCount> m_slots;

    // Slot the next frame is recorded in; pending slots are the ones before it
    uint32_t m_next       = 0;
    uint32_t m_pending    = 0;
    bool     m_recording  = false;
    uint32_t m_frameToken = NoToken;
    uint32_t m_untimed    = 0;
    bool     m_loggedFailure = false;

    uint32_t issue(Slot& slot);

    void collect();

    bool read(Slot& slot);

    void addFrame(const Slot& slot);

    void dropOldest();

  };

}
