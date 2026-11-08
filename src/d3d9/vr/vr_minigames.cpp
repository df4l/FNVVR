#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_caravan_pick.h"
#include "vr_console_screen.h"
#include "vr_game_addresses.h"
#include "vr_game_depth.h"
#include "vr_game_memory.h"
#include "vr_game_nodes.h"
#include "vr_math.h"
#include "vr_menu_scenes.h"
#include "vr_minigames.h"
#include "vr_scene_model.h"
#include "vr_virtual_gamepad.h"

namespace dxvk {

  namespace {

    using IsMenuActiveFn  = bool  (__cdecl*)(uint32_t menuId, uint32_t unused);
    using IsTopMenuFn     = bool  (__cdecl*)(uint32_t menuId);
    using PointerFn       = void* (__cdecl*)();
    using SceneFn         = void  (__cdecl*)();
    using GetFloatFn      = float (__thiscall*)(void* tile, uint32_t trait);
    using HoveredBetFn    = uint32_t (__thiscall*)(void* menu);
    using MainFn          = void  (__thiscall*)(void* main);
    using ScreenFn        = void  (__thiscall*)(void* main, void* screen, void* renderer);
    using DrawFn          = void  (__thiscall*)(void* menu, void* texture, uint32_t clearFlags, void* target);
    using AssignFn        = void* (__thiscall*)(void* pointer, void* object);
    using TextureManagerFn = void* (__cdecl*)();
    using BorrowFn        = void* (__thiscall*)(void* manager, void* renderer, uint32_t type,
      uint32_t a, uint32_t b, uint32_t c);
    using ReturnFn        = void  (__thiscall*)(void* manager, void* texture);
    using Get3DFn         = void* (__thiscall*)(void* reference);
    using ClearFn         = void  (__fastcall*)(void* renderer, void* unused, const void* rect, uint32_t flags);
    using IsActorFn       = bool  (__thiscall*)(void* reference);
    using PickConstructFn = void* (__fastcall*)(void* pick, void* unused);
    using PickSetFilterFn = void  (__fastcall*)(void* pick, void* unused, uint32_t filter);
    using PickSetPointFn  = void  (__fastcall*)(void* pick, void* unused, const VrVector3* point);
    using PickObjectFn    = bool  (__fastcall*)(uint8_t* world, void* unused, void* pick);
    using GetFilterFn     = uint32_t* (__fastcall*)(uint8_t* actor, void* unused, uint32_t* filter);
    using GetPartFn       = uint8_t* (__fastcall*)(uint8_t* object, void* unused);
    using TileByNameFn    = void* (__cdecl*)(void* tile, const char* name);
    using TileSetFloatFn  = void  (__thiscall*)(void* tile, uint32_t trait, float value, bool clearActions);

    // The lock's keyhole, in the menu's space: the pin turns about it. The
    // lock is scaled so that the long screwdriver stays off the face.
    constexpr VrVector3 LockTarget     = { 141.0f, 0.6f, 0.0f };
    constexpr float     LockUnits      = 120.0f;
    constexpr float     LockDistance   = 0.55f;
    constexpr float     LockDrop       = 0.12f;

    // Pin's reach from the keyhole: further, the hand holds nothing
    constexpr float     PinHandReach   = 0.7f;
    // A stick takes the pin over until the hand has moved this far, in metres
    constexpr float     StickTakeover  = 0.25f;
    constexpr float     HandTakesBack  = 0.05f;

    // The slot machine's reels at the height of the chest and the lever at
    // the right hand's reach
    constexpr VrVector3 SlotTarget     = { 162.0f, 0.0f, 0.0f };
    constexpr VrVector3 SlotLeverBall  = { 177.0f, 16.0f, 16.7f };
    constexpr float     SlotDistance   = 0.45f;
    constexpr float     SlotDrop       = 0.25f;

    // Caravan's mat at 1.75 times its model, so that its cards are the size
    // of real ones. The eyes are put above the middle of the mat, behind the
    // player's edge (y -21): the mat stays still while the game's camera pans
    // between its betting, deck and game areas.
    constexpr float     CaravanScale   = 1.75f;
    constexpr float     CaravanUnits   = VrGameUnitsPerMetre / CaravanScale;
    constexpr float     CaravanNearEdge = -21.0f;
    constexpr float     CaravanFarEdge  = 14.7f;
    constexpr VrVector3 CaravanEye     = { 0.0f, CaravanNearEdge - 0.25f * CaravanUnits, 1.1f + 0.6f * CaravanUnits };
    constexpr VrVector3 CaravanFacing  = { 0.0f, 1.0f, 0.0f };

    // Between the player and the challenger, the mat lies on the world's
    // table under it, else at the height of the game's diner tables. The
    // world's model of the table is the mat's at scale 1, the seat 25 cm
    // behind the player's edge.
    constexpr VrModelArea CaravanArea = {
      -28.0f * CaravanScale, 28.9f * CaravanScale,
      CaravanNearEdge * CaravanScale, CaravanFarEdge * CaravanScale };

    constexpr VrSceneFit CaravanFit = {
      { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },
      { 0.0f, 0.0f, 0.0f }, CaravanScale,
      { 0.0f, CaravanNearEdge * CaravanScale - 0.25f * VrGameUnitsPerMetre, 0.0f }, { 0.0f, 1.0f, 0.0f },
      &CaravanArea, 1 };

    // Room left between the far edge and the challenger's feet, and how far
    // the challenger may stand for the table to go between the two
    constexpr float     CaravanGap         = 20.0f;
    constexpr float     CaravanReach       = 300.0f;
    // The game's diner table, put under the mat when no surface of the
    // world takes it: 117 x 57 units, its top 55.3 above its origin, its
    // long side along its +X. Its middle goes under the mat's.
    constexpr char      CaravanTableModel[] = "Meshes\\Furniture\\DinerTable01_NV.nif";
    constexpr float     CaravanTableTop    = 55.3f;
    constexpr VrVector3 CaravanTableCentre = { 0.45f, 0.5f * (CaravanNearEdge + CaravanFarEdge), 0.0f };
    // A surface of the world takes the mat when the rays down at its centre
    // and corners all meet it this high above the player's feet, this flat
    constexpr float     CaravanSurfaceLow  = 55.0f;
    constexpr float     CaravanSurfaceHigh = 110.0f;
    constexpr float     CaravanSurfaceFlat = 3.0f;
    constexpr float     CaravanRayTop      = 120.0f;
    // The table opens out of black
    constexpr float     CaravanFadeIn  = 0.5f;
    // The lasers point at the whole mat, just above its cloth (z 1.1).
    // A slot is on the laser within this reach of where the laser meets the
    // mat, the one already chosen favoured by the stickiness, in the mat's
    // units (1.4 cm between two rows at scale 1). A hand card is on the
    // laser where the laser meets it first.
    constexpr VrVector3 CaravanMatCentre = { 0.45f, 0.5f * (CaravanNearEdge + CaravanFarEdge), 1.1f };
    constexpr float     CaravanMatSizeX  = 56.9f;
    constexpr float     CaravanMatSizeY  = CaravanFarEdge - CaravanNearEdge;
    // The hand's card shapes, in their own plane: 3.5 x 2.5 units
    constexpr float     CaravanCardHalfX = 1.75f;
    constexpr float     CaravanCardHalfY = 1.25f;
    constexpr float     CaravanSlotReach = 3.0f;
    constexpr float     CaravanStickiness = 0.4f;
    // A click waits this long for the menu's step to what the laser is on
    constexpr float     CaravanClickWait = 0.6f;
    // A stick past this moves the menu's selection itself
    constexpr float     CaravanStickRest = 0.2f;
    // Deeper than this below the hand's or a grid's root, no card or slot
    constexpr uint32_t  CaravanNodeDepth = 8;
    // Deeper than this below the scene's root, no model of a track's row
    constexpr uint32_t  CaravanRowModelDepth = 4;
    // The values of the tracks lie on a square of the mat, from the
    // player's edge across the middle of the tracks, just above the cloth:
    // the player's past their seventh row (y -18.2), the opponent's past
    // theirs (y 3.3)
    constexpr float     CaravanBoardSide   = 27.0f;
    constexpr float     CaravanBoardHeight = 1.15f;
    constexpr float     CaravanValueNearY  = -19.6f;
    constexpr float     CaravanValueFarY   = 4.8f;

    // Top of the blackjack table, in its model
    constexpr float     BlackJackHalfX = 22.75f;
    constexpr float     BlackJackHalfY = 37.2f;
    constexpr float     FeltHeight     = 0.1f;

    // What can be pointed at on the roulette felt, around the selector's
    // limits. The selector and the chips are on the felt.
    constexpr float     RouletteFeltMargin = 5.0f;

    // Frames a gesture holds its button
    constexpr uint32_t  PulseFrames    = 3;

    // Haptics, as the game's rumble on a gamepad
    constexpr float     HapticPeriod   = 0.06f;
    constexpr int64_t   HapticPulse    = 50'000'000;
    constexpr int64_t   HapticTick     = 30'000'000;

    // Vertical space between the panel and what is below it, in metres, and
    // the steepest the line to the panel can be
    constexpr float     BoardBottom    = 0.08f;
    constexpr float     BoardPitch     = 0.9f;

    constexpr float     TerminalDistance = 0.7f;
    constexpr float     TerminalWidth    = 0.62f;

    // The Reflectron's projector a little larger than the terminal: the
    // face on its screen is what the player looks at
    constexpr float     ReflectronDistance = 0.75f;
    constexpr float     ReflectronWidth    = 0.8f;

