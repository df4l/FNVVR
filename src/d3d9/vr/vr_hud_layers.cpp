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
      && VrGameMemory::matches(VrGame::HudSubtitlesStoreSite,
        VrGame::HudSubtitlesStore, sizeof(VrGame::HudSubtitlesStore))
      && VrGameMemory::matches(VrGame::TileGetNode,
        VrGame::TileGetNodePrologue, sizeof(VrGame::TileGetNodePrologue))
      && VrGameMemory::matches(VrGame::ObjectSetFlag,
        VrGame::ObjectSetFlagPrologue, sizeof(VrGame::ObjectSetFlagPrologue))
      && VrGameMemory::readable(VrGame::HudMainMenu, sizeof(uintptr_t));

    m_menusFound = m_available
      && VrGameMemory::matches(VrGame::MenuTileReadSite,
        VrGame::MenuTileRead, sizeof(VrGame::MenuTileRead))
      && VrGameMemory::matches(VrGame::InterfaceManagerGetSingleton,
        VrGame::InterfaceManagerGetSingletonBytes, sizeof(VrGame::InterfaceManagerGetSingletonBytes))
      && VrGameMemory::readable(VrGame::MenuTiles, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::MenuTileCount, sizeof(uint16_t))
      && VrGameMemory::readable(VrGame::InterfaceManager, sizeof(uintptr_t));

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
      if (std::find(keep, keep + count, group) == keep + count)
        cullTile(readField<uintptr_t>(hud, group));
    }

    return true;
  }


  bool VrHudLayers::isolateFromMenus(const uintptr_t* keep, size_t count) {
    if (!m_menusFound || !isolate(keep, count))
      return false;

    uintptr_t tiles = readField<uintptr_t>(VrGame::MenuTiles, 0);
    uint32_t  menus = readField<uint16_t>(VrGame::MenuTileCount, 0);

    for (uint32_t i = 0; tiles && i < menus; i++) {
      if (i != VrGame::HudMenuId - VrGame::FirstMenuId)
        cullTile(readField<uintptr_t>(tiles, i * sizeof(uintptr_t)));
    }

    uintptr_t interfaceManager = readField<uintptr_t>(VrGame::InterfaceManager, 0);

    if (interfaceManager)
      cullTile(readField<uintptr_t>(interfaceManager, VrGame::InterfaceCursorTile));

    return true;
  }


  void VrHudLayers::cullTile(uintptr_t tile) {
    uintptr_t node = tile ? readField<uintptr_t>(tile, VrGame::TileNode) : 0;

    if (!node)
      return;

    auto flags = reinterpret_cast<uint32_t*>(node + VrGame::ObjectFlags);

    if (*flags & VrGame::ObjectFlagAppCulled)
      return;

    *flags |= VrGame::ObjectFlagAppCulled;
    m_culledFlags.push_back(flags);
  }


  void VrHudLayers::restore() {
    for (uint32_t* flags : m_culledFlags)
      *flags &= ~VrGame::ObjectFlagAppCulled;

    m_culledFlags.clear();
  }

}
