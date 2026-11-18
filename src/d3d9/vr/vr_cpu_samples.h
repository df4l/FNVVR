#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "vr_render_profile.h"

namespace dxvk {

  /**
   * \brief Where the game thread is in the VR frame, stored with each sample
   *
   * Values below \c VrCpuPhaseEyeBase are the phases outside the eyes;
   * an eye's pass is \c vrCpuEyePhase(eye, pass).
   */
  enum class VrCpuPhase : uint8_t {
    /// The game's own main loop, outside the VR frame (its update)
    Game,
    /// Waiting for the headset's next frame
    Wait,
    /// The VR frame outside the eyes: input, scope, panels, submission
    Frame,
  };

  constexpr uint8_t VrCpuPhaseEyeBase = 16;

  constexpr uint8_t vrCpuEyePhase(uint32_t eye, VrRenderPass pass) {
    return uint8_t(VrCpuPhaseEyeBase + eye * VrRenderPassCount + uint32_t(pass));
  }

  /**
   * \brief Name of a phase in logs and in the samples file
   */
  std::string vrCpuPhaseName(uint8_t phase);

  /**
   * \brief Tells whether the bytes before an address end with a call
   *
   * A value on the stack is taken as a return address only if the code
   * just before it is a call instruction: \c E8 rel32 or the \c FF /2
   * forms the game's compiler emits (register, register plus displacement,
   * absolute memory, with or without a SIB byte).
   *
   * \param [in] before The seven bytes before the address, \c before[6]
   *    being the byte just before it
   */
  bool vrFollowsCall(const uint8_t* before);

  /**
   * \brief Code ranges of the loaded modules, to name sampled addresses
   */
  class VrCpuModules {

  public:

    struct Module {
      uint32_t    base = 0;
      uint32_t    end  = 0;
      std::string name;
    };

    static constexpr uint32_t Unknown = ~0u;

    /**
     * \brief Replaces the modules, which may be given in any order
     */
    void set(std::vector<Module> modules);

    /**
     * \returns Index of the module holding the address, or \c Unknown
     */
    uint32_t find(uint32_t address) const;

    const std::vector<Module>& modules() const {
      return m_modules;
    }

  private:

    std::vector<Module> m_modules;

  };

  /**
   * \brief One sample of the game thread
   */
  struct VrCpuSample {
    /// Instruction pointer
    uint32_t eip = 0;
    /// Nearest return addresses into the game found on the stack, 0 if none
    std::array<uint32_t, 2> callers = { };
    uint8_t  phase = 0;

    bool operator == (const VrCpuSample& other) const {
      return eip == other.eip && callers == other.callers && phase == other.phase;
    }
  };

  struct VrCpuSampleHash {
    size_t operator () (const VrCpuSample& sample) const;
  };

  /**
   * \brief Samples of a window, counted per distinct sample
   */
  class VrCpuSampleTable {

  public:

    void add(const VrCpuSample& sample);

    void reset();

    uint32_t total() const {
      return m_total;
    }

    const std::unordered_map<VrCpuSample, uint32_t, VrCpuSampleHash>& counts() const {
      return m_counts;
    }

    /**
     * \brief Share of the samples per module and per phase, for the log
     *
     * \param [in] modules Names the instruction pointers
     * \param [in] maxModules Modules listed; the others are summed
     */
    std::string describeShares(const VrCpuModules& modules, uint32_t maxModules) const;

  private:

    std::unordered_map<VrCpuSample, uint32_t, VrCpuSampleHash> m_counts;
    uint32_t m_total = 0;

  };

}
