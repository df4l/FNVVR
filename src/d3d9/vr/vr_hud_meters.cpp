#include <cmath>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_head_hud.h"
#include "vr_hud_meters.h"

namespace dxvk {

  namespace {

    using TileGetFloatFn = float (__thiscall*)(uintptr_t tile, uint32_t trait);
    using TileSetFloatFn = void (__thiscall*)(uintptr_t tile, uint32_t trait, float value, bool propagate);

    template<typename T>
    T readField(uintptr_t base, uintptr_t offset) {
      return *reinterpret_cast<const T*>(base + offset);
    }

  }


  bool VrHudMeters::initialize() {
    bool shared = VrGameMemory::matches(VrGame::HudMessagesStoreSite,
        VrGame::HudMessagesStore, sizeof(VrGame::HudMessagesStore))
      && VrGameMemory::matches(VrGame::TileGetFloat,
        VrGame::TileGetFloatPrologue, sizeof(VrGame::TileGetFloatPrologue))
      && VrGameMemory::matches(VrGame::TileSetFloat,
        VrGame::TileSetFloatPrologue, sizeof(VrGame::TileSetFloatPrologue))
      && VrGameMemory::readable(VrGame::HudMainMenu, sizeof(uintptr_t));

    m_radiationAvailable = shared
      && VrGameMemory::matches(VrGame::HudRadiationMeterStoreSite,
        VrGame::HudRadiationMeterStore, sizeof(VrGame::HudRadiationMeterStore));

    m_xpAvailable = shared
      && VrGameMemory::matches(VrGame::HudXpMeterStoreSite,
        VrGame::HudXpMeterStore, sizeof(VrGame::HudXpMeterStore))
      && VrGameMemory::matches(VrGame::HudXpMeterBaseReadSite,
        VrGame::HudXpMeterBaseRead, sizeof(VrGame::HudXpMeterBaseRead));

    m_sneakAvailable = shared
      && VrGameMemory::matches(VrGame::HudSneakMeterStoreSite,
        VrGame::HudSneakMeterStore, sizeof(VrGame::HudSneakMeterStore));

    m_breathAvailable = shared
      && VrGameMemory::matches(VrGame::HudBreathMeterStoreSite,
        VrGame::HudBreathMeterStore, sizeof(VrGame::HudBreathMeterStore));

    return m_radiationAvailable || m_xpAvailable || m_sneakAvailable || m_breathAvailable;
  }


  VrHudMetersPlaced VrHudMeters::place(float interfaceHeight) {
    VrHudMetersPlaced placed;

    uintptr_t hud = readField<uintptr_t>(VrGame::HudMainMenu, 0);

    if (!hud)
      return placed;

    placeColumn(hud, placed);
    placeBelowMiddle(hud, interfaceHeight, placed);
    return placed;
  }


  void VrHudMeters::placeColumn(uintptr_t hud, VrHudMetersPlaced& placed) {
    if (!m_radiationAvailable && !m_xpAvailable)
      return;

    uintptr_t messages  = readField<uintptr_t>(hud, VrGame::HudMessages);
    uintptr_t radiation = m_radiationAvailable ? readField<uintptr_t>(hud, VrGame::HudRadiationMeter) : 0;
    uintptr_t xp        = m_xpAvailable ? readField<uintptr_t>(hud, VrGame::HudXpMeter) : 0;

    if (!messages)
      return;

    auto getFloat = reinterpret_cast<TileGetFloatFn>(VrGame::TileGetFloat);
    auto setFloat = reinterpret_cast<TileSetFloatFn>(VrGame::TileSetFloat);

    // Only HUDMainMenu::Create sets the messages' height
    float row = getFloat(messages, VrGame::TileTraitY);

    if (radiation) {
      float y = getFloat(radiation, VrGame::TileTraitY);

      if (y != row) {
        setFloat(radiation, VrGame::TileTraitY, row, true);

        if (!m_loggedRadiation) {
          Logger::info(str::format("VR: The radiation meter is moved from height ", y, " to ", row,
            ", the row of the HUD messages, to be shown in front of the head"));
          m_loggedRadiation = true;
        }
      }

      placed.radiation = true;
    }

    if (xp) {
      float target = row + VrRadiationMeterHeight;
      float base   = getFloat(xp, VrGame::TileTraitXpMeterBaseY);

      if (base != target) {
        // The meter may be raised from its base by the HUD's visibility
        // mode, which stays the same
        float y = getFloat(xp, VrGame::TileTraitY);
        setFloat(xp, VrGame::TileTraitXpMeterBaseY, target, true);
        setFloat(xp, VrGame::TileTraitY, y - base + target, true);

        if (!m_loggedXp) {
          Logger::info(str::format("VR: The XP meter is moved from height ", base, " to ", target,
            ", below the radiation meter, to be shown in front of the head"));
          m_loggedXp = true;
        }
      }

      placed.xp = true;
    }
  }


  void VrHudMeters::placeBelowMiddle(uintptr_t hud, float interfaceHeight, VrHudMetersPlaced& placed) {
    uintptr_t sneak  = m_sneakAvailable ? readField<uintptr_t>(hud, VrGame::HudSneakMeter) : 0;
    uintptr_t breath = m_breathAvailable ? readField<uintptr_t>(hud, VrGame::HudBreathMeter) : 0;

    float target = std::floor(0.5f * interfaceHeight) + VrSneakMeterBelowMiddle;

    if (sneak)
      placed.sneak = placeAt(sneak, target, m_loggedSneak, "sneak meter");

    if (breath)
      placed.breath = placeAt(breath, target, m_loggedBreath, "breath meter");
  }


  bool VrHudMeters::placeAt(uintptr_t tile, float y, bool& logged, const char* name) {
    auto getFloat = reinterpret_cast<TileGetFloatFn>(VrGame::TileGetFloat);
    auto setFloat = reinterpret_cast<TileSetFloatFn>(VrGame::TileSetFloat);

    float current = getFloat(tile, VrGame::TileTraitY);

    if (current != y) {
      setFloat(tile, VrGame::TileTraitY, y, true);

      if (!logged) {
        Logger::info(str::format("VR: The ", name, " is moved from height ", current, " to ", y,
          ", below the middle of the HUD panel in front of the head"));
        logged = true;
      }
    }

    return true;
  }

}
