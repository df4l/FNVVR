#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_gpu_timer.h"

namespace dxvk {

  namespace {

    // Stereo frames covered by each logged window, as the CPU timings
    constexpr uint32_t WindowFrames = 900;

    // Frames a timed frame may wait for the GPU before it is dropped
    constexpr uint32_t MaxWaits = 30;

  }


  VrGpuTimer::VrGpuTimer(IDirect3DDevice9* device, uint64_t frequency)
  : m_device(device), m_frequency(frequency), m_profile(WindowFrames) { }


  std::unique_ptr<VrGpuTimer> VrGpuTimer::create(IDirect3DDevice9* device) {
    if (!device || FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, nullptr)))
      return nullptr;

    Com<IDirect3DQuery9> frequencyQuery;
    uint64_t frequency = 0;

    if (FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &frequencyQuery))
     || FAILED(frequencyQuery->Issue(D3DISSUE_END))
     || frequencyQuery->GetData(&frequency, sizeof(frequency), D3DGETDATA_FLUSH) != S_OK
     || !frequency)
      return nullptr;

    Logger::info(str::format("VR: GPU timings are on, a summary every ", WindowFrames, " frames"));
    return std::unique_ptr<VrGpuTimer>(new VrGpuTimer(device, frequency));
  }


  void VrGpuTimer::beginFrame() {
    if (m_recording)
      cancelFrame();

    Slot& slot = m_slots[m_next];

    if (slot.pending) {
      m_untimed += 1;
      return;
    }

    slot.used  = 0;
    slot.waits = 0;
    slot.marks.clear();

    m_recording  = true;
    m_frameToken = begin(VrGpuSection::part(VrGpuFramePart::Frame));
  }


  void VrGpuTimer::endFrame() {
    if (m_recording) {
      end(m_frameToken);

      m_slots[m_next].pending = true;
      m_next      = (m_next + 1) % SlotCount;
      m_pending  += 1;
      m_recording = false;
    }

    collect();
  }


  void VrGpuTimer::cancelFrame() {
    if (m_recording)
      m_untimed += 1;

    m_recording = false;
  }


  uint32_t VrGpuTimer::begin(VrGpuSection section) {
    if (!m_recording)
      return NoToken;

    Slot& slot = m_slots[m_next];
    uint32_t query = issue(slot);

    if (query == NoQuery)
      return NoToken;

    slot.marks.push_back({ section, query, NoQuery });
    return uint32_t(slot.marks.size() - 1);
  }


  void VrGpuTimer::end(uint32_t token) {
    if (!m_recording)
      return;

    Slot& slot = m_slots[m_next];

    if (token < slot.marks.size() && slot.marks[token].end == NoQuery)
      slot.marks[token].end = issue(slot);
  }


  uint32_t VrGpuTimer::issue(Slot& slot) {
    if (slot.used == slot.queries.size()) {
      Com<IDirect3DQuery9> query;

      if (FAILED(m_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &query))) {
        if (!m_loggedFailure) {
          Logger::warn("VR: A GPU timestamp query could not be created, sections are missing from the GPU timings");
          m_loggedFailure = true;
        }

        return NoQuery;
      }

      slot.queries.push_back(std::move(query));
    }

    if (FAILED(slot.queries[slot.used]->Issue(D3DISSUE_END)))
      return NoQuery;

    return slot.used++;
  }


  void VrGpuTimer::collect() {
    while (m_pending) {
      Slot& slot = m_slots[(m_next + SlotCount - m_pending) % SlotCount];

      if (!read(slot)) {
        if (++slot.waits <= MaxWaits)
          return;

        dropOldest();
        continue;
      }

      addFrame(slot);
      slot.pending = false;
      m_pending   -= 1;
    }
  }


  bool VrGpuTimer::read(Slot& slot) {
    slot.values.resize(slot.used);

    // Timestamps are written in order: the last one decides
    for (uint32_t i = slot.used; i > 0; i--) {
      if (slot.queries[i - 1]->GetData(&slot.values[i - 1], sizeof(uint64_t), 0) != S_OK)
        return false;
    }

    return true;
  }


  void VrGpuTimer::addFrame(const Slot& slot) {
    for (const Mark& mark : slot.marks) {
      if (mark.end == NoQuery)
        continue;

      double ms = vrGpuTimestampMs(slot.values[mark.begin], slot.values[mark.end], m_frequency);

      if (ms >= 0.0)
        m_profile.add(mark.section, ms);
    }

    if (m_profile.endFrame()) {
      Logger::info(m_profile.describe(m_untimed));
      m_profile.reset();
      m_untimed = 0;
    }
  }


  void VrGpuTimer::dropOldest() {
    m_slots[(m_next + SlotCount - m_pending) % SlotCount].pending = false;
    m_pending -= 1;
    m_untimed += 1;
  }

}
