#include "d3d9_device.h"

namespace dxvk {

  namespace {

    /// Vertex shader stage, the only one that reads the stereo matrix
    constexpr VkShaderStageFlags StereoStages = VK_SHADER_STAGE_VERTEX_BIT;

    /// Size of a matrix of four vec4 rows
    constexpr VkDeviceSize StereoMatrixSize = 16u * sizeof(float);

    /// Matrices written in turn: far more than the frames the GPU may be behind
    constexpr VkDeviceSize StereoBufferSize = 64u << 10;

  }


  void D3D9DeviceEx::InitStereoReplay() {
    m_stereoCs = std::make_shared<D3D9StereoCsState>();

    VkDeviceSize alignment = m_dxvkDevice->properties().core.properties.limits.minUniformBufferOffsetAlignment;
    m_stereoEntrySize  = align(StereoMatrixSize, std::max<VkDeviceSize>(alignment, 16u));
    m_stereoEntryCount = uint32_t(StereoBufferSize / m_stereoEntrySize);

    DxvkBufferCreateInfo info;
    info.size   = StereoBufferSize;
    info.usage  = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    info.access = VK_ACCESS_UNIFORM_READ_BIT;
    info.stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    info.debugName = "Stereo transform";

    m_stereoBuffer = m_dxvkDevice->createBuffer(info,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_stereoMapPtr = reinterpret_cast<uint8_t*>(m_stereoBuffer->mapPtr(0));

    // The first entry is the identity, bound unless a replay draws the world
    const float identity[16] = {
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f };

    std::memcpy(m_stereoMapPtr, identity, sizeof(identity));

    EmitCs([
      cSlice = DxvkBufferSlice(m_stereoBuffer, 0u, StereoMatrixSize)
    ] (DxvkContext* ctx) {
      ctx->bindUniformBuffer(StereoStages,
        D3D9ShaderResourceMapping::VSStereoTransform, DxvkBufferSlice(cSlice));
    });
  }


  void D3D9DeviceEx::ReplaceCsChunk() {
    m_csDataType = D3D9CmdType::None;
    m_csData = nullptr;
    m_csChunk = AllocCsChunk();
  }


  bool D3D9DeviceEx::PauseStereoCapture() {
    if (likely(!m_stereoCapturing))
      return false;

    FlushCsChunk();
    m_stereoCapturing = false;
    ReplaceCsChunk();
    return true;
  }


  void D3D9DeviceEx::ResumeStereoCapture(bool Paused) {
    if (likely(!Paused))
      return;

    FlushCsChunk();
    m_stereoCapturing = true;
    ReplaceCsChunk();
  }


  void D3D9DeviceEx::BeginVrStereoCapture() {
    D3D9DeviceLock lock = LockDevice();

    DiscardStereoCaptureLocked();

    // Runs once, before the kept commands: what they may rely on
    // without setting it, and every buffer storage they may read
    EmitCs([
      cState = m_stereoCs,
      cIa    = &m_iaState
    ] (DxvkContext* ctx) {
      cState->captured = std::make_unique<DxvkStereoBindings>();
      ctx->saveStereoBindings(*cState->captured);
      cState->streamsUsedCaptured = cIa->streamsUsed;
      cState->journal.clear();
      ctx->setStereoBufferJournal(&cState->journal);
    });

    FlushCsChunk();
    m_stereoCapturing = true;
    ReplaceCsChunk();
  }


  void D3D9DeviceEx::SetVrStereoWorld(bool world) {
    D3D9DeviceLock lock = LockDevice();

    EmitCs([
      cState = m_stereoCs,
      cWorld = world
    ] (DxvkContext* ctx) {
      cState->world = cWorld;

      VkDeviceSize offset = cWorld && cState->eye ? cState->rightOffset : 0u;
      ctx->bindUniformBufferRange(StereoStages,
        D3D9ShaderResourceMapping::VSStereoTransform, offset, StereoMatrixSize);
    });
  }


  void D3D9DeviceEx::EndVrStereoCapture() {
    D3D9DeviceLock lock = LockDevice();

    if (!m_stereoCapturing)
      return;

    FlushCsChunk();
    m_stereoCapturing = false;
    ReplaceCsChunk();

    m_stereoCaptured = !m_stereoChunks.empty();
  }


  bool D3D9DeviceEx::ReplayVrStereoCapture(const std::array<float, 16>& rows) {
    D3D9DeviceLock lock = LockDevice();

    if (m_stereoCapturing || !m_stereoCaptured)
      return false;

    VkDeviceSize offset = VkDeviceSize(m_stereoNextEntry) * m_stereoEntrySize;
    std::memcpy(m_stereoMapPtr + offset, rows.data(), StereoMatrixSize);

    if (++m_stereoNextEntry >= m_stereoEntryCount)
      m_stereoNextEntry = 1u;

    FlushCsChunk();

    // The buffers get back the storage they had when the capture started,
    // and the context the bindings it had then
    EmitCs([
      cState  = m_stereoCs,
      cIa     = &m_iaState,
      cOffset = offset
    ] (DxvkContext* ctx) {
      ctx->setStereoBufferJournal(nullptr);
      cState->beforeReplay = std::make_unique<DxvkStereoBindings>();
      ctx->saveStereoBindings(*cState->beforeReplay);
      cState->streamsUsedBeforeReplay = cIa->streamsUsed;

      cState->beforeReplayStorage.clear();

      for (const auto& entry : cState->journal.entries()) {
        cState->beforeReplayStorage.push_back({ entry.second.buffer, entry.second.buffer->storage() });
        ctx->assignStereoBufferStorage(entry.second.buffer, entry.second.storage);
      }

      ctx->restoreStereoBindings(*cState->captured);
      cIa->streamsUsed = cState->streamsUsedCaptured;

      cState->replayJournal.clear();
      ctx->setStereoBufferJournal(&cState->replayJournal);

      cState->eye = 1u;
      cState->rightOffset = cOffset;
    });

    FlushCsChunk();

    std::vector<DxvkCsChunkRef> chunks = std::move(m_stereoChunks);
    m_stereoChunks.clear();
    m_stereoCaptured = false;

    for (const auto& chunk : chunks)
      EmitCsChunk(DxvkCsChunkRef(chunk));

    // Everything as it was before the replay
    EmitCs([
      cState = m_stereoCs,
      cIa    = &m_iaState
    ] (DxvkContext* ctx) {
      ctx->setStereoBufferJournal(nullptr);

      for (const auto& entry : cState->replayJournal.entries()) {
        if (!cState->journal.contains(entry.first))
          ctx->assignStereoBufferStorage(entry.second.buffer, entry.second.storage);
      }

      for (const auto& entry : cState->beforeReplayStorage)
        ctx->assignStereoBufferStorage(entry.buffer, entry.storage);

      ctx->restoreStereoBindings(*cState->beforeReplay);
      cIa->streamsUsed = cState->streamsUsedBeforeReplay;

      cState->eye   = 0u;
      cState->world = false;

      cState->journal.clear();
      cState->replayJournal.clear();
      cState->beforeReplayStorage.clear();
      cState->captured     = nullptr;
      cState->beforeReplay = nullptr;
    });

    FlushCsChunk();
    return true;
  }


  void D3D9DeviceEx::DiscardVrStereoCapture() {
    D3D9DeviceLock lock = LockDevice();
    DiscardStereoCaptureLocked();
  }


  void D3D9DeviceEx::DiscardStereoCaptureLocked() {
    if (m_stereoCapturing) {
      FlushCsChunk();
      m_stereoCapturing = false;
      ReplaceCsChunk();
    } else if (!m_stereoCaptured && m_stereoChunks.empty()) {
      return;
    }

    m_stereoChunks.clear();
    m_stereoCaptured = false;

    EmitCs([
      cState = m_stereoCs
    ] (DxvkContext* ctx) {
      ctx->setStereoBufferJournal(nullptr);
      cState->journal.clear();
      cState->captured = nullptr;
    });
  }

}