    // The glass of the menu's monitor (TerminalInterface01.nif), in the frame
    // of its screen: findings/minigames.md §12
    constexpr VrScreenGlass MenuScreenGlass = {
      { 0.0f, -0.02f, -0.03f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, 6.2f, 4.46f };

    // The glass of the world's terminals, in the frame of the shape that is it
    struct TerminalScreenModel {
      const char*   shape;
      VrScreenGlass glass;
    };

    constexpr VrScreenGlass DeskTerminalGlass = {
      { 5.19f, 0.02f, 23.48f }, { 0.0f, 1.0f, 0.0f }, { -0.0865f, 0.0f, 0.9962f }, 21.29f, 17.4f };

    constexpr TerminalScreenModel TerminalScreens[] = {
      // terminaldesk01.nif, nvdlc01_terminaldesk01.nif
      { "TerminalDesk01base:1", DeskTerminalGlass },
      // the ruined vaults' terminaldeskr01.nif
      { "TerminalDeskR01:1",    DeskTerminalGlass },
      // terminal01.nif, the wall terminal
      { "T01Screen01:1", { { 22.48f, -1.69f, 12.68f }, { 0.0f, 1.0f, 0.0f }, { -0.005f, 0.0f, 1.0f }, 30.78f, 25.15f } },
      // terminal02.nif
      { "T01Screen02:1", { { 24.34f, -1.69f, 8.59f }, { 0.0f, 1.0f, 0.0f }, { 0.1687f, 0.0f, 0.9857f }, 30.78f, 25.15f } } };

    // The lines of text drawn on the world's wall terminals, which the
    // menu's screen replaces
    constexpr const char* TerminalFakeTexts[] = {
      "Text01", "Text02", "Text03", "Text04", "Text05", "Text06" };

    // The glass shape is four levels below a terminal's root
    constexpr uint32_t  TerminalScreenDepth = 8;
    // Part of the world's glass the menu's screen takes, inside the frame
    constexpr float     TerminalScreenFill  = 0.95f;

    // How the scenes lie on the casino's objects, in the models' space:
    // findings/minigames.md §6. The game's table is only a top, on the
    // world's, which holds it up. Its felt is above all of the world's top.
    // The blackjack table is round on the players' side, the stools around
    // it stay free. The roulette dealer stands in a recess of the table's
    // side, from x = -24 to the table's ends.
    constexpr VrModelArea BlackJackAreas[] = {
      { -13.0f, 46.3f, -90.2f, 85.5f },
      { -30.0f, 46.3f, -70.0f, 66.0f },
      { -45.9f, 46.3f, -45.0f, 40.0f } };

    constexpr VrModelArea RouletteAreas[] = {
      { -24.0f, 63.8f, -86.4f, 87.2f } };

    constexpr VrModelArea SlotMachineAreas[] = {
      { -20.3f, 20.3f, -16.9f, 16.9f } };

    constexpr VrSceneFit BlackJackFit = {
      { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },
      { 0.2f, -2.35f, 76.4f }, 2.0f,
      { -62.0f, 16.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
      BlackJackAreas, 3 };

    constexpr VrSceneFit RouletteFit = {
      { 0.0f, 1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },
      { 23.0f, 32.4f, 77.6f }, 1.28f,
      { 80.0f, 32.4f, 0.0f }, { -1.0f, 0.0f, 0.0f },
      RouletteAreas, 1 };

    // Above the object's origin, at the scene's size. The reels turn around
    // 36 units, 38 from the seat: looking down on them by about 12 degrees, as
    // from a stool, puts the head 22 units below the top of the machine. The
    // menu's camera, far from the machine, is at 26.9: seen from the seat, that
    // is from under it. The height of the game's camera, a third-person one on
    // a stool, is at the raised screen.
    constexpr float SlotMachineEyeHeight = 44.0f;

    // The root of the scene is 175 units ahead of the menu's camera and 18
    // below it, and the machine's feet are 1.2 units below its origin
    constexpr VrSceneFit SlotMachineFit = {
      { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f },
      { 0.0f, -245.0f, 26.9f }, 1.4f,
      { 0.0f, -45.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
      SlotMachineAreas, 1, SlotMachineEyeHeight };

    // The vigor tester's front is along its model's +Y: the counter with
    // its buttons stands out to y = 18.6, the cards are behind the glass at
    // y = -2.2, its back against the wall. The player stands before the
    // counter, about 65 cm from it, and keeps the game camera's height.
    constexpr VrModelArea VigorTesterAreas[] = {
      { -40.9f, 40.9f, -16.6f, 18.6f } };
    constexpr VrVector3 VigorTesterSeat    = { 0.0f, 64.0f, 0.0f };
    constexpr VrVector3 VigorTesterFacing  = { 0.0f, -1.0f, 0.0f };
    // The middle of the cards and bulbs: the lasers' rectangle lies there,
    // so that a point of it is where the menu's camera picks a button
    constexpr VrVector3 VigorTesterFace    = { 0.0f, -15.0f, 92.0f };
    // The rectangle is placed again when it moves more than this, in metres
    constexpr float     PanelMoves         = 0.01f;
    // More buttons in one list than this means it was not read correctly
    constexpr uint32_t  MaxVigorButtons    = 256;

    // The roulette wheel of the world stands above the game's table
    constexpr const char* RouletteWorldWheel = "Object02:0";

    // More entries than this means the list was not read correctly
    constexpr uint32_t  MaxProcessActors = 4096;
    // The pelvis is two levels below a skeleton's root
    constexpr uint32_t  PelvisDepth      = 6;
    // Characters logged at the opening of a menu, to see who stands where
    constexpr float     LoggedActorRange = 200.0f;
    // Frames between two looks at who stands in the way
    constexpr uint32_t  ActorsCheckFrames = 6;

    VrMinigames* g_minigames = nullptr;

    // Set during the draw of a scene that lies on an object of the world
    bool g_keepWorldDepth = false;
    bool g_clearHooked    = false;

    template<typename T>
    T& field(void* object, uintptr_t offset) {
      return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
    }

    template<typename Fn>
    Fn game(uintptr_t address) {
      return reinterpret_cast<Fn>(address);
    }

    template<typename T>
    T global(uintptr_t address) {
      return *reinterpret_cast<T*>(address);
    }

    bool isMenuActive(uint32_t menuId) {
      return game<IsMenuActiveFn>(VrGame::IsMenuActive)(menuId, 0);
    }

    bool isTopMenu(uint32_t menuId) {
      return game<IsTopMenuFn>(VrGame::IsTopMenu)(menuId);
    }

    uintptr_t cameraAddress(uint32_t menuId) {
      switch (menuId) {
        case VrGame::LockPickMenuId:    return VrGame::LockPickCamera;
        case VrGame::SlotMachineMenuId: return VrGame::SlotMachineCamera;
        case VrGame::BlackJackMenuId:   return VrGame::BlackJackCamera;
        case VrGame::RouletteMenuId:    return VrGame::RouletteCamera;
        case VrGame::CaravanMenuId:     return VrGame::CaravanCamera;
        case VrGame::LoveTesterMenuId:  return VrGame::LoveTesterCamera;
        default:                        return 0;
      }
    }

    uintptr_t sceneRender(uint32_t menuId) {
      for (const auto& scene : VrGame::MenuScenes) {
        if (scene.menuId == menuId)
          return scene.render;
      }

      return 0;
    }

    float clamp(float value, float low, float high) {
      return std::min(std::max(value, low), high);
    }

    const char* kindName(uint32_t menuId) {
      switch (menuId) {
        case VrGame::LockPickMenuId:    return "lock";
        case VrGame::SlotMachineMenuId: return "slot machine";
        case VrGame::BlackJackMenuId:   return "blackjack table";
        case VrGame::RouletteMenuId:    return "roulette table";
        case VrGame::CaravanMenuId:     return "Caravan table";
        case VrGame::LoveTesterMenuId:  return "vigor tester";
        case VrGame::RaceSexMenuId:     return "Reflectron";
        default:                        return "terminal";
      }
    }

    bool hasName(const char* path, const char* lowerName) {
      std::string lower = path;

      for (char& c : lower)
        c = char(std::tolower(static_cast<unsigned char>(c)));

      return lower.find(lowerName) != std::string::npos;
    }

    // Where an object of a scene graph is, in the space of its graph's root
    VrWorldObject worldPose(uint8_t* object) {
      auto rotation = reinterpret_cast<const float*>(object + VrGame::ObjectWorldRotation);

      VrWorldObject pose;
      pose.x        = { rotation[0], rotation[3], rotation[6] };
      pose.y        = { rotation[1], rotation[4], rotation[7] };
      pose.z        = { rotation[2], rotation[5], rotation[8] };
      pose.position = field<VrVector3>(object, VrGame::ObjectWorldTranslation);
      pose.scale    = field<float>(object, VrGame::ObjectWorldScale);
      return pose;
    }

    // The vigor tester's cabinet in the menu's scene, null until it is loaded
    uint8_t* vigorTesterCabinet() {
      auto menu = global<uint8_t*>(VrGame::LoveTesterInstance);
      return menu ? field<uint8_t*>(menu, VrGame::LoveTesterCabinetModel) : nullptr;
    }

    // The game's clear of a scene: it stays out of the world's depth buffer
    // when the scene lies on an object of the world
    void __fastcall sceneClearHook(void* renderer, void* unused, const void* rect, uint32_t flags) {
      if (g_keepWorldDepth)
        flags &= ~VrGame::ClearDepthBuffer;

      if (flags)
        game<ClearFn>(VrGame::RendererClearBuffer)(renderer, nullptr, rect, flags);
    }

  }


  std::unique_ptr<VrMinigames> VrMinigames::create(IDirect3DDevice9* device, IVRBackend& backend) {
    bool found = !g_minigames
      && VrGameMemory::matches(VrGame::IsTopMenu, VrGame::IsTopMenuPrologue, sizeof(VrGame::IsTopMenuPrologue))
      && VrGameMemory::matches(VrGame::RouletteHoveredBet, VrGame::RouletteHoveredBetPrologue, sizeof(VrGame::RouletteHoveredBetPrologue))
      && VrGameMemory::matches(VrGame::GetActivateRef, VrGame::GetActivateRefPrologue, sizeof(VrGame::GetActivateRefPrologue))
      && VrGameMemory::matchesUnlessRewritten(VrGame::RefGet3D, VrGame::RefGet3DPrologue, sizeof(VrGame::RefGet3DPrologue))
      && VrGameMemory::readable(VrGame::ProcessActors, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::ProcessHighEnd, sizeof(uint32_t))
      && VrGameMemory::readable(VrGame::LockPickInstance, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::BlackJackInstance, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::RouletteInstance, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::SlotMachineInstance, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::CaravanCamera, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::CaravanInstance, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::CaravanSceneRoot, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::LoveTesterInstance, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::LoveTesterCamera, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::LockPickSliderTrait, sizeof(uint32_t))
      && VrGameMemory::readable(VrGame::TerminalVtable, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::RaceSexRenderedVtable, sizeof(uintptr_t));

    if (!found)
      return nullptr;

    std::unique_ptr<VrMinigames> minigames(new VrMinigames(device, backend));
    g_minigames = minigames.get();

    minigames->m_caravanTable = VrSceneModel::create(CaravanTableModel);

    // Without them, the game's tiles keep Caravan's values
    minigames->m_canHideTiles =
         VrGameMemory::matches(VrGame::TileSetFloat, VrGame::TileSetFloatPrologue, sizeof(VrGame::TileSetFloatPrologue))
      && VrGameMemory::matches(VrGame::TileGetByName, VrGame::TileGetByNamePrologue, sizeof(VrGame::TileGetByNamePrologue));

    // Without the rays, Caravan's table never takes a surface of the world
    minigames->m_canPick = VrGameMemory::matches(VrGame::PickDataConstruct,
      VrGame::PickDataConstructPrologue, sizeof(VrGame::PickDataConstructPrologue));

    // The world's terminals and the Reflectron need the console's hook to
    // get a screen for both eyes; without it they stay on the panel
    VrConsoleScreen::setAfterOffscreen(&VrMinigames::afterOffscreen);

    // The calls stay redirected if the option is turned off and on again
    if (!g_clearHooked) {
      auto clearHook = reinterpret_cast<const void*>(&sceneClearHook);

      bool keepsDepth = VrGameMemory::redirectCall(VrGame::BlackJackDepthClearSite, VrGame::RendererClearBuffer, clearHook);
      keepsDepth = VrGameMemory::redirectCall(VrGame::SlotMachineDepthClearSite, VrGame::RendererClearBuffer, clearHook) && keepsDepth;
      keepsDepth = VrGameMemory::redirectCall(VrGame::RouletteDepthClearSite, VrGame::RendererClearBuffer, clearHook) && keepsDepth;
      keepsDepth = VrGameMemory::redirectCall(VrGame::LoveTesterDepthClearSite, VrGame::RendererClearBuffer, clearHook) && keepsDepth;
      keepsDepth = VrGameMemory::redirectCall(VrGame::CaravanDepthClearSite, VrGame::RendererClearBuffer, clearHook) && keepsDepth;

      if (!keepsDepth)
        Logger::warn("VR: A depth clear of the scenes on the world's objects was not found, they are drawn over the world in front of them");

      g_clearHooked = true;
    }

    return minigames;
  }


  VrMinigames::VrMinigames(IDirect3DDevice9* device, IVRBackend& backend)
  : m_device(device), m_backend(backend) {

  }


  VrMinigames::~VrMinigames() {
    leaveObject();
    g_keepWorldDepth = false;
    g_minigames = nullptr;
  }


  uint32_t VrMinigames::menuId() const {
    switch (m_kind) {
      case Kind::Lock:        return VrGame::LockPickMenuId;
      case Kind::SlotMachine: return VrGame::SlotMachineMenuId;
      case Kind::BlackJack:   return VrGame::BlackJackMenuId;
      case Kind::Roulette:    return VrGame::RouletteMenuId;
      case Kind::Caravan:     return VrGame::CaravanMenuId;
      case Kind::VigorTester: return VrGame::LoveTesterMenuId;
      case Kind::Reflectron:  return VrGame::RaceSexMenuId;
      default:                return 0;
    }
  }


  VrMinigames::Kind VrMinigames::findKind() const {
    if (isMenuActive(VrGame::LockPickMenuId))
      return Kind::Lock;

    if (isMenuActive(VrGame::SlotMachineMenuId))
      return Kind::SlotMachine;

    if (isMenuActive(VrGame::BlackJackMenuId))
      return Kind::BlackJack;

    if (isMenuActive(VrGame::RouletteMenuId))
      return Kind::Roulette;

    if (isMenuActive(VrGame::CaravanMenuId))
      return Kind::Caravan;

    if (isMenuActive(VrGame::LoveTesterMenuId))
      return Kind::VigorTester;

    void* rendered = game<PointerFn>(VrGame::GetCurrentRenderedMenu)();

    if (!rendered || rendered == game<PointerFn>(VrGame::GetPipBoy)())
      return Kind::None;

    uintptr_t vtable = field<uintptr_t>(rendered, 0);

    if (vtable == VrGame::TerminalVtable)
      return Kind::Terminal;

    if (vtable == VrGame::RaceSexRenderedVtable)
      return Kind::Reflectron;

    return Kind::None;
  }


  void VrMinigames::update(
    const VrMinigameFrame&        frame,
    const VrInputState&           input,
    const VrPointerSurfaceState&  surface) {
    Kind kind = frame.stereo ? findKind() : Kind::None;

    // Until that menu closes
    if (kind != m_declined)
      m_declined = Kind::None;
    else
      kind = Kind::None;

    // The game may forget it as soon as a menu is open
    if (!frame.stereo)
      m_activateRef = game<PointerFn>(VrGame::GetActivateRef)();

    // A terminal goes from one rendered menu to the next (hacking, then the
    // computer) on the same monitor: the scene stays where it is
    if (kind == m_kind && showsRenderedMenu()) {
      auto rendered = static_cast<uint8_t*>(game<PointerFn>(VrGame::GetCurrentRenderedMenu)());

      if (rendered != m_rendered) {
        if (renderedMenuReady(rendered))
          m_rendered = rendered;
        else
          close();
      }
    }

    if (kind != m_kind) {
      close();

      if (kind != Kind::None && open(kind, frame)) {
        m_kind = kind;
        m_panelRevision++;
        Logger::info(str::format("VR: The ", kindName(menuId()),
          m_onObject ? " lies on the world's object, " : " stands in the room, ",
          m_space.unitsPerMetre, " units per metre, the panel ", m_panel.width, " m wide"));
      }
    }

    m_pad = VrMinigamePad();
    m_pad.active = active();

    if (!active())
      return;

    if (m_onObject)
      standOnObject(frame);

    // A menu over the mini-game (a message, a quantity) has the input
    bool onTop = showsRenderedMenu() || isTopMenu(menuId());

    if (m_pulseFrames) {
      m_pad.buttons |= m_pulse;
      m_pulseFrames--;
    }

    switch (m_kind) {
      case Kind::Lock:
        if (onTop)
          updateLock(input, frame.dt);
        break;

      case Kind::SlotMachine:
        if (onTop)
          updateSlotMachine(input);
        break;

      case Kind::BlackJack:
        if (onTop)
          updateBlackJack(input, frame.dt);
        break;

      case Kind::Roulette:
        m_hasSurface = onTop;

        if (onTop)
          updateRoulette(surface);
        break;

      case Kind::Caravan:
        if (onTop)
          updateCaravan(surface, input, frame.dt);
        else
          m_hasSurface = false;
        break;

      case Kind::VigorTester:
        if (onTop)
          aimVigorTesterCamera(input);
        break;

      default:
        break;
    }
  }


  bool VrMinigames::open(Kind kind, const VrMinigameFrame& frame) {
    m_pulseFrames = 0;
    m_hasSurface  = false;
    m_handDrives  = true;
    m_betTrigger  = false;
    m_pinAngle    = 0.0f;
    m_caravanCard = -1;
    m_caravanSlot = -1;
    m_caravanTrigger = false;
    m_caravanClick   = false;
    m_caravanStepping = false;
    m_leverHeld   = { };
    m_levers      = { };
    m_signals     = VrTableSignals();
    m_tension     = VrLockTension();

    if (kind == Kind::Terminal)
      return placeRenderedMenu(frame, true, TerminalDistance, TerminalWidth);

    if (kind == Kind::Reflectron)
      return placeRenderedMenu(frame, false, ReflectronDistance, ReflectronWidth);

    Kind previous = m_kind;
    m_kind = kind;
    uint32_t id = menuId();
    m_kind = previous;

    void* camera = global<void*>(cameraAddress(id));

    // Not yet: the menu builds its scene in its first frames
    if (!camera)
      return false;

    VrGameCamera access;
    access.attach(camera);
    VrGameCameraPose pose = access.readPose();

    switch (kind) {
      case Kind::Lock:
        m_space = vrSceneSpaceInFront(frame.head, LockTarget, LockDistance, LockDrop, LockUnits);
        m_lock  = m_space.anchor;
        placeBoard(frame, false);
        break;

      case Kind::SlotMachine:
        if (!placeOnObject(id, frame))
          m_space = vrSceneSpaceInFront(frame.head, SlotTarget, SlotDistance, SlotDrop, VrGameUnitsPerMetre);

        placeBoard(frame, false);
        break;

      case Kind::BlackJack:
      case Kind::Roulette:
        if (!placeOnObject(id, frame))
          m_space = vrSceneSpaceAroundHead(frame.head, pose.position, pose.forward, VrGameUnitsPerMetre);

        placeBoard(frame, true);
        break;

      case Kind::Caravan:
        if (!placeCaravanTable(frame))
          m_space = vrSceneSpaceAroundHead(frame.head, CaravanEye, CaravanFacing, CaravanUnits);

        placeBoard(frame, true);
        m_backend.fadeView(0.0f, 1.0f);
        m_backend.fadeView(CaravanFadeIn, 0.0f);
        break;

      case Kind::VigorTester:
        // The machine is the scene: away from it, nothing in the room would
        // say what it is. The menu builds the cabinet in its first frames.
        if (!vigorTesterCabinet())
          return false;

        if (!placeOnObject(id, frame)) {
          Logger::info("VR: The vigor tester has no machine of the world to lie on, it stays on the panel");
          m_declined = kind;
          return false;
        }
        break;

      default:
        return false;
    }

    if (kind == Kind::Roulette) {
      placeRouletteSurface();
      m_hasSurface = true;
    }

    return true;
  }


  bool VrMinigames::placeOnObject(uint32_t id, const VrMinigameFrame& frame) {
    auto reference = static_cast<uint8_t*>(m_activateRef);

    if (!reference || !frame.hasReference)
      return false;

    uint8_t* base = field<uint8_t*>(reference, VrGame::RefBaseForm);

    if (!base || field<uint8_t>(base, VrGame::FormType) != VrGame::FormTypeActivator)
      return false;

    const char* model = field<const char*>(base, VrGame::ActivatorModelPath);

    if (!model)
      return false;

    const VrSceneFit* fit = nullptr;

    if (id == VrGame::BlackJackMenuId && hasName(model, "cardtable01"))
      fit = &BlackJackFit;
    else if (id == VrGame::RouletteMenuId && hasName(model, "roulettetable01"))
      fit = &RouletteFit;
    else if (id == VrGame::SlotMachineMenuId && hasName(model, "slot_machine0"))
      fit = &SlotMachineFit;
    else if (id == VrGame::LoveTesterMenuId && hasName(model, "nv_vitomaticvigortester") && fitVigorTester())
      fit = &m_modelFit;

    auto root = fit ? activateRoot() : nullptr;

    if (!root) {
      Logger::info(str::format("VR: No place for the scene on ", model));
      return false;
    }

    // The machines are whole in the scene. The tables' are only tops, and the
    // world's holds them up: only what stands above that top goes.
    if (fit == &SlotMachineFit || fit == &m_modelFit)
      hideRoot(root);
    else if (fit == &RouletteFit)
      VrMenuScenes::hideObject(reinterpret_cast<uintptr_t>(root), RouletteWorldWheel, m_objectHidden);

    takeSeat(root, worldPose(root), fit, frame);
    return true;
  }


  uint8_t* VrMinigames::activateRoot() const {
    auto reference = static_cast<uint8_t*>(m_activateRef);

    if (!reference)
      return nullptr;

    auto vtable = field<uint8_t*>(reference, 0);
    return static_cast<uint8_t*>((*reinterpret_cast<Get3DFn*>(vtable + VrGame::RefGet3DSlot))(reference));
  }


  void VrMinigames::takeSeat(uint8_t* root, const VrWorldObject& object, const VrSceneFit* fit, const VrMinigameFrame& frame) {
    m_object     = object;
    m_fit        = fit;
    m_viewCamera = vrSeatCamera(m_object, *fit,
      vrComputeEyeInReference(frame.reference, frame.head), frame.cameraHeight, VrGameUnitsPerMetre);
    m_onObject   = true;
    m_seated     = true;

    Logger::info(str::format("VR: The seat's camera is ", m_viewCamera.position.z - m_object.position.z,
      " units above the object, the game's ", frame.cameraHeight - m_object.position.z));

    if (root)
      keep(root);

    updateActorsInTheWay(true);
    standOnObject(frame);
  }


  bool VrMinigames::placeCaravanTable(const VrMinigameFrame& frame) {
    auto menu       = global<uint8_t*>(VrGame::CaravanInstance);
    auto challenger = menu ? field<uint8_t*>(menu, VrGame::CaravanChallenger) : nullptr;
    auto player     = global<uint8_t*>(VrGame::Player);

    if (!challenger || !player || !frame.hasReference)
      return false;

    VrVector3 feet  = field<VrVector3>(player, VrGame::RefLocation);
    VrVector3 other = field<VrVector3>(challenger, VrGame::RefLocation);
    float distance  = vrLength(VrVector3 { other.x - feet.x, other.y - feet.y, 0.0f });

    VrWorldObject table;

    if (distance > CaravanReach || std::abs(other.z - feet.z) > CaravanSurfaceHigh
     || !vrTableBetween(feet, other, CaravanArea.maxY, CaravanGap, feet.z + CaravanTableTop, table)) {
      Logger::info(str::format("VR: The Caravan challenger stands ", distance, " units away, the table stands in the room"));
      return false;
    }

    float surface = 0.0f;
    bool  onWorld = findCaravanSurface(player, table, feet.z, surface);

    if (onWorld)
      table.position.z = surface;
    else
      showCaravanTable();

    Logger::info(str::format("VR: The Caravan table goes between the player and the challenger, ", distance,
      " units apart, ", onWorld ? "on a surface of the world " : "at a table's height, ",
      table.position.z - feet.z, " units above the feet"));

    takeSeat(nullptr, table, &CaravanFit, frame);
    return true;
  }


  void VrMinigames::showCaravanTable() {
    void* scene = global<void*>(VrGame::CaravanSceneRoot);

    if (!m_caravanTable || !scene)
      return;

    VrVector3 origin = CaravanTableCentre;
    origin.z = -CaravanTableTop / CaravanScale;

    if (m_caravanTable->show(scene, origin, 1.0f / CaravanScale))
      Logger::info("VR: The game's diner table stands under the Caravan mat");
  }


  bool VrMinigames::findCaravanSurface(uint8_t* player, const VrWorldObject& table, float feet, float& surface) {
    if (!m_canPick)
      return false;

    uint8_t* cell  = reinterpret_cast<GetPartFn>(VrGame::RefGetParentCell)(player, nullptr);
    uint8_t* world = cell ? reinterpret_cast<GetPartFn>(VrGame::CellGetWorld)(cell, nullptr) : nullptr;

    if (!world)
      return false;

    // The game's grab ray: it skips the player
    uint32_t playerFilter = 0;
    reinterpret_cast<GetFilterFn>(VrGame::GetCollisionFilter)(player, nullptr, &playerFilter);
    uint32_t rayFilter = (playerFilter >> VrGame::FilterGroupShift << VrGame::FilterGroupShift) | VrGame::LayerItemPick;

    const VrVector2 points[] = {
      { 0.5f * (CaravanArea.minX + CaravanArea.maxX), 0.5f * (CaravanArea.minY + CaravanArea.maxY) },
      { CaravanArea.minX, CaravanArea.minY }, { CaravanArea.maxX, CaravanArea.minY },
      { CaravanArea.minX, CaravanArea.maxY }, { CaravanArea.maxX, CaravanArea.maxY } };

    float lowest  = FLT_MAX;
    float highest = -FLT_MAX;
    bool  meets   = true;

    for (const VrVector2& point : points) {
      alignas(16) uint8_t pick[VrGame::PickDataSize];
      VrVector3 from = table.position + table.x * point.x + table.y * point.y;
      from.z = feet + CaravanRayTop;
      VrVector3 length = { 0.0f, 0.0f, CaravanSurfaceLow - CaravanRayTop };

      reinterpret_cast<PickConstructFn>(VrGame::PickDataConstruct)(pick, nullptr);
      reinterpret_cast<PickSetFilterFn>(VrGame::PickDataSetFilter)(pick, nullptr, rayFilter);
      reinterpret_cast<PickSetPointFn>(VrGame::PickDataSetFrom)(pick, nullptr, &from);
      reinterpret_cast<PickSetPointFn>(VrGame::PickDataSetLength)(pick, nullptr, &length);

      auto vtable = field<uint8_t*>(world, 0);

      if (!(*reinterpret_cast<PickObjectFn*>(vtable + VrGame::WorldPickObjectSlot))(world, nullptr, pick)) {
        meets = false;
        break;
      }

      float z = from.z + length.z * field<float>(pick, VrGame::PickDataHitFraction);
      lowest  = std::min(lowest, z);
      highest = std::max(highest, z);
    }

    if (!meets) {
      Logger::info("VR: No surface of the world under the whole Caravan table");
      return false;
    }

    if (highest - lowest > CaravanSurfaceFlat || highest - feet > CaravanSurfaceHigh) {
      Logger::info(str::format("VR: The surface under the Caravan table is uneven, from ",
        lowest - feet, " to ", highest - feet, " units above the feet"));
      return false;
    }

    surface = highest;
    return true;
  }


  void VrMinigames::standAt(uint8_t* root, const VrWorldObject& object, const VrSceneFit* fit, const VrMinigameFrame& frame) {
    // The view stays the player's: nothing stands between the eyes and the
    // object that the player does not see already
    m_object     = object;
    m_fit        = fit;
    m_viewCamera = frame.camera;
    m_onObject   = true;
    m_seated     = false;

    keep(root);
    standOnObject(frame);
  }


  void VrMinigames::viewFrom(const VrGameCameraPose& camera) {
    if (!m_onObject || m_seated)
      return;

    m_viewCamera = camera;
    placeOnObjectSpace();
  }


  bool VrMinigames::placeTerminalOnScreen(const VrMinigameFrame& frame) {
    uint8_t* root   = frame.hasReference ? activateRoot() : nullptr;
    auto     screen = field<uint8_t*>(m_rendered, VrGame::TerminalScreen);

    if (!root)
      return false;

    uint8_t* glass = nullptr;
    const TerminalScreenModel* model = nullptr;

    for (const auto& candidate : TerminalScreens) {
      glass = reinterpret_cast<uint8_t*>(vrFindGameObject(reinterpret_cast<uintptr_t>(root),
        candidate.shape, TerminalScreenDepth));

      if (glass) {
        model = &candidate;
        break;
      }
    }

    VrWorldObject object = worldPose(root);

    if (!glass || object.scale <= 0.0f) {
      Logger::info("VR: The terminal has no screen of the world to show on, it stands in the room");
      return false;
    }

    m_modelFit = vrSceneFitOnScreen(worldPose(screen), MenuScreenGlass,
      vrObjectInObject(object, worldPose(glass)), model->glass, TerminalScreenFill);

    for (const char* text : TerminalFakeTexts)
      VrMenuScenes::hideObject(reinterpret_cast<uintptr_t>(root), text, m_objectHidden);

    Logger::info(str::format("VR: The terminal shows on the world's ", model->shape));
    standAt(root, object, &m_modelFit, frame);
    return true;
  }


  void VrMinigames::placeTerminalPanel() {
    // A laser on this rectangle gives the cursor position the menu's camera
    // sees on its screen, which lies on the world's
    VrPanelPlacement panel = vrSceneViewRectangle(m_space, m_renderedDepth, m_renderedFrustum);

    if (vrLength(panel.pose.position - m_panel.pose.position) > PanelMoves
     || std::abs(panel.width - m_panel.width) > PanelMoves)
      m_panelRevision++;

    m_panel = panel;
  }


  void VrMinigames::standOnObject(const VrMinigameFrame& frame) {
    m_reference = frame.reference;

    if (m_seated) {
      hidePlayerBody();

      // The world runs behind the casino's games, and people walk
      if (!m_actorsWait) {
        m_actorsWait = ActorsCheckFrames;
        updateActorsInTheWay(false);
      }

      m_actorsWait--;
    }

    placeOnObjectSpace();
  }


  void VrMinigames::placeOnObjectSpace() {
    m_space = vrSceneSpaceOnObject(m_reference, m_viewCamera, m_object, *m_fit, VrGameUnitsPerMetre);

    if (m_fit == &RouletteFit)
      placeRouletteSurface();

    if (m_fit == &m_modelFit) {
      if (m_rendered)
        placeTerminalPanel();
      else
        placeVigorTesterPanel();
    }
  }


  bool VrMinigames::fitVigorTester() {
    void*    camera  = global<void*>(VrGame::LoveTesterCamera);
    uint8_t* cabinet = vigorTesterCabinet();

    if (!camera || !cabinet)
      return false;

    // Kept before the lasers move it. The menu makes a camera of its own
    // each time it opens and lets it go when it closes.
    VrGameCamera access;
    access.attach(camera);
    VrGameCameraState state = access.save();

    m_vigorCamera = access.readPose();
    m_vigorFrustum.left   = state.frustum[0];
    m_vigorFrustum.right  = state.frustum[1];
    m_vigorFrustum.top    = state.frustum[2];
    m_vigorFrustum.bottom = state.frustum[3];
    m_vigorHand     = VrHand::Right;
    m_vigorTriggers = { };

    VrWorldObject model = worldPose(cabinet);

    if (model.scale <= 0.0f)
      return false;

    m_modelFit = vrSceneFitOfModel(model);
    m_modelFit.seat      = VigorTesterSeat;
    m_modelFit.facing    = VigorTesterFacing;
    m_modelFit.areas     = VigorTesterAreas;
    m_modelFit.areaCount = 1;
    return true;
  }


  void VrMinigames::placeVigorTesterPanel() {
    uint8_t* cabinet = vigorTesterCabinet();

    if (!cabinet)
      return;

    VrWorldObject model = worldPose(cabinet);
    VrVector3 face = model.position + (model.x * VigorTesterFace.x + model.y * VigorTesterFace.y
      + model.z * VigorTesterFace.z) * model.scale;

    float distance = vrDot(face - m_vigorCamera.position, m_vigorCamera.forward);

    if (distance <= 0.0f)
      return;

    // A laser on this rectangle gives the cursor position the menu's camera
    // sees on the machine where the laser points
    VrPanelPlacement panel = vrCameraViewRectangle(m_space, m_vigorCamera, distance, m_vigorFrustum);

    if (vrLength(panel.pose.position - m_panel.pose.position) > PanelMoves
     || std::abs(panel.width - m_panel.width) > PanelMoves)
      m_panelRevision++;

    m_panel = panel;
  }


  bool VrMinigames::vigorTesterButtonOnRay(const VrVector3& origin, const VrVector3& direction) {
    auto menu = global<uint8_t*>(VrGame::LoveTesterInstance);

    if (!menu)
      return false;

    uint32_t page = field<uint32_t>(menu, VrGame::LoveTesterPage);

    if (page > VrGame::LoveTesterLastPage)
      return false;

    const uintptr_t lists[] = {
      VrGame::LoveTesterPages + VrGame::LoveTesterPageSize * page,
      VrGame::LoveTesterCommonButtons };

    for (uintptr_t list : lists) {
      auto     buttons = field<uint8_t* const*>(menu, list + VrGame::SimpleArrayBase);
      uint32_t count   = field<uint32_t>(menu, list + VrGame::SimpleArraySize);

      if (!buttons || count > MaxVigorButtons)
        continue;

      for (uint32_t i = 0; i < count; i++) {
        uint8_t* node  = buttons[i] ? field<uint8_t*>(buttons[i], VrGame::LoveTesterButtonNode) : nullptr;
        auto*    bound = node ? field<const float*>(node, VrGame::ObjectWorldBound) : nullptr;

        // The menu's pick leaves out what is culled
        if (!bound || (field<uint32_t>(node, VrGame::ObjectFlags) & VrGame::ObjectFlagAppCulled))
          continue;

        if (vrRayMeetsSphere(origin, direction, VrVector3 { bound[0], bound[1], bound[2] }, bound[3]))
          return true;
      }
    }

    return false;
  }


  void VrMinigames::aimVigorTesterCamera(const VrInputState& input) {
    void* camera = global<void*>(VrGame::LoveTesterCamera);

    if (!camera || !m_onObject)
      return;

    // The hand the cursor follows, chosen as VrMenuPointer does: a press on
    // the rectangle takes it, and a hand that points elsewhere gives it up
    const VrAction triggers[VrHandCount] = { VrAction::MenuLeftTrigger, VrAction::MenuRightTrigger };
    std::array<VrPanelHit, VrHandCount> hits;

    for (uint32_t i = 0; i < VrHandCount; i++) {
      if (input.controllers[i].isActive)
        hits[i] = vrIntersectPanel(input.controllers[i].aimPose, m_panel);

      bool trigger = input.actions.isPressed(triggers[i]);

      if (trigger && !m_vigorTriggers[i] && hits[i].hit)
        m_vigorHand = VrHand(i);

      m_vigorTriggers[i] = trigger;
    }

    uint32_t other = 1u - uint32_t(m_vigorHand);

    if (!hits[uint32_t(m_vigorHand)].hit && hits[other].hit)
      m_vigorHand = VrHand(other);

    // The menu picks its buttons along its camera's view through the
    // cursor: that view is the laser. Without one, the camera is the menu's.
    VrGameCameraPose pose = m_vigorCamera;
    const VrPanelHit& hit = hits[uint32_t(m_vigorHand)];

    // The menu's click takes the whole screen. Off its buttons the triggers
    // are the gamepad's, which turn the pages.
    m_pad.decidesTarget = true;
    m_pad.overTarget    = false;

    if (hit.hit) {
      const VrPose& aim = input.controllers[uint32_t(m_vigorHand)].aimPose;
      VrVector3 origin = vrRoomToScene(m_space, aim.position);
      VrVector3 ahead  = vrRoomToScene(m_space, aim.position + vrRotate(aim.orientation, VrVector3 { 0.0f, 0.0f, -1.0f }));

      pose = vrCameraAlongRay(m_vigorCamera, m_vigorFrustum, hit.uv, origin, ahead - origin);
      m_pad.overTarget = vigorTesterButtonOnRay(origin, ahead - origin);
    }

    VrGameCamera access;
    access.attach(camera);
    access.apply(pose, m_vigorFrustum);
  }


  void VrMinigames::keep(void* object) {
    m_kept.push_back(nullptr);
    game<AssignFn>(VrGame::NiPointerAssign)(&m_kept.back(), object);
  }


  void VrMinigames::hideRoot(uint8_t* root) {
    auto flags = reinterpret_cast<uint32_t*>(root + VrGame::ObjectFlags);

    if (!(*flags & VrGame::ObjectFlagAppCulled)) {
      *flags |= VrGame::ObjectFlagAppCulled;
      m_objectHidden.push_back(flags);
    }
  }


  void VrMinigames::hidePlayerBody() {
    // A player sitting is in third person: the body stays on its stool while
    // the view is at the seat. The game turns it on and off with the view,
    // which is why it is looked at every frame.
    auto player = global<uint8_t*>(VrGame::Player);
    auto root   = player ? static_cast<uint8_t*>(game<Get3DFn>(VrGame::RefGet3D)(player)) : nullptr;

    if (!root)
      return;

    auto flags = reinterpret_cast<uint32_t*>(root + VrGame::ObjectFlags);

    if (*flags & VrGame::ObjectFlagAppCulled)
      return;

    *flags |= VrGame::ObjectFlagAppCulled;

    if (!m_bodyHidden) {
      m_bodyHidden = flags;
      keep(root);
      Logger::info("VR: The player's body is hidden while the view is at the seat");
    }
  }


  void VrMinigames::updateActorsInTheWay(bool logActors) {
    auto actors = global<const uintptr_t*>(VrGame::ProcessActors);
    auto end    = global<uint32_t>(VrGame::ProcessHighEnd);

    if (!actors || end > MaxProcessActors)
      return;

    for (uint32_t i = 0; i < end; i++) {
      auto actor = reinterpret_cast<uint8_t*>(actors[i]);

      if (!actor)
        continue;

      auto vtable = field<uint8_t*>(actor, 0);

      if (!(*reinterpret_cast<IsActorFn*>(vtable + VrGame::RefIsActorSlot))(actor))
        continue;

      auto root = static_cast<uint8_t*>((*reinterpret_cast<Get3DFn*>(vtable + VrGame::RefGet3DSlot))(actor));

      if (!root)
        continue;

      // The root stays where the reference is, whatever an animation does
      // with the body
      auto pelvis = reinterpret_cast<uint8_t*>(vrFindGameObject(reinterpret_cast<uintptr_t>(root),
        VrGame::PelvisNodeName, PelvisDepth));

      VrVector3 body = vrPointInObject(m_object,
        field<VrVector3>(pelvis ? pelvis : root, VrGame::ObjectWorldTranslation));

      bool inTheWay = vrBodyInTheWay(*m_fit, body);

      auto flags  = reinterpret_cast<uint32_t*>(root + VrGame::ObjectFlags);
      auto hidden = std::find(m_actorsHidden.begin(), m_actorsHidden.end(), flags);

      if (inTheWay && hidden == m_actorsHidden.end() && !(*flags & VrGame::ObjectFlagAppCulled)) {
        if (std::find(m_kept.begin(), m_kept.end(), static_cast<void*>(root)) == m_kept.end())
          keep(root);

        *flags |= VrGame::ObjectFlagAppCulled;
        m_actorsHidden.push_back(flags);
      } else if (!inTheWay && hidden != m_actorsHidden.end()) {
        *flags &= ~VrGame::ObjectFlagAppCulled;
        m_actorsHidden.erase(hidden);
      }

      if (logActors && std::abs(body.x) < LoggedActorRange && std::abs(body.y) < LoggedActorRange) {
        Logger::info(str::format("VR: A character at (", body.x, ", ", body.y, ", ", body.z,
          ") of the object", inTheWay ? " is in the way, it is hidden" : ""));
      }
    }
  }


  void VrMinigames::placeRouletteSurface() {
    VrVector3 centre = {
      0.5f * (VrGame::RouletteSelectorMinX + VrGame::RouletteSelectorMaxX),
      0.5f * (VrGame::RouletteSelectorMinY + VrGame::RouletteSelectorMaxY),
      FeltHeight };

    m_surface = vrSceneSurface(m_space, centre,
      VrGame::RouletteSelectorMaxX - VrGame::RouletteSelectorMinX + 2.0f * RouletteFeltMargin,
      VrGame::RouletteSelectorMaxY - VrGame::RouletteSelectorMinY + 2.0f * RouletteFeltMargin);
  }


  void VrMinigames::leaveObject() {
    if (m_caravanTable)
      m_caravanTable->hide();

    VrMenuScenes::showHidden(m_objectHidden);
    m_objectHidden.clear();
    VrMenuScenes::showHidden(m_actorsHidden);
    m_actorsHidden.clear();
    m_actorsWait = 0;

    // The game may have gone back to first person since, and wants it hidden
    auto player = global<uint8_t*>(VrGame::Player);

    if (m_bodyHidden && player && field<uint8_t>(player, VrGame::PlayerCurrentThirdPerson))
      *m_bodyHidden &= ~VrGame::ObjectFlagAppCulled;

    m_bodyHidden = nullptr;

    for (void*& object : m_kept)
      game<AssignFn>(VrGame::NiPointerAssign)(&object, nullptr);

    m_kept.clear();

    m_onObject = false;
    m_seated   = false;
    m_fit      = nullptr;
  }


  void VrMinigames::close() {
    // The menu can stay open with its scene back on the panel: its camera is
    // its own again. Once the menu has closed, it has let its camera go.
    void* vigorCamera = m_kind == Kind::VigorTester ? global<void*>(VrGame::LoveTesterCamera) : nullptr;

    if (vigorCamera && isMenuActive(VrGame::LoveTesterMenuId)) {
      VrGameCamera access;
      access.attach(vigorCamera);
      access.apply(m_vigorCamera, m_vigorFrustum);
    }

    // A try of open() that has to wait for the menu's scene may have started
    leaveObject();

    if (m_kind == Kind::None)
      return;

    Logger::info(str::format("VR: The ", kindName(menuId()), " is put away"));

    m_kind       = Kind::None;
    m_rendered   = nullptr;
    m_hasCaravanBoard    = false;
    m_caravanTilesHidden = false;
    m_hasSurface = false;
    m_panelRevision++;
    m_pad = VrMinigamePad();
    releaseTexture();
  }


  void VrMinigames::placeBoard(const VrMinigameFrame& frame, bool aboveTable) {
    m_panel.width  = frame.panelWidth;
    m_panel.height = frame.panelWidth * frame.panelAspect;

    if (!aboveTable) {
      m_panel.pose = vrComputePanelPose(frame.head, frame.panelDistance);
      return;
    }

    // The panel's lower edge just under the eyes: the lasers reach the felt
    // below it, and the tiles at the edge of the view are not over the table
    VrPose level = vrComputePanelPose(frame.head, frame.panelDistance);
    VrVector3 target = level.position + VrVector3 { 0.0f, 0.5f * m_panel.height - BoardBottom, 0.0f };

    m_panel.pose = vrComputePanelPoseTowards(frame.head, target, frame.panelDistance, BoardPitch);
  }


  bool VrMinigames::renderedMenuReady(uint8_t* rendered) {
    return rendered
        && field<bool>(rendered, VrGame::TerminalInitialized)
        && field<void*>(rendered, VrGame::TerminalCamera)
        && field<void*>(rendered, VrGame::TerminalModel)
        && field<void*>(rendered, VrGame::TerminalScreen);
  }


  bool VrMinigames::placeRenderedMenu(const VrMinigameFrame& frame, bool onScreen, float distance, float width) {
    auto rendered = static_cast<uint8_t*>(game<PointerFn>(VrGame::GetCurrentRenderedMenu)());

    if (!renderedMenuReady(rendered) || !VrConsoleScreen::hooksOffscreen())
      return false;

    void* camera = field<void*>(rendered, VrGame::TerminalCamera);
    void* model  = field<void*>(rendered, VrGame::TerminalModel);

    VrGameCamera access;
    access.attach(camera);
    VrGameCameraState state = access.save();

    VrGameFrustum frustum;
    frustum.left   = state.frustum[0];
    frustum.right  = state.frustum[1];
    frustum.top    = state.frustum[2];
    frustum.bottom = state.frustum[3];

    VrVector3 target = field<VrVector3>(model, VrGame::ObjectWorldTranslation);
    float view = (frustum.right - frustum.left) * target.x;

    if (view <= 0.0f)
      return false;

    m_rendered        = rendered;
    m_renderedDepth   = target.x;
    m_renderedFrustum = frustum;

    // On the screen of the world's terminal when there is one
    if (onScreen && placeTerminalOnScreen(frame))
      return true;

    m_space = vrSceneSpaceInFront(frame.head, target, distance, 0.0f, view / width);
    m_panel = vrSceneViewRectangle(m_space, target.x, frustum);
    return true;
  }


  void VrMinigames::press(uint16_t button) {
    m_pulse       = button;
    m_pulseFrames = PulseFrames;
    m_pad.buttons |= button;
  }


  void VrMinigames::updateLock(const VrInputState& input, float dt) {
    void* menu = global<void*>(VrGame::LockPickInstance);

    if (!menu)
      return;

    void* slider = field<void*>(menu, VrGame::LockPickSlider);
    void* range  = field<void*>(menu, VrGame::LockPickSliderRange);

    m_pad.noTriggers  = true;
    m_pad.noShoulders = true;

    const VrControllerState& right = input.controllers[uint32_t(VrHand::Right)];
    const VrControllerState& left  = input.controllers[uint32_t(VrHand::Left)];

    // Both sticks keep their use for whoever prefers them
    if (std::abs(input.actions.move.x) >= StickTakeover) {
      m_handDrives = false;
      m_stickHand  = right.gripPose.position;
    } else if (!m_handDrives && vrLength(right.gripPose.position - m_stickHand) >= HandTakesBack) {
      m_handDrives = true;
    }

    bool handNear = right.isActive && vrLength(right.gripPose.position - m_lock.position) <= PinHandReach;

    if (m_handDrives && handNear && slider && range) {
      m_pinAngle = vrLockPinAngle(m_lock, right.gripPose.position, m_pinAngle);

      float width    = game<GetFloatFn>(VrGame::TileGetFloat)(range, VrGame::TileTraitWidth);
      float position = game<GetFloatFn>(VrGame::TileGetFloat)(slider, global<uint32_t>(VrGame::LockPickSliderTrait));

      m_pad.driveLeftX = true;
      m_pad.leftX      = vrLockPinStick(position, vrLockPinSlider(m_pinAngle, width));
    }

    bool trigger = input.actions.isPressed(VrAction::MenuLeftTrigger);
    bool grip    = input.actions.isPressed(VrAction::MenuPrevious);

    float tension = left.isActive
      ? m_tension.update(trigger, grip, left.gripPose.orientation)
      : (trigger ? 1.0f : 0.0f);

    if (tension > 0.0f) {
      m_pad.driveRightX = true;
      m_pad.rightX      = vrLockTensionStick(tension);
    }

    // The gamepad's rumble of this menu is in the hands
    uint32_t state = field<uint32_t>(menu, VrGame::MinigameState);
    m_hapticWait -= dt;

    if (m_hapticWait <= 0.0f && (state == VrGame::LockStateTurning || state == VrGame::LockStateTension)) {
      m_hapticWait = HapticPeriod;

      bool bends = state == VrGame::LockStateTension;
      m_backend.applyHaptic(VrHand::Left, bends ? 0.6f : 0.15f, HapticPulse);

      if (bends)
        m_backend.applyHaptic(VrHand::Right, 0.6f, HapticPulse);
    }
  }


  void VrMinigames::updateSlotMachine(const VrInputState& input) {
    void* menu = global<void*>(VrGame::SlotMachineInstance);

    if (!menu)
      return;

    m_pad.noShoulders = true;

    VrVector3 ball = vrSceneToRoom(m_space, SlotLeverBall);

    const VrAction grips[VrHandCount] = { VrAction::MenuPrevious, VrAction::MenuNext };

    for (uint32_t i = 0; i < VrHandCount; i++) {
      const VrControllerState& controller = input.controllers[i];

      bool pulled = m_levers[i].update(controller.isActive && input.actions.isPressed(grips[i]),
        controller.gripPose.position, ball);

      if (m_levers[i].holds() && !m_leverHeld[i])
        m_backend.applyHaptic(VrHand(i), 0.3f, HapticTick);

      m_leverHeld[i] = m_levers[i].holds();

      if (pulled && field<uint32_t>(menu, VrGame::MinigameState) == VrGame::SlotMachineStateBet) {
        m_backend.applyHaptic(VrHand(i), 0.8f, HapticPulse);
        press(VrGamepadButton::A);
        Logger::info("VR: The slot machine's lever is pulled");
      }
    }
  }


  void VrMinigames::updateBlackJack(const VrInputState& input, float dt) {
    void* menu = global<void*>(VrGame::BlackJackInstance);

    if (!menu)
      return;

    VrPose fromRoom = vrInverse(m_space.anchor);
    std::array<VrTableHand, VrHandCount> hands;

    for (uint32_t i = 0; i < VrHandCount; i++) {
      const VrControllerState& controller = input.controllers[i];

      if (!controller.isActive)
        continue;

      VrVector3 scene = vrRoomToScene(m_space, controller.gripPose.position);

      hands[i].active = true;
      hands[i].over   = std::abs(scene.x) <= BlackJackHalfX && std::abs(scene.y) <= BlackJackHalfY;
      hands[i].height = scene.z / m_space.unitsPerMetre;
      hands[i].side   = vrTransformPoint(fromRoom, controller.gripPose.position).x;
    }

    VrTableSignal signal = m_signals.update(hands, dt);

    if (signal == VrTableSignal::None || field<uint32_t>(menu, VrGame::MinigameState) != VrGame::BlackJackStateTurn)
      return;

    for (uint32_t i = 0; i < VrHandCount; i++) {
      if (hands[i].active && hands[i].over)
        m_backend.applyHaptic(VrHand(i), 0.5f, HapticTick);
    }

    press(signal == VrTableSignal::Tap ? VrGamepadButton::A : VrGamepadButton::B);

    Logger::info(signal == VrTableSignal::Tap
      ? "VR: A tap on the blackjack table: hit"
      : "VR: A wave over the blackjack table: stay");
  }


  void VrMinigames::updateRoulette(const VrPointerSurfaceState& pointer) {
    void* menu = global<void*>(VrGame::RouletteInstance);

    if (!menu || field<uint32_t>(menu, VrGame::MinigameState) != VrGame::RouletteStateBet) {
      m_betTrigger = false;
      return;
    }

    if (!pointer.hit) {
      m_betTrigger = false;
      return;
    }

    VrVector3 centre = {
      0.5f * (VrGame::RouletteSelectorMinX + VrGame::RouletteSelectorMaxX),
      0.5f * (VrGame::RouletteSelectorMinY + VrGame::RouletteSelectorMaxY),
      FeltHeight };

    VrVector3 point = vrSurfacePoint(centre,
      VrGame::RouletteSelectorMaxX - VrGame::RouletteSelectorMinX + 2.0f * RouletteFeltMargin,
      VrGame::RouletteSelectorMaxY - VrGame::RouletteSelectorMinY + 2.0f * RouletteFeltMargin,
      pointer.uv);

    // The menu moves the selector, the next chip and the bet's highlight
    // only while a stick moves: it gets the laser's point, and a movement
    // too small to see, each way in turn
    field<float>(menu, VrGame::RouletteSelectorX) = clamp(point.x, VrGame::RouletteSelectorMinX, VrGame::RouletteSelectorMaxX);
    field<float>(menu, VrGame::RouletteSelectorY) = clamp(point.y, VrGame::RouletteSelectorMinY, VrGame::RouletteSelectorMaxY);

    constexpr float Nudge = 8200.0f / 32767.0f;
    m_selectorNudge = -m_selectorNudge;
    m_pad.driveLeftX = true;
    m_pad.leftX      = Nudge * float(m_selectorNudge);

    bool pressed = pointer.trigger && !m_betTrigger;
    m_betTrigger = pointer.trigger;

    if (!pressed)
      return;

    uint32_t bet = game<HoveredBetFn>(VrGame::RouletteHoveredBet)(menu);

    if (bet >= VrGame::RouletteNoBet)
      return;

    // The same trigger takes a chip back from a bet that has one
    bool placed = false;

    for (auto list = field<uint8_t*>(menu, VrGame::RouletteBetList); list && !placed; list = field<uint8_t*>(list, 4)) {
      void* item = field<void*>(list, 0);
      placed = item && field<uint32_t>(item, VrGame::RouletteBetId) == bet;
    }

    press(placed ? VrGamepadButton::Y : VrGamepadButton::A);
    m_backend.applyHaptic(pointer.hand, 0.3f, HapticTick);
  }


  void VrMinigames::placeCaravanSurface() {
    m_surface = vrSceneSurface(m_space, CaravanMatCentre, CaravanMatSizeX, CaravanMatSizeY);
  }


  void VrMinigames::updateCaravan(const VrPointerSurfaceState& pointer, const VrInputState& input, float dt) {
    auto menu = global<uint8_t*>(VrGame::CaravanInstance);

    bool playing = menu && field<uint32_t>(menu, VrGame::CaravanActiveMenu) == VrGame::CaravanGameMenu;
    bool idle    = playing && field<uint32_t>(menu, VrGame::CaravanGameState) == 0;

    // A step asked in the last frame: taken, or the menu left its idle state
    // first (a button) and would take it later, from a selection that is
    // not the one shown
    if (m_caravanStepping && menu) {
      uint32_t& key = field<uint32_t>(menu, VrGame::CaravanPendingKey);

      if (!key) {
        m_caravanStepping = false;
      } else if (!idle) {
        key = 0;
        field<int32_t>(menu, VrGame::CaravanSelectedCard)  = m_caravanWasCard;
        field<int32_t>(menu, VrGame::CaravanSelectedTrack) = m_caravanWasTrack;
        field<int32_t>(menu, VrGame::CaravanSelectedRow)   = m_caravanWasRow;
        m_caravanStepping = false;
      }
    }

    m_hasSurface = playing;
    m_hasCaravanBoard = playing && m_canHideTiles && placeCaravanBoard(menu);

    if (!playing) {
      m_caravanCard  = -1;
      m_caravanSlot  = -1;
      m_caravanClick = false;
      m_caravanTrigger = false;
      return;
    }

    placeCaravanSurface();

    if (m_caravanClick) {
      m_caravanClickAge += dt;

      if (m_caravanClickAge > CaravanClickWait)
        m_caravanClick = false;
    }

    if (!pointer.hit) {
      m_caravanCard    = -1;
      m_caravanSlot    = -1;
      m_caravanTrigger = false;
      return;
    }

    if (pointer.trigger && !m_caravanTrigger) {
      m_caravanClick    = true;
      m_caravanClickAge = 0.0f;
    }

    m_caravanTrigger = pointer.trigger;

    VrVector3 point = vrSurfacePoint(CaravanMatCentre, CaravanMatSizeX, CaravanMatSizeY, pointer.uv);
    VrVector2 at    = { point.x, point.y };

    // The hand's cards stand off the cloth and overlap: they take the laser
    // itself, not where it meets the mat
    const VrPose& aim = input.controllers[uint32_t(pointer.hand)].aimPose;
    VrVector3 origin  = vrRoomToScene(m_space, aim.position);
    VrVector3 ahead   = vrRoomToScene(m_space, aim.position + vrRotate(aim.orientation, VrVector3 { 0.0f, 0.0f, -1.0f }));

    uint32_t flags = field<uint32_t>(menu, VrGame::CaravanFlags);
    bool choosesSlot = (flags & VrGame::CaravanFlagSelectTrack) != 0;
    bool discards    = choosesSlot && (flags & VrGame::CaravanFlagDestroyTrack);
    bool setsUp      = choosesSlot && (flags & VrGame::CaravanFlagTrackSetup);

    // The first three cards go to a track the menu chooses: the click places
    // the card wherever the laser is on the mat
    bool onTarget = setsUp;
    int32_t wasCard = m_caravanCard;
    int32_t wasSlot = m_caravanSlot;
    VrCaravanKey key  = VrCaravanKey::None;
    VrCaravanStep step;

    if (!choosesSlot && caravanCardOnLaser(menu, origin, ahead - origin)) {
      int32_t current = field<int32_t>(menu, VrGame::CaravanSelectedCard);
      int32_t size    = int32_t(field<uint32_t>(menu, VrGame::CaravanPlayerHand + VrGame::SimpleArraySize));
      step     = vrCaravanHandStep(m_caravanCard, current, size);
      key      = step.key;
      onTarget = key == VrCaravanKey::None;
    } else if (discards && caravanSlotOnLaser(menu, at, true)) {
      int32_t track = m_caravanSlot / int32_t(VrGame::CaravanRowCount);
      key      = vrCaravanTrackKey(track, field<int32_t>(menu, VrGame::CaravanSelectedTrack));
      onTarget = key == VrCaravanKey::None;
    } else if (choosesSlot && !discards && !setsUp && caravanSlotOnLaser(menu, at, false)) {
      int32_t track = m_caravanSlot / int32_t(VrGame::CaravanRowCount);
      int32_t row   = m_caravanSlot % int32_t(VrGame::CaravanRowCount);
      onTarget = track == field<int32_t>(menu, VrGame::CaravanSelectedTrack)
              && row   == field<int32_t>(menu, VrGame::CaravanSelectedRow);

      if (!onTarget) {
        step = vrCaravanSlotStep(uint32_t(track), row);
        key  = step.key;
      }
    }

    if (m_caravanCard != wasCard && m_caravanCard >= 0 && !choosesSlot)
      Logger::info(str::format("VR: The laser is on the Caravan hand card ", m_caravanCard));

    if (m_caravanSlot != wasSlot && m_caravanSlot >= 0 && choosesSlot) {
      Logger::info(str::format("VR: The laser is on the Caravan track ", m_caravanSlot / int32_t(VrGame::CaravanRowCount),
        ", row ", m_caravanSlot % int32_t(VrGame::CaravanRowCount)));
    }

    // The menu takes one step a quarter of a second at most, and none while
    // a stick moves it or a step is still waiting
    bool stickMoves = input.hasActions
      && std::max(std::abs(input.actions.move.x), std::abs(input.actions.move.y)) > CaravanStickRest;
    bool canStep = idle && !stickMoves && !m_caravanStepping
      && !field<uint32_t>(menu, VrGame::CaravanPendingKey)
      && global<float>(VrGame::CaravanTimeDelta) - field<float>(menu, VrGame::CaravanSelectionDelay) > VrGame::CaravanStepDelay;

    if (key != VrCaravanKey::None && canStep) {
      m_caravanWasCard  = field<int32_t>(menu, VrGame::CaravanSelectedCard);
      m_caravanWasTrack = field<int32_t>(menu, VrGame::CaravanSelectedTrack);
      m_caravanWasRow   = field<int32_t>(menu, VrGame::CaravanSelectedRow);

      if (!choosesSlot) {
        field<int32_t>(menu, VrGame::CaravanSelectedCard) = step.selection;
      } else if (!discards) {
        field<int32_t>(menu, VrGame::CaravanSelectedTrack) = m_caravanSlot / int32_t(VrGame::CaravanRowCount);
        field<int32_t>(menu, VrGame::CaravanSelectedRow)   = step.selection;
      }

      askCaravanStep(menu, key, pointer.hand);
    }

    // A click on what is selected: the gamepad's A takes the card, places it
    // or discards the track
    if (m_caravanClick && onTarget && idle && !m_caravanStepping) {
      m_caravanClick = false;
      press(VrGamepadButton::A);

      if (!choosesSlot)
        Logger::info(str::format("VR: The trigger takes the Caravan hand card ", field<int32_t>(menu, VrGame::CaravanSelectedCard)));

      m_backend.applyHaptic(pointer.hand, 0.3f, HapticTick);
    }
  }


  bool VrMinigames::placeCaravanBoard(uint8_t* menu) {
    std::array<float, VrCaravanBoard::Tracks> xs;

    for (uint32_t track = 0; track < VrCaravanBoard::Tracks; track++) {
      auto  grid  = field<uintptr_t>(menu, VrGame::CaravanTrackGrids + track * sizeof(uint32_t));
      auto  node  = grid ? vrFindGameObject(grid, "Select1_01:0", CaravanNodeDepth) : 0;
      auto* bound = node ? field<const float*>(reinterpret_cast<void*>(node), VrGame::ObjectWorldBound) : nullptr;

      if (!bound)
        return false;

      xs[track] = bound[0];

      uintptr_t cards = VrGame::CaravanCardTracks + track * VrGame::CaravanRowCount * VrGame::CaravanCardListSize;
      bool started = field<uint32_t>(menu, cards + VrGame::SimpleArraySize) != 0;

      m_caravanBoard.values[track] = started
        ? field<int32_t>(menu, VrGame::CaravanTrackInfos + track * VrGame::CaravanTrackInfoSize + VrGame::CaravanTrackValue)
        : -1;
    }

    auto range = std::minmax_element(xs.begin(), xs.end());
    float left = 0.5f * (*range.first + *range.second) - 0.5f * CaravanBoardSide;
    float top  = CaravanNearEdge + CaravanBoardSide;

    for (uint32_t track = 0; track < VrCaravanBoard::Tracks; track++) {
      float y = track < 3 ? CaravanValueNearY : CaravanValueFarY;
      m_caravanBoard.spots[track] = {
        clamp((xs[track] - left) / CaravanBoardSide, 0.0f, 1.0f),
        clamp((top - y) / CaravanBoardSide, 0.0f, 1.0f) };
    }

    VrVector3 centre = { left + 0.5f * CaravanBoardSide, top - 0.5f * CaravanBoardSide, CaravanBoardHeight };
    m_caravanBoard.placement = vrSceneSurface(m_space, centre, CaravanBoardSide, CaravanBoardSide);
    return true;
  }


  void VrMinigames::setCaravanValuesShown(bool shown) {
    auto menu = m_kind == Kind::Caravan ? global<uint8_t*>(VrGame::CaravanInstance) : nullptr;

    // Out of the game, the game hides its values itself
    if (!menu || field<uint32_t>(menu, VrGame::CaravanActiveMenu) != VrGame::CaravanGameMenu) {
      m_caravanTilesHidden = false;
      return;
    }

    if (!shown && !m_caravanTilesHidden)
      return;

    // The game shows them again at each new game
    void* tiles = field<void*>(menu, VrGame::CaravanGameTiles);

    if (!tiles)
      return;

    for (const char* name : { VrGame::CaravanPlayerValuesTile, VrGame::CaravanOpponentValuesTile }) {
      void* tile = game<TileByNameFn>(VrGame::TileGetByName)(tiles, name);

      if (tile)
        game<TileSetFloatFn>(VrGame::TileSetFloat)(tile, VrGame::TileTraitVisible, shown ? 0.0f : 1.0f, true);
    }

    if (shown != m_caravanTilesHidden) {
      Logger::info(shown
        ? "VR: The values of Caravan's tracks lie on the mat"
        : "VR: The values of Caravan's tracks are back on the panel");
    }

    m_caravanTilesHidden = shown;
  }


  void VrMinigames::askCaravanStep(uint8_t* menu, VrCaravanKey key, VrHand hand) {
    uint32_t code = 0;

    switch (key) {
      case VrCaravanKey::Left:  code = VrGame::CaravanKeyLeft;  break;
      case VrCaravanKey::Right: code = VrGame::CaravanKeyRight; break;
      case VrCaravanKey::Up:    code = VrGame::CaravanKeyUp;    break;
      default: return;
    }

    field<uint32_t>(menu, VrGame::CaravanPendingKey) = code;
    m_caravanStepping = true;
    m_backend.applyHaptic(hand, 0.15f, HapticTick);
  }


  bool VrMinigames::caravanCardOnLaser(uint8_t* menu, const VrVector3& origin, const VrVector3& direction) {
    auto     hand  = field<uintptr_t>(menu, VrGame::CaravanPlayerHandModel);
    uint32_t count = field<uint32_t>(menu, VrGame::CaravanPlayerHand + VrGame::SimpleArraySize);

    if (!hand || !count || count > VrGame::CaravanHandSize) {
      m_caravanCard = -1;
      return false;
    }

    std::array<VrCaravanCard, VrGame::CaravanHandSize> cards;

    for (uint32_t i = 0; i < count; i++) {
      std::string name = str::format("Player-Deck_0", i + 1, ":", i);
      auto* node = reinterpret_cast<uint8_t*>(vrFindGameObject(hand, name.c_str(), CaravanNodeDepth));

      // A card without a shape, or culled, meets no laser
      if (!node || (field<uint32_t>(node, VrGame::ObjectFlags) & VrGame::ObjectFlagAppCulled)) {
        cards[i].scale = 0.0f;
        continue;
      }

      std::memcpy(cards[i].rotate, node + VrGame::ObjectWorldRotation, sizeof(cards[i].rotate));
      cards[i].translate = field<VrVector3>(node, VrGame::ObjectWorldTranslation);
      cards[i].scale     = field<float>(node, VrGame::ObjectWorldScale);
      cards[i].halfX     = CaravanCardHalfX;
      cards[i].halfY     = CaravanCardHalfY;
    }

    m_caravanCard = vrFirstCardOnRay(cards.data(), count, origin, direction);
    return m_caravanCard >= 0;
  }


  bool VrMinigames::caravanSlotOnLaser(uint8_t* menu, const VrVector2& at, bool ownTracksOnly) {
    constexpr uint32_t Count = VrGame::CaravanTrackCount * VrGame::CaravanRowCount;
    std::array<VrVector2, Count> slots;
    slots.fill(VrVector2 { FLT_MAX, FLT_MAX });

    uint32_t tracks = ownTracksOnly ? 3 : VrGame::CaravanTrackCount;

    for (uint32_t track = 0; track < tracks; track++) {
      auto grid = field<uintptr_t>(menu, VrGame::CaravanTrackGrids + track * sizeof(uint32_t));

      if (!grid)
        continue;

      for (uint32_t row = 0; row < VrGame::CaravanRowCount; row++) {
        std::string name = str::format("Select", row + 1, "_01:0");
        auto  node  = vrFindGameObject(grid, name.c_str(), CaravanNodeDepth);
        auto* bound = node ? field<const float*>(reinterpret_cast<void*>(node), VrGame::ObjectWorldBound) : nullptr;

        if (bound)
          slots[track * VrGame::CaravanRowCount + row] = { bound[0], bound[1] };

        // A row past the first empty one holds nothing to put a card on
        uintptr_t cards = VrGame::CaravanCardTracks
          + (track * VrGame::CaravanRowCount + row) * VrGame::CaravanCardListSize;

        if (!field<uint32_t>(menu, cards + VrGame::SimpleArraySize))
          break;
      }
    }

    m_caravanSlot = vrNearestPoint(slots.data(), Count, at, CaravanSlotReach, m_caravanSlot, CaravanStickiness);
    return m_caravanSlot >= 0;
  }


  void VrMinigames::drawEye(const VrEyeView& eye, float nearPlane, float farPlane) {
    if (showsRenderedMenu()) {
      drawRenderedMenu(eye);
      return;
    }

    uint32_t id  = menuId();
    void* camera = id ? global<void*>(cameraAddress(id)) : nullptr;

    if (!camera)
      return;

    uint8_t* graph  = global<uint8_t*>(VrGame::WorldSceneGraph);
    void*    culler = graph ? field<void*>(graph, VrGame::SceneGraphCuller) : nullptr;
    void*    cullerCamera = culler ? field<void*>(culler, VrGame::CullerCamera) : nullptr;

    VrGameCamera access;
    access.attach(camera);
    VrGameCameraState state = access.save();

    std::vector<uint32_t*> hidden;

    if (m_kind == Kind::Caravan)
      hideCaravanCardsOutOfFrame(state, hidden);

    float scale = m_space.unitsPerMetre / VrGameUnitsPerMetre;

    access.apply(vrSceneCameraPose(m_space, eye.pose), vrComputeGameFrustum(eye.fov));
    access.setDepthRange(nearPlane * scale, farPlane * scale);

    if (m_kind == Kind::Lock)
      VrMenuScenes::hideLockBackdrop(hidden);

    if (m_kind == Kind::SlotMachine)
      hideRaisedPayoutScreen(hidden);

    // The first-person pass moves every point light by minus the player's
    // position, this scene's too: it would have none
    auto  shadowScene = global<uint8_t*>(VrGame::ShadowSceneNodes);
    auto* offset      = shadowScene ? &field<VrVector3>(shadowScene, VrGame::ShadowSceneLightingOffset) : nullptr;
    VrVector3 gameOffset;

    if (offset) {
      gameOffset = *offset;
      *offset = { 0.0f, 0.0f, 0.0f };
    }

    g_keepWorldDepth = m_onObject;

    game<SceneFn>(sceneRender(id))();

    g_keepWorldDepth = false;

    if (offset)
      *offset = gameOffset;

    VrMenuScenes::showHidden(hidden);

    access.restore(state);

    if (culler)
      field<void*>(culler, VrGame::CullerCamera) = cullerCamera;
  }


  void VrMinigames::hideRaisedPayoutScreen(std::vector<uint32_t*>& hidden) {
    void* menu = global<void*>(VrGame::SlotMachineInstance);
    auto  root = global<uintptr_t>(VrGame::SlotMachineSceneRoot);

    if (!menu || !root)
      return;

    // Raised, it is above the frame of the game's camera, floating over the
    // machine for a head free to look up
    uint32_t state = field<uint32_t>(menu, VrGame::MinigameState);

    if (state != VrGame::SlotMachineStatePayoutIn && state != VrGame::SlotMachineStatePayoutOut)
      VrMenuScenes::hideObject(root, VrGame::SlotMachinePayoutScreen, hidden);
  }


  void VrMinigames::hideCaravanCardsOutOfFrame(const VrGameCameraState& camera, std::vector<uint32_t*>& hidden) {
    auto root = global<uintptr_t>(VrGame::CaravanSceneRoot);

    if (!root)
      return;

    // The rows of each track (one model per row) wait with their cards
    // where the game's camera never looks: under it on the player's side,
    // past the opponent's tracks on theirs. Free to look around, the eyes
    // would see them as rows of cards out of nowhere
    std::vector<uintptr_t> rows;

    vrVisitGameObjects(root, CaravanRowModelDepth, [&rows] (uintptr_t object, uint32_t) {
      auto name = field<const char*>(reinterpret_cast<void*>(object), VrGame::ObjectName);

      if (name && vrIsCaravanRowModel(name))
        rows.push_back(object);
    });

    VrGameCameraPose pose;
    pose.forward  = { camera.rotation[0], camera.rotation[3], camera.rotation[6] };
    pose.up       = { camera.rotation[1], camera.rotation[4], camera.rotation[7] };
    pose.right    = { camera.rotation[2], camera.rotation[5], camera.rotation[8] };
    pose.position = { camera.translation[0], camera.translation[1], camera.translation[2] };

    VrGameFrustum frustum;
    frustum.left   = camera.frustum[0];
    frustum.right  = camera.frustum[1];
    frustum.top    = camera.frustum[2];
    frustum.bottom = camera.frustum[3];

    size_t first = hidden.size();

    for (uintptr_t row : rows) {
      vrVisitGameObjects(row, CaravanNodeDepth, [&] (uintptr_t object, uint32_t) {
        auto* bound = field<const float*>(reinterpret_cast<void*>(object), VrGame::ObjectWorldBound);
        auto& flags = field<uint32_t>(reinterpret_cast<void*>(object), VrGame::ObjectFlags);

        if (vrGameAsNode(object) || !bound || (flags & VrGame::ObjectFlagAppCulled)
         || !vrSphereOutOfView(pose, frustum, { bound[0], bound[1], bound[2] }, bound[3]))
          return;

        flags |= VrGame::ObjectFlagAppCulled;
        hidden.push_back(&flags);
      });
    }

    size_t count = hidden.size() - first;

    if (count != m_caravanCardsOutOfFrame) {
      Logger::info(str::format("VR: ", count, " card shapes of Caravan's ", rows.size(),
        " track rows are out of the game's frame and left out of the eyes"));
      m_caravanCardsOutOfFrame = count;
    }
  }


  void VrMinigames::drawRenderedMenu(const VrEyeView& eye) {
    void* main     = global<void*>(VrGame::MainSingleton);
    void* renderer = global<void*>(VrGame::Renderer);
    uint8_t* graph = global<uint8_t*>(VrGame::WorldSceneGraph);

    if (!m_rendered || !m_texture || !main || !renderer || !graph)
      return;

    void* camera = field<void*>(m_rendered, VrGame::TerminalCamera);
    void* screen = field<void*>(m_rendered, VrGame::TerminalScreen);

    if (!camera || !screen)
      return;

    void* culler = field<void*>(graph, VrGame::SceneGraphCuller);
    void* cullerCamera = culler ? field<void*>(culler, VrGame::CullerCamera) : nullptr;

    VrGameCamera access;
    access.attach(camera);
    VrGameCameraState state = access.save();

    // As near as the console's terminal, whatever the game's near plane
    constexpr float NearPlane = 0.05f;
    constexpr float FarPlane  = 20.0f;

    access.apply(vrSceneCameraPose(m_space, eye.pose), vrComputeGameFrustum(eye.fov));
    access.setDepthRange(NearPlane * m_space.unitsPerMetre, FarPlane * m_space.unitsPerMetre);

    // The game gives its texture back after each eye: this one stays
    void*& texture = *reinterpret_cast<void**>(VrGame::InterfaceFxTexture);
    void*  gameTexture = texture;
    texture = m_texture;

    m_device->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, vrGameDepthClear(), 0);

    // On the world's terminal, its own monitor holds the screen. The game
    // adds the screen to what is behind it: the world's dark glass lights up.
    if (!m_onObject)
      game<MainFn>(VrGame::DrawRenderedMenuParent)(main);

    game<ScreenFn>(VrGame::RenderPipBoy)(main, screen, renderer);

    texture = gameTexture;
    access.restore(state);

    if (culler)
      field<void*>(culler, VrGame::CullerCamera) = cullerCamera;
  }


  void __cdecl VrMinigames::afterOffscreen() {
    if (g_minigames)
      g_minigames->renderRenderedMenuScreen();
  }


  void VrMinigames::renderRenderedMenuScreen() {
    if (!showsRenderedMenu() || !m_rendered)
      return;

    void* renderer = global<void*>(VrGame::Renderer);

    if (!renderer)
      return;

    if (!m_texture) {
      void* textures = game<TextureManagerFn>(VrGame::GetTextureManager)();
      void* borrowed = textures ? game<BorrowFn>(VrGame::BorrowRenderedTexture)(textures, renderer,
        VrGame::InterfaceFxTextureType, 0, 0, 0) : nullptr;

      if (borrowed)
        game<AssignFn>(VrGame::NiPointerAssign)(&m_texture, borrowed);
    }

    if (!m_texture)
      return;

    // The Reflectron's Draw renders the face first, into a texture of its own
    auto vtable = field<uint8_t*>(m_rendered, 0);
    (*reinterpret_cast<DrawFn*>(vtable + VrGame::TerminalDraw))(m_rendered, m_texture, 1, nullptr);
  }


  void VrMinigames::releaseTexture() {
    if (!m_texture)
      return;

    void* textures = game<TextureManagerFn>(VrGame::GetTextureManager)();

    if (textures)
      game<ReturnFn>(VrGame::ReturnRenderedTexture)(textures, m_texture);

    game<AssignFn>(VrGame::NiPointerAssign)(&m_texture, nullptr);
  }

}
