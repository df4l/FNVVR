#include <windows.h>

#include <algorithm>

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_hud_layers.h"

namespace dxvk {

  namespace {

    using TileGetFloatFn = float (__thiscall*)(uintptr_t tile, uint32_t trait);
    using TileGetChildFn = uintptr_t (__thiscall*)(uintptr_t tile, const char* name);

    /// What yUI adds to the HUD's tile: crosshair, hit marker, hit indicator,
    /// objective markers and loot menu. They are placed for a flat screen
    /// around its centre, and the head panel shows the whole HUD image.
    constexpr const char* YuiTiles[] = { "JDC", "JHM", "JHI", "JVO", "JLM" };

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

    m_rectsFound = m_available
      && VrGameMemory::matches(VrGame::HudHitPointsStoreSite,
        VrGame::HudHitPointsStore, sizeof(VrGame::HudHitPointsStore))
      && VrGameMemory::matches(VrGame::HudInfoStoreSite,
        VrGame::HudInfoStore, sizeof(VrGame::HudInfoStore))
      && VrGameMemory::matches(VrGame::HudActionPointsStoreSite,
        VrGame::HudActionPointsStore, sizeof(VrGame::HudActionPointsStore))
      && VrGameMemory::matches(VrGame::TileGetFloat,
        VrGame::TileGetFloatPrologue, sizeof(VrGame::TileGetFloatPrologue));

    m_yuiLoaded = m_menusFound && GetModuleHandleW(L"yUI.dll")
      && VrGameMemory::matches(VrGame::TileGetChildByName,
        VrGame::TileGetChildByNamePrologue, sizeof(VrGame::TileGetChildByNamePrologue));

    return m_available;
  }


  bool VrHudLayers::groupRect(uintptr_t group, VrInterfaceRect& rect) const {
    if (!m_rectsFound)
      return false;

    uintptr_t hud  = readField<uintptr_t>(VrGame::HudMainMenu, 0);
    uintptr_t tile = hud ? readField<uintptr_t>(hud, group) : 0;

    if (!tile)
      return false;

    // The groups are children of the HUD menu's tile, which is at the
    // origin, so their position is relative to the screen
    auto getFloat = reinterpret_cast<TileGetFloatFn>(VrGame::TileGetFloat);
    rect.left   = getFloat(tile, VrGame::TileTraitX);
    rect.top    = getFloat(tile, VrGame::TileTraitY);
    rect.right  = rect.left + getFloat(tile, VrGame::TileTraitWidth);
    rect.bottom = rect.top  + getFloat(tile, VrGame::TileTraitHeight);
    return true;
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

    if (m_yuiLoaded) {
      uintptr_t tiles   = readField<uintptr_t>(VrGame::MenuTiles, 0);
      uintptr_t hudTile = tiles
        ? readField<uintptr_t>(tiles, (VrGame::HudMenuId - VrGame::FirstMenuId) * sizeof(uintptr_t)) : 0;

      for (uint32_t i = 0; hudTile && i < std::size(YuiTiles); i++)
        cullTile(reinterpret_cast<TileGetChildFn>(VrGame::TileGetChildByName)(hudTile, YuiTiles[i]));
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


  bool VrHudLayers::isMenuDrawn() const {
    if (!m_menusFound)
      return false;

    uintptr_t tiles = readField<uintptr_t>(VrGame::MenuTiles, 0);
    uint32_t  menus = readField<uint16_t>(VrGame::MenuTileCount, 0);

    for (uint32_t i = 0; tiles && i < menus; i++) {
      if (i == VrGame::HudMenuId - VrGame::FirstMenuId)
        continue;

      uintptr_t tile = readField<uintptr_t>(tiles, i * sizeof(uintptr_t));
      uintptr_t node = tile ? readField<uintptr_t>(tile, VrGame::TileNode) : 0;

      // A node outside the interface scene graph is not drawn
      if (node && readField<uintptr_t>(node, VrGame::ObjectParent)
       && !(readField<uint32_t>(node, VrGame::ObjectFlags) & VrGame::ObjectFlagAppCulled))
        return true;
    }

    return false;
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
