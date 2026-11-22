#include "dxvk_context.h"

namespace dxvk {

  void DxvkContext::saveStereoBindings(
          DxvkStereoBindings&       bindings) {
    bindings.state          = m_state;
    bindings.samplers       = m_samplers;
    bindings.uniformBuffers = m_uniformBuffers;
    bindings.resources      = m_resources;
  }


  void DxvkContext::restoreStereoBindings(
    const DxvkStereoBindings&       bindings) {
    // Everything recorded so far goes into the command buffer
    // before the bindings change, as for external rendering
    endCurrentCommands();

    m_state          = bindings.state;
    m_samplers       = bindings.samplers;
    m_uniformBuffers = bindings.uniformBuffers;
    m_resources      = bindings.resources;

    m_state.om.boundShadingRate = nullptr;

    beginCurrentCommands();

    m_flags.set(
      DxvkContextFlag::GpDirtySpecConstants,
      DxvkContextFlag::CpDirtySpecConstants,
      DxvkContextFlag::DirtyPushData);
  }


  void DxvkContext::assignStereoBufferStorage(
    const Rc<DxvkBuffer>&           buffer,
    const Rc<DxvkResourceAllocation>& storage) {
    DxvkStereoBufferJournal* journal = std::exchange(m_stereoJournal, nullptr);
    invalidateBuffer(buffer, Rc<DxvkResourceAllocation>(storage));
    m_stereoJournal = journal;
  }

}
