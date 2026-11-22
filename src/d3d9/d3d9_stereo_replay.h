#pragma once

#include <memory>
#include <vector>

#include "../dxvk/dxvk_stereo_replay.h"

namespace dxvk {

  /**
   * \brief State of the stereo replay on the CS thread
   *
   * Only touched by commands, so only on the CS thread. Shared with the
   * commands that read it, see D3D9DeviceEx::ReplayVrStereoCapture.
   */
  struct D3D9StereoCsState {
    /// Eye the commands run for: 0 as recorded, 1 during the replay
    uint32_t                eye = 0u;
    /// Offset of the right eye's matrix in the device's stereo buffer
    VkDeviceSize            rightOffset = 0u;

    /// Bindings when the capture started, restored before the replay
    std::unique_ptr<DxvkStereoBindings> captured;
    /// Bindings before the replay, restored after it
    std::unique_ptr<DxvkStereoBindings> beforeReplay;

    /// Buffers given new storage since the capture started
    DxvkStereoBufferJournal journal;
    /// Buffers given new storage during the replay
    DxvkStereoBufferJournal replayJournal;
    /// Storage of the journal's buffers before the replay
    std::vector<DxvkStereoBufferJournal::Entry> beforeReplayStorage;

    /// Vertex streams the input layout used, kept by the device on the CS thread
    uint32_t                streamsUsedCaptured = 0u;
    uint32_t                streamsUsedBeforeReplay = 0u;
  };

}
