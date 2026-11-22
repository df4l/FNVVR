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
    m_stereo.cs = std::make_shared<D3D9StereoCsState>();

    VkDeviceSize alignment = m_dxvkDevice->properties().core.properties.limits.minUniformBufferOffsetAlignment;
    m_stereo.entrySize  = align(StereoMatrixSize, std::max<VkDeviceSize>(alignment, 16u));
    m_stereo.entryCount = uint32_t(StereoBufferSize / m_stereo.entrySize);

    DxvkBufferCreateInfo info;
    info.size   = StereoBufferSize;
    info.usage  = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    info.access = VK_ACCESS_UNIFORM_READ_BIT;
    info.stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    info.debugName = "Stereo transform";

    m_stereo.buffer = m_dxvkDevice->createBuffer(info,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_stereo.mapPtr = reinterpret_cast<uint8_t*>(m_stereo.buffer->mapPtr(0));

    // The first entry is the identity, bound unless a replay draws the world
    const float identity[16] = {
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f };

    std::memcpy(m_stereo.mapPtr, identity, sizeof(identity));

    EmitCs([
      cSlice = DxvkBufferSlice(m_stereo.buffer, 0u, StereoMatrixSize)
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
    if (likely(!m_stereo.capturing))
      return false;

    FlushCsChunk();
    m_stereo.capturing = false;
    ReplaceCsChunk();

    // The replay flushes at the same place, with a flush of its own
    if (m_stereo.flushes.empty() || m_stereo.flushes.back() != m_stereo.chunks.size())
      m_stereo.flushes.push_back(m_stereo.chunks.size());

    return true;
  }


  void D3D9DeviceEx::ResumeStereoCapture(bool Paused) {
    if (likely(!Paused))
      return;

    FlushCsChunk();
    m_stereo.capturing = true;
    ReplaceCsChunk();
  }


  void D3D9DeviceEx::BeginVrStereoCapture() {
    D3D9DeviceLock lock = LockDevice();

    DiscardStereoCaptureLocked();
    m_stereo.flushes.clear();

    // Runs once, before the kept commands: what they may rely on
    // without setting it, and every buffer storage they may read
    EmitCs([
      cState = m_stereo.cs,
      cIa    = &m_iaState
    ] (DxvkContext* ctx) {
      cState->captured = std::make_unique<DxvkStereoBindings>();
      ctx->saveStereoBindings(*cState->captured);
      cState->streamsUsedCaptured = cIa->streamsUsed;
      cState->journal.clear();
      ctx->setStereoBufferJournal(&cState->journal);
    });

    FlushCsChunk();
    m_stereo.capturing = true;
    ReplaceCsChunk();
  }


  void D3D9DeviceEx::SetVrStereoWorld(bool world) {
    D3D9DeviceLock lock = LockDevice();

    EmitCs([
      cState = m_stereo.cs,
      cWorld = world
    ] (DxvkContext* ctx) {
      VkDeviceSize offset = cWorld && cState->eye ? cState->rightOffset : 0u;
      ctx->bindUniformBufferRange(StereoStages,
        D3D9ShaderResourceMapping::VSStereoTransform, offset, StereoMatrixSize);
    });
  }


  void D3D9DeviceEx::EndVrStereoCapture() {
    D3D9DeviceLock lock = LockDevice();

    if (!m_stereo.capturing)
      return;

    FlushCsChunk();
    m_stereo.capturing = false;
    ReplaceCsChunk();

    m_stereo.captured = !m_stereo.chunks.empty();
  }


  bool D3D9DeviceEx::ReplayVrStereoCapture(const std::array<float, 16>& rows) {
    D3D9DeviceLock lock = LockDevice();

    if (m_stereo.capturing || !m_stereo.captured)
      return false;

    VkDeviceSize offset = VkDeviceSize(m_stereo.nextEntry) * m_stereo.entrySize;
    std::memcpy(m_stereo.mapPtr + offset, rows.data(), StereoMatrixSize);

    if (++m_stereo.nextEntry >= m_stereo.entryCount)
      m_stereo.nextEntry = 1u;

    FlushCsChunk();

    bool first = !m_stereo.replays++;

    if (first) {
      Logger::info(str::format("D3D9: First stereo replay: ", m_stereo.chunks.size(), " chunks, ",
        m_stereo.flushes.size(), " flushes, matrix at offset ", offset));
    }

    // The buffers get back the storage they had when the capture started,
    // and the context the bindings it had then
    EmitCs([
      cState  = m_stereo.cs,
      cIa     = &m_iaState,
      cOffset = offset,
      cFirst  = first
    ] (DxvkContext* ctx) {
      if (cFirst) {
        Logger::info(str::format("D3D9: First stereo replay: ", cState->journal.entries().size(),
          " buffers get back their storage, bindings captured: ", cState->captured ? "yes" : "no"));
      }

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

    std::vector<DxvkCsChunkRef> chunks = std::move(m_stereo.chunks);
    std::vector<size_t> flushes = std::move(m_stereo.flushes);
    m_stereo.chunks.clear();
    m_stereo.flushes.clear();
    m_stereo.captured = false;

    size_t nextFlush = 0u;

    for (size_t i = 0u; i < chunks.size(); i++) {
      EmitCsChunk(DxvkCsChunkRef(chunks[i]));

      if (nextFlush < flushes.size() && flushes[nextFlush] <= i + 1u) {
        ExecuteFlush(false);

        while (nextFlush < flushes.size() && flushes[nextFlush] <= i + 1u)
          nextFlush++;
      }
    }

    // Everything as it was before the replay
    EmitCs([
      cState = m_stereo.cs,
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

      cState->eye = 0u;

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
    if (m_stereo.capturing) {
      FlushCsChunk();
      m_stereo.capturing = false;
      ReplaceCsChunk();
    } else if (!m_stereo.captured && m_stereo.chunks.empty()) {
      return;
    }

    m_stereo.chunks.clear();
    m_stereo.flushes.clear();
    m_stereo.captured = false;

    EmitCs([
      cState = m_stereo.cs
    ] (DxvkContext* ctx) {
      ctx->setStereoBufferJournal(nullptr);
      cState->journal.clear();
      cState->captured = nullptr;
    });
  }

}
