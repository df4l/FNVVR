#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_console_screen.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_nodes.h"
#include "vr_math.h"
#include "vr_menu_scenes.h"
#include "vr_minigames.h"
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

    // The root of the scene is 175 units ahead of the menu's camera and 18
    // below it, and the machine's feet are 1.2 units below its origin
    constexpr VrSceneFit SlotMachineFit = {
      { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f },
      { 0.0f, -245.0f, 26.9f }, 1.4f,
      { 0.0f, -45.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
      SlotMachineAreas, 1 };

    // The roulette wheel of the world stands above the game's table
    constexpr const char* RouletteWorldWheel = "Object02:0";

    // More entries than this means the list was not read correctly
    constexpr uint32_t  MaxProcessActors = 4096;
    // The pelvis is two levels below a skeleton's root
    constexpr uint32_t  PelvisDepth      = 6;
    // Characters logged at the opening of a menu, to see who stands where
    constexpr float     LoggedActorRange = 200.0f;

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
        default:                        return "terminal";
      }
    }

    bool hasName(const char* path, const char* lowerName) {
      std::string lower = path;

      for (char& c : lower)
        c = char(std::tolower(static_cast<unsigned char>(c)));

      return lower.find(lowerName) != std::string::npos;
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
      && VrGameMemory::matches(VrGame::RefGet3D, VrGame::RefGet3DPrologue, sizeof(VrGame::RefGet3DPrologue))
      && VrGameMemory::readable(VrGame::ProcessActors, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::ProcessHighEnd, sizeof(uint32_t))
      && VrGameMemory::readable(VrGame::LockPickInstance, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::BlackJackInstance, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::RouletteInstance, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::SlotMachineInstance, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::CaravanCamera, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::LockPickSliderTrait, sizeof(uint32_t))
      && VrGameMemory::readable(VrGame::TerminalVtable, sizeof(uintptr_t));

    if (!found)
      return nullptr;

    std::unique_ptr<VrMinigames> minigames(new VrMinigames(device, backend));
    g_minigames = minigames.get();

    // The world's terminals need the console's hook to get a screen for
    // both eyes; without it they stay on the panel
    VrConsoleScreen::setAfterOffscreen(&VrMinigames::afterOffscreen);

    // The calls stay redirected if the option is turned off and on again
    if (!g_clearHooked) {
      auto clearHook = reinterpret_cast<const void*>(&sceneClearHook);

      bool keepsDepth = VrGameMemory::redirectCall(VrGame::BlackJackDepthClearSite, VrGame::RendererClearBuffer, clearHook);
      keepsDepth = VrGameMemory::redirectCall(VrGame::SlotMachineDepthClearSite, VrGame::RendererClearBuffer, clearHook) && keepsDepth;
      keepsDepth = VrGameMemory::redirectCall(VrGame::RouletteDepthClearSite, VrGame::RendererClearBuffer, clearHook) && keepsDepth;

      if (!keepsDepth)
        Logger::warn("VR: A depth clear of the casino's scenes was not found, they are drawn over the world in front of them");

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

    void* rendered = game<PointerFn>(VrGame::GetCurrentRenderedMenu)();

    if (rendered && rendered != game<PointerFn>(VrGame::GetPipBoy)()
     && field<uintptr_t>(rendered, 0) == VrGame::TerminalVtable)
      return Kind::Terminal;

    return Kind::None;
  }


  void VrMinigames::update(
    const VrMinigameFrame&        frame,
    const VrInputState&           input,
    const VrPointerSurfaceState&  surface) {
    Kind kind = frame.stereo ? findKind() : Kind::None;

    // The game may forget it as soon as a menu is open
    if (!frame.stereo)
      m_activateRef = game<PointerFn>(VrGame::GetActivateRef)();

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
    bool onTop = m_kind == Kind::Terminal || isTopMenu(menuId());

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

      case Kind::Caravan: {
        // Its camera pans over the table between the menus, and the table
        // follows, so that what the game shows stays in front of the player
        void* camera = global<void*>(VrGame::CaravanCamera);

        if (camera) {
          VrGameCamera access;
          access.attach(camera);
          m_space.origin = access.readPose().position;
        }
      } break;

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
    m_leverHeld   = { };
    m_levers      = { };
    m_signals     = VrTableSignals();
    m_tension     = VrLockTension();

    if (kind == Kind::Terminal)
      return placeTerminal(frame);

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
        m_space = vrSceneSpaceAroundHead(frame.head, pose.position, pose.forward, VrGameUnitsPerMetre);
        placeBoard(frame, true);
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

    auto vtable = field<uint8_t*>(reference, 0);
    auto root   = fit ? static_cast<uint8_t*>((*reinterpret_cast<Get3DFn*>(vtable + VrGame::RefGet3DSlot))(reference)) : nullptr;

    if (!root) {
      Logger::info(str::format("VR: No place for the scene on ", model, ", it stands in the room"));
      return false;
    }

    auto rotation = reinterpret_cast<const float*>(root + VrGame::ObjectWorldRotation);

    m_object.x        = { rotation[0], rotation[3], rotation[6] };
    m_object.y        = { rotation[1], rotation[4], rotation[7] };
    m_object.z        = { rotation[2], rotation[5], rotation[8] };
    m_object.position = field<VrVector3>(root, VrGame::ObjectWorldTranslation);
    m_object.scale    = field<float>(root, VrGame::ObjectWorldScale);

    m_fit        = fit;
    m_seatCamera = vrSeatCamera(m_object, *fit,
      vrComputeEyeInReference(frame.reference, frame.head), frame.cameraHeight, VrGameUnitsPerMetre);
    m_onObject   = true;

    keep(root);

    // The machine is whole in the scene. The tables' are only tops, and the
    // world's holds them up: only what stands above that top goes.
    if (fit == &SlotMachineFit)
      hideRoot(root);
    else if (fit == &RouletteFit)
      VrMenuScenes::hideObject(reinterpret_cast<uintptr_t>(root), RouletteWorldWheel, m_objectHidden);

    hideActorsInTheWay();
    standOnObject(frame);
    return true;
  }


  void VrMinigames::standOnObject(const VrMinigameFrame& frame) {
    m_space = vrSceneSpaceOnObject(frame.reference, m_seatCamera, m_object, *m_fit, VrGameUnitsPerMetre);

    hidePlayerBody();

    if (m_fit == &RouletteFit)
      placeRouletteSurface();
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


  void VrMinigames::hideActorsInTheWay() {
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

      if (inTheWay) {
        keep(root);
        hideRoot(root);
      }

      if (std::abs(body.x) < LoggedActorRange && std::abs(body.y) < LoggedActorRange) {
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
    VrMenuScenes::showHidden(m_objectHidden);
    m_objectHidden.clear();

    // The game may have gone back to first person since, and wants it hidden
    auto player = global<uint8_t*>(VrGame::Player);

    if (m_bodyHidden && player && field<uint8_t>(player, VrGame::PlayerCurrentThirdPerson))
      *m_bodyHidden &= ~VrGame::ObjectFlagAppCulled;

    m_bodyHidden = nullptr;

    for (void*& object : m_kept)
      game<AssignFn>(VrGame::NiPointerAssign)(&object, nullptr);

    m_kept.clear();

    m_onObject = false;
    m_fit      = nullptr;
  }


  void VrMinigames::close() {
    // A try of open() that has to wait for the menu's scene may have started
    leaveObject();

    if (m_kind == Kind::None)
      return;

    Logger::info(str::format("VR: The ", kindName(menuId()), " is put away"));

    m_kind       = Kind::None;
    m_terminal   = nullptr;
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


  bool VrMinigames::placeTerminal(const VrMinigameFrame& frame) {
    auto rendered = static_cast<uint8_t*>(game<PointerFn>(VrGame::GetCurrentRenderedMenu)());

    if (!rendered || !VrConsoleScreen::hooksOffscreen())
      return false;

    void* camera = field<void*>(rendered, VrGame::TerminalCamera);
    void* model  = field<void*>(rendered, VrGame::TerminalModel);

    if (!field<bool>(rendered, VrGame::TerminalInitialized) || !camera || !model
     || !field<void*>(rendered, VrGame::TerminalScreen))
      return false;

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

    m_space = vrSceneSpaceInFront(frame.head, target, TerminalDistance, 0.0f, view / TerminalWidth);
    m_panel = vrSceneViewRectangle(m_space, target.x, frustum);
    m_terminal = rendered;
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


  void VrMinigames::drawEye(const VrEyeView& eye, float nearPlane, float farPlane) {
    if (m_kind == Kind::Terminal) {
      drawTerminal(eye);
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

    float scale = m_space.unitsPerMetre / VrGameUnitsPerMetre;

    access.apply(vrSceneCameraPose(m_space, eye.pose), vrComputeGameFrustum(eye.fov));
    access.setDepthRange(nearPlane * scale, farPlane * scale);

    std::vector<uint32_t*> hidden;

    if (m_kind == Kind::Lock)
      VrMenuScenes::hideLockBackdrop(hidden);

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


  void VrMinigames::drawTerminal(const VrEyeView& eye) {
    void* main     = global<void*>(VrGame::MainSingleton);
    void* renderer = global<void*>(VrGame::Renderer);
    uint8_t* graph = global<uint8_t*>(VrGame::WorldSceneGraph);

    if (!m_terminal || !m_texture || !main || !renderer || !graph)
      return;

    void* camera = field<void*>(m_terminal, VrGame::TerminalCamera);
    void* screen = field<void*>(m_terminal, VrGame::TerminalScreen);

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

    m_device->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    game<MainFn>(VrGame::DrawRenderedMenuParent)(main);
    game<ScreenFn>(VrGame::RenderPipBoy)(main, screen, renderer);

    texture = gameTexture;
    access.restore(state);

    if (culler)
      field<void*>(culler, VrGame::CullerCamera) = cullerCamera;
  }


  void __cdecl VrMinigames::afterOffscreen() {
    if (g_minigames)
      g_minigames->renderTerminalScreen();
  }


  void VrMinigames::renderTerminalScreen() {
    if (m_kind != Kind::Terminal || !m_terminal)
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

    auto vtable = field<uint8_t*>(m_terminal, 0);
    (*reinterpret_cast<DrawFn*>(vtable + VrGame::TerminalDraw))(m_terminal, m_texture, 1, nullptr);
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
