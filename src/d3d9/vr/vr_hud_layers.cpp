#include <algorithm>

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_hud_layers.h"

namespace dxvk {

  namespace {

    template<typename T>
    T readField(uintptr_t base, uintptr_t offset) {
      return *reinterpret_cast<const T*>(base + offset);
    }

  }


  bool VrHudLayers::initialize() {
    m_available = VrGameMemory::matches(VrGame::HudMessagesStoreSite,
        VrGame::HudMessagesStore, sizeof(VrGame::HudMessagesStore))
      && VrGameMemory::matches(VrGame::HudQuestReminderStoreSite,
        VrGame::HudQuestReminderStore, sizeof(VrGame::HudQuestReminderStore))
      && VrGameMemory::matches(VrGame::TileGetNode,
        VrGame::TileGetNodePrologue, sizeof(VrGame::TileGetNodePrologue))
      && VrGameMemory::matches(VrGame::ObjectSetFlag,
        VrGame::ObjectSetFlagPrologue, sizeof(VrGame::ObjectSetFlagPrologue))
      && VrGameMemory::readable(VrGame::HudMainMenu, sizeof(uintptr_t));

    return m_available;
  }


  bool VrHudLayers::isolate(const uintptr_t* keep, size_t count) {
    restore();

    if (!m_available)
      return false;

    uintptr_t hud = readField<uintptr_t>(VrGame::HudMainMenu, 0);

    if (!hud)
      return false;

    for (uintptr_t group : VrGame::HudGroups) {
      if (std::find(keep, keep + count, group) != keep + count)
        continue;

      uintptr_t tile = readField<uintptr_t>(hud, group);
      uintptr_t node = tile ? readField<uintptr_t>(tile, VrGame::TileNode) : 0;

      if (!node)
        continue;

      auto flags = reinterpret_cast<uint32_t*>(node + VrGame::ObjectFlags);

      if (*flags & VrGame::ObjectFlagAppCulled)
        continue;

      *flags |= VrGame::ObjectFlagAppCulled;
      m_culledFlags.push_back(flags);
    }

    return true;
  }


  void VrHudLayers::restore() {
    for (uint32_t* flags : m_culledFlags)
      *flags &= ~VrGame::ObjectFlagAppCulled;

    m_culledFlags.clear();
  }

}
