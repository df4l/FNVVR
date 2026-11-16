#include <iomanip>
#include <sstream>

#include "vr_spike_log.h"

namespace dxvk {

  namespace {

    std::string fixed(double value, int precision = 2) {
      std::ostringstream stream;
      stream << std::fixed << std::setprecision(precision) << value;
      return stream.str();
    }

    double step(const VrFrameCycle& cycle, VrFrameStep step) {
      return cycle[uint32_t(step)];
    }

  }


  VrSpikeLog::VrSpikeLog(double periodMs, bool gpuTimed, LineFn sink)
  : m_periodMs(periodMs), m_gpuTimed(gpuTimed), m_sink(std::move(sink)) { }


  void VrSpikeLog::setCpuPasses(uint64_t frameId, const VrRenderProfile::EyePasses& passes) {
    Record& record = m_records[frameId % RecordCount];

    // A frame whose GPU times never came
    if (record.used && record.hasCycle)
      finish(record);

    record = Record();
    record.frameId = frameId;
    record.used    = true;
    record.cpu     = passes;
  }


  void VrSpikeLog::setCycle(uint64_t frameId, const VrFrameCycle& cycle, double endSeconds) {
    Record* record = find(frameId);

    if (!record || record->hasCycle)
      return;

    record->hasCycle   = true;
    record->cycle      = cycle;
    record->endSeconds = endSeconds;
    tryFinish(*record);
  }


  void VrSpikeLog::setGpu(uint64_t frameId, const VrGpuFrameTimes& times) {
    Record* record = find(frameId);

    if (!record || record->hasGpu)
      return;

    record->hasGpu = true;
    record->gpu    = times;
    tryFinish(*record);
  }


  VrSpikeLog::Record* VrSpikeLog::find(uint64_t frameId) {
    Record& record = m_records[frameId % RecordCount];
    return record.used && record.frameId == frameId ? &record : nullptr;
  }


  void VrSpikeLog::tryFinish(Record& record) {
    if (record.hasCycle && (record.hasGpu || !m_gpuTimed))
      finish(record);
  }


  void VrSpikeLog::finish(Record& record) {
    record.used = false;

    double interval = step(record.cycle, VrFrameStep::Interval);
    double busy     = interval - step(record.cycle, VrFrameStep::Wait);
    double gpuFrame = record.gpu.parts[uint32_t(VrGpuFramePart::Frame)];

    std::string reasons;

    auto reason = [&reasons] (bool condition, const char* name) {
      if (condition)
        reasons += (reasons.empty() ? "" : ", ") + std::string(name);
    };

    reason(interval >= m_periodMs * 1.5, "late");
    reason(busy > m_periodMs, "game thread busy");
    reason(record.hasGpu && gpuFrame > m_periodMs, "GPU");

    if (!reasons.empty()) {
      if (m_lines < MaxLines) {
        m_sink(describe(record, reasons));

        if (m_spikeCallback)
          m_spikeCallback(record.frameId);

        m_lines += 1;
      } else {
        m_suppressed += 1;
      }
    }

    if (++m_frames < WindowFrames)
      return;

    if (m_suppressed)
      m_sink("VR: Spikes: " + std::to_string(m_suppressed) + " more in the last "
        + std::to_string(WindowFrames) + " frames were not logged");

    m_frames     = 0;
    m_lines      = 0;
    m_suppressed = 0;
  }


  std::string VrSpikeLog::describe(const Record& record, const std::string& reasons) const {
    const VrFrameCycle& cycle = record.cycle;

    auto cpu = [&record] (VrRenderPass pass) {
      return fixed(record.cpu[0][uint32_t(pass)]) + " | " + fixed(record.cpu[1][uint32_t(pass)]);
    };

    auto gpu = [&record] (VrRenderPass pass) {
      return fixed(record.gpu.eyes[0][uint32_t(pass)]) + " | " + fixed(record.gpu.eyes[1][uint32_t(pass)]);
    };

    std::ostringstream line;
    line << "VR: Spike in frame " << record.frameId << " (" << reasons << "), cycle ended at "
         << fixed(record.endSeconds, 6) << " s."
         << " CPU ms: frame " << fixed(step(cycle, VrFrameStep::Interval))
         << ", eye rendering " << fixed(step(cycle, VrFrameStep::Render))
         << ", panels " << fixed(step(cycle, VrFrameStep::Panels))
         << ", DXVK sync " << fixed(step(cycle, VrFrameStep::Sync))
         << ", layout transition " << fixed(step(cycle, VrFrameStep::Transition))
         << ", submission " << fixed(step(cycle, VrFrameStep::Submit))
         << ", hand-off " << fixed(step(cycle, VrFrameStep::HandOff))
         << ", after submission " << fixed(step(cycle, VrFrameStep::After))
         << ", waiting " << fixed(step(cycle, VrFrameStep::Wait))
         << ". CPU passes, left | right: whole eye " << cpu(VrRenderPass::Eye)
         << ", shadow maps " << cpu(VrRenderPass::Shadows)
         << ", water " << cpu(VrRenderPass::Water)
         << ", scene " << cpu(VrRenderPass::Scene)
         << ", first person " << cpu(VrRenderPass::FirstPerson)
         << ", image space " << cpu(VrRenderPass::ImageSpace) << ".";

    if (!record.hasGpu) {
      line << " GPU not timed";
      return line.str();
    }

    line << " GPU ms: frame " << fixed(record.gpu.parts[uint32_t(VrGpuFramePart::Frame)])
         << ", scope " << fixed(record.gpu.parts[uint32_t(VrGpuFramePart::Scope)])
         << ", panels " << fixed(record.gpu.parts[uint32_t(VrGpuFramePart::Panels)])
         << ", left | right: whole eye " << gpu(VrRenderPass::Eye)
         << ", shadow maps " << gpu(VrRenderPass::Shadows)
         << ", water " << gpu(VrRenderPass::Water)
         << ", scene " << gpu(VrRenderPass::Scene)
         << ", first person " << gpu(VrRenderPass::FirstPerson)
         << ", image space " << gpu(VrRenderPass::ImageSpace)
         << ", interface " << gpu(VrRenderPass::Interface)
         << ", eye copy " << gpu(VrRenderPass::EyeCopy);
    return line.str();
  }

}
