#pragma once

#include <unordered_map>

#include "dxvk_buffer.h"
#include "dxvk_context_state.h"
#include "dxvk_sampler.h"

namespace dxvk {

  /**
   * \brief Bindings of a context, saved to run recorded commands again
   *
   * Everything a command may rely on without setting it itself: the
   * pipeline state, the vertex and index buffers, render targets, push
   * data and the resources bound to shaders. Command buffer state such
   * as an active render pass is not part of it.
   */
  struct DxvkStereoBindings {
    DxvkContextState                                           state;
    std::array<Rc<DxvkSampler>,  MaxNumSamplerSlots>           samplers;
    std::array<DxvkBufferSlice,  MaxNumUniformBufferSlots>     uniformBuffers;
    std::array<DxvkViewPair,     MaxNumResourceSlots>          resources;
  };


  /**
   * \brief Storage of buffers before they were first replaced
   *
   * Recorded commands bind buffers, whose storage is looked up when a draw
   * runs. A buffer that was given new storage since the commands were
   * recorded (a discarded dynamic buffer, a constant buffer ring) has to
   * get back the storage it had when they were recorded before they run
   * again. The journal keeps, for each buffer given new storage while it
   * is open, the storage it had before.
   */
  class DxvkStereoBufferJournal {

  public:

    struct Entry {
      Rc<DxvkBuffer>             buffer;
      Rc<DxvkResourceAllocation> storage;
    };

    /**
     * \brief Notes the storage a buffer had before it is replaced
     *
     * Only the first replacement of each buffer is kept.
     */
    void note(const Rc<DxvkBuffer>& buffer, const Rc<DxvkResourceAllocation>& previous) {
      m_entries.try_emplace(buffer.ptr(), Entry { buffer, previous });
    }

    bool contains(const DxvkBuffer* buffer) const {
      return m_entries.find(buffer) != m_entries.end();
    }

    const std::unordered_map<const DxvkBuffer*, Entry>& entries() const {
      return m_entries;
    }

    void clear() {
      m_entries.clear();
    }

  private:

    std::unordered_map<const DxvkBuffer*, Entry> m_entries;

  };

}
