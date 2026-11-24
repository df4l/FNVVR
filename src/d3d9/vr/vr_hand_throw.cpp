#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_sound.h"
#include "vr_hand_throw.h"
#include "vr_math.h"
#include "vr_melee.h"
#include "vr_weapon_aim.h"

namespace dxvk {

  namespace {

    // TESObjectWEAP::Fire, __thiscall with the actor
    using WeaponFireFn = void (__fastcall*)(uint8_t* weapon, void* unused, uint8_t* actor);

    // Actor::HandlePostAnimationActions, __thiscall without arguments
    using HandlePostAnimationFn = void (__fastcall*)(uint8_t* actor, void* unused);

    // TESForm::GetFormByNumericID
    using FormByIdFn = uint8_t* (__cdecl*)(uint32_t id);

    VrHandThrow* g_handThrow = nullptr;

    // How long the hand stays empty after a throw before the next weapon
    // is shown in it, about the game's throw animation from its release
    constexpr float RefillTime = 0.5f;

    // A throw the game's update of the player has not fired by then, while
    // a menu stops it, is dropped
    constexpr float PendingTimeout = 0.5f;

    // Where the left index fingertip is in its controller's frame, as the
    // interaction prompt takes it (VrHandHudLayout::FingertipOffset)
    constexpr VrVector3 Fingertip = { 0.08f * VrGameUnitsPerMetre, 0.0f, 0.0f };

    // When a projectile thrown by hand is looked at again for the log
    constexpr float WatchTimes[] = { 0.1f, 0.5f, 2.0f };

    constexpr uint32_t MaxLoggedThrows = 50;
    constexpr uint32_t MaxLoggedMisses = 20;
    constexpr uint32_t MaxLoggedArms   = 50;
    constexpr uint32_t MaxLoggedReaches = 20;

    constexpr float CentimetresPerUnit = 100.0f / VrGameUnitsPerMetre;
    constexpr float DegreesPerRadian   = 57.2957795f;

    uint8_t* player() {
      return *reinterpret_cast<uint8_t* const*>(VrGame::Player);
    }

    template<typename T>
    T& field(uint8_t* base, uintptr_t offset) {
      return *reinterpret_cast<T*>(base + offset);
    }

    template<typename T>
    T readField(const uint8_t* base, uintptr_t offset) {
      T value;
      std::memcpy(&value, base + offset, sizeof(value));
      return value;
    }

    VrVector3 readVector(const uint8_t* object, uintptr_t offset) {
      VrVector3 v;
      std::memcpy(&v, object + offset, sizeof(v));
      return v;
    }

    std::string hexId(uint32_t id) {
      char text[16];
      std::snprintf(text, sizeof(text), "%08X", id);
      return text;
    }

    const char* weaponName(const uint8_t* form) {
      const char* name = readField<const char*>(form, VrGame::WeaponFullName);
      return name ? name : "";
    }

    /**
     * \brief A position in the game world, in metres in the body's frame
     */
    VrVector3 inBody(const VrGameCameraPose& body, const VrVector3& world) {
      return vrPointToHand(body, world) * (1.0f / VrGameUnitsPerMetre);
    }

    /**
     * \brief A point of the right hand's frame, in metres in the game world
     */
    VrVector3 inMetres(const VrGameCameraPose& hand, const VrVector3& local) {
      return vrPointFromHand(hand, local) * (1.0f / VrGameUnitsPerMetre);
    }

    /**
     * \brief Level direction of a pose's forward axis
     *
     * \returns The unit direction, or the pose's forward if it points
     *    straight up or down
     */
    VrVector3 levelForward(const VrGameCameraPose& pose) {
      VrVector3 level = { pose.forward.x, pose.forward.y, 0.0f };
      float length = vrLength(level);
      return length > 1e-3f ? level * (1.0f / length) : pose.forward;
    }

  }


  VrHandThrow::~VrHandThrow() {
    // The patched call stays in place and falls through to the game's function
    g_handThrow = nullptr;
  }


  std::unique_ptr<VrHandThrow> VrHandThrow::install(VrWeaponHand& weaponHand) {
    if (g_handThrow)
      return nullptr;

    if (!VrGameMemory::matches(VrGame::WeaponFire, VrGame::WeaponFirePrologue, sizeof(VrGame::WeaponFirePrologue))
     || !VrGameMemory::matchesUnlessRewritten(VrGame::FormById, VrGame::FormByIdPrologue, sizeof(VrGame::FormByIdPrologue)))
      return nullptr;

    // Without it, throws are fired while the frame is drawn, as the AI
    // threads step Havok
    bool firesInUpdate = VrGameMemory::redirectCall(VrGame::PlayerPostAnimationCallSite,
      VrGame::HandlePostAnimationActions, reinterpret_cast<const void*>(&VrHandThrow::postAnimationHook));

    Logger::info("VR: Grenades, mines and thrown weapons are armed with the left hand and thrown with the right");

    if (!firesInUpdate)
      Logger::warn("VR: The player's update could not be patched, throws by hand are fired while the frame is drawn");

    std::unique_ptr<VrHandThrow> handThrow(new VrHandThrow(weaponHand, firesInUpdate));
    g_handThrow = handThrow.get();
    return handThrow;
  }


  VrHandThrowEvents VrHandThrow::update(const VrHandThrowInput& input) {
    VrHandThrowEvents events;

    const VrHeldThrown* held = m_weaponHand.heldThrown();
    bool vats = *reinterpret_cast<const uint32_t*>(VrGame::VatsCameraState) != 0;

    m_throwsWithHand = held && input.hand && !vats;

    if (m_hasPending) {
      m_pending.age += input.dt;

      if (m_pending.age > PendingTimeout || vats) {
        Logger::info(str::format("VR: The throw of '", weaponName(m_pending.form),
          "' was not fired by the game's update of the player, it is dropped"));
        m_hasPending = false;
      }
    }

    // The next one comes into the hand, unarmed, even if the Pip-Boy was
    // opened meanwhile
    if (m_refill > 0.0f) {
      m_refill -= input.dt;

      if (m_refill <= 0.0f) {
        m_weaponHand.showThrownAgain();
        m_arm.reset(m_arm.arming());
      }
    }

    if (!m_throwsWithHand) {
      m_holding = false;
      m_lit     = false;
      m_tracker.reset();
      stopSounds();

      // V.A.T.S. throws with the game's animation, which brings the next
      // one unarmed. Elsewhere (Pip-Boy, menus) the weapon stays as it was.
      if (vats) {
        m_weaponHand.setThrowArmState(VrThrowArmState());
        m_form   = nullptr;
        m_refill = 0.0f;
      }

      return events;
    }

    // The model's parts are known once it is loaded, which may be after
    // the weapon came into the hand
    const char* model = readField<const char*>(held->form, VrGame::WeaponModelPath);
    VrThrowArming arming = vrThrowArming(held->type, model, held->parts);

    if (held->form != m_form || (arming != m_arm.arming() && held->loaded && !m_arm.busy() && !m_holding))
      startWeapon(*held, arming);

    bool ready = held->shown && m_refill <= 0.0f && !m_hasPending;

    if (ready && input.otherHand) {
      VrThrowArmInput arm = armInput(input, *held);
      events.arm = m_arm.update(arm, m_config);
      logArm(events.arm);
      logPinReach(arm, events.arm);
    }

    m_gripHeld    = input.grip;
    m_triggerHeld = input.trigger;

    bool lit = held->parts.lighter && input.otherHand && input.trigger;

    // The animation's sounds: the lighter struck, the hand on the pin, then
    // the fuse catching, the pin out, the button pressed
    if (const VrThrowAnimation* animation = m_weaponHand.throwAnimation()) {
      if ((lit && !m_lit) || events.arm == VrArmEvent::PinTaken)
        queueSounds(animation->prepare);

      if (events.arm == VrArmEvent::Armed)
        queueSounds(animation->arm.empty() && m_arm.arming() == VrThrowArming::Button ? animation->prepare : animation->arm);
    }

    m_lit = lit;
    playSounds(input);

    VrThrowArmState state;
    state.pinHeld = m_arm.holdsPin();
    state.pinOut  = m_arm.arming() == VrThrowArming::Pin && m_arm.armed();
    state.lit     = m_lit;
    state.armed   = m_arm.armed();
    m_weaponHand.setThrowArmState(state);

    // A grenade's motion counts from the press of the trigger; a mine
    // leaves at the press, moving as the hand did just before
    bool placed  = vrThrowIsPlaced(held->type);
    bool pressed = input.attack && !m_holding;
    bool acts    = placed ? pressed : (m_holding && !input.attack);

    if (pressed && !placed)
      m_tracker.reset();

    // The throw follows the controller's own motion: the model's centre can
    // sit a hand's width from it, as a spear held for its throw does, and
    // the wrist's flick at the release would turn its motion up or down
    VrVector3 centre = vrPointFromHand(*input.hand, held->centre);
    m_tracker.add(inBody(input.body, input.hand->position), input.dt);
    m_holding = input.attack;

    if (!acts || !ready)
      return events;

    if (!m_arm.armed()) {
      if (m_loggedUnarmed < MaxLoggedMisses) {
        Logger::info(str::format("VR: '", weaponName(held->form), "' is not armed (",
          vrThrowArmingName(m_arm.arming()), "), it stays in the hand"));
        m_loggedUnarmed++;
      }
      return events;
    }

    // A mine leaves at the press, moving as the hand does then; a throw
    // takes the hand's fastest motion just before the trigger let go
    VrVector3 released  = m_tracker.velocity(m_config.velocityWindow);
    VrVector3 velocity  = placed ? released
      : m_tracker.peakVelocity(m_config.velocityWindow, m_config.peakLookback);
    float     handSpeed = vrLength(velocity);

    PendingThrow pending;
    pending.form      = held->form;
    pending.handSpeed = handSpeed;
    pending.releasedSpeed = vrLength(released);
    pending.releasedPitch = vrLength(released) > 0.0f
      ? -vrComputeAimAngles(vrPointFromHand(input.body, released) - input.body.position).pitch
      : 0.0f;
    pending.player    = readVector(player(), VrGame::RefLocation);
    pending.centre    = held->centre;
    pending.release.origin = centre;

    if (placed) {
      // As fast as the hand moves, along its motion, or straight ahead of
      // the hand when it is still
      pending.release.share    = 1.0f;
      pending.release.maxSpeed = handSpeed * VrGameUnitsPerMetre;
      pending.release.direction = handSpeed >= m_config.placeDirectionSpeed
        ? vrPointFromHand(input.body, velocity * (1.0f / handSpeed)) - input.body.position
        : levelForward(*input.hand);
    } else {
      pending.release.share    = vrComputeThrowShare(handSpeed, m_config);
      pending.release.maxSpeed = std::numeric_limits<float>::max();

      if (!(pending.release.share > 0.0f)) {
        if (m_loggedSlow < MaxLoggedMisses) {
          Logger::info(str::format("VR: The hand let go at ", handSpeed, " m/s, slower than ", m_config.minSpeed,
            " m/s: '", weaponName(held->form), "' stays in the hand"));
          m_loggedSlow++;
        }
        return events;
      }

      // From the body's frame back to the world
      pending.release.direction = vrPointFromHand(input.body, velocity * (1.0f / handSpeed)) - input.body.position;
    }

    if (m_firesInUpdate) {
      m_pending    = pending;
      m_hasPending = true;
    } else {
      fire(pending);
    }

    // A fuse burning in the hand stops with the throw: the projectile has
    // its own sounds
    stopSounds();

    // The pin stays out, and in the left hand while the grip holds it,
    // until the next one comes
    m_holding = false;
    m_refill  = RefillTime;
    m_tracker.reset();

    events.thrown = true;
    return events;
  }


  void VrHandThrow::startWeapon(const VrHeldThrown& held, VrThrowArming arming) {
    const char* model = readField<const char*>(held.form, VrGame::WeaponModelPath);

    stopSounds();

    m_form    = held.form;
    m_holding = false;
    m_refill  = 0.0f;
    m_arm.reset(arming);
    m_tracker.reset();

    Logger::info(str::format("VR: '", weaponName(held.form), "' (", model ? model : "", ", type ", held.type,
      ") is armed by hand: ", vrThrowArmingName(arming),
      vrThrowIsPlaced(held.type) ? ", placed at a press of the trigger" : ", thrown by letting go of the trigger",
      held.loaded ? "" : " (model not loaded yet)"));
  }


  VrThrowArmInput VrHandThrow::armInput(
    const VrHandThrowInput&     input,
    const VrHeldThrown&         held) const {
    const VrGameCameraPose& hand  = *input.hand;
    const VrGameCameraPose& other = *input.otherHand;

    VrThrowArmInput arm;
    arm.hand      = other.position * (1.0f / VrGameUnitsPerMetre);
    arm.fingertip = inMetres(other, Fingertip);
    arm.flame     = held.hasFlame ? inMetres(other, held.flame) : arm.hand;
    arm.grip      = input.grip;
    arm.trigger   = input.trigger;
    arm.dt        = input.dt;

    // The part acted on, or the whole model when it has none
    VrVector3 target = held.centre;
    float     radius = held.radius;

    switch (m_arm.arming()) {
      case VrThrowArming::Pin:
        if (held.parts.pin) {
          target = held.pin;
          radius = held.pinRadius;
        }
        break;

      case VrThrowArming::Fuse:
        if (held.hasFuse) {
          target = held.fuse;
          radius = held.fuseRadius;
        }
        break;

      case VrThrowArming::Button:
        if (held.hasButton) {
          target = held.button;
          radius = held.buttonRadius;
        }
        break;

      case VrThrowArming::None:
        break;
    }

    arm.target = inMetres(hand, target);
    arm.radius = radius / VrGameUnitsPerMetre;
    return arm;
  }


  void VrHandThrow::fire(const PendingThrow& pending) {
    uint8_t* actor  = player();
    auto     weapon = const_cast<uint8_t*>(pending.form);

    // Thrown weapons get the power of a throw held to the end, as the game
    // gives V.A.T.S. throws: the hand's speed decides instead
    float& releaseTimer = field<float>(actor, VrGame::PlayerProjectileReleaseTimer);
    float  timer = releaseTimer;
    releaseTimer = VrGame::ThrowReleaseTimerFull;

    m_weaponHand.setThrowRelease(&pending.release);
    reinterpret_cast<WeaponFireFn>(VrGame::WeaponFire)(weapon, nullptr, actor);
    m_weaponHand.setThrowRelease(nullptr);

    releaseTimer = timer;

    if (m_loggedThrows >= MaxLoggedThrows)
      return;

    m_loggedThrows++;

    const VrThrowSpeeds& speeds = m_weaponHand.throwSpeeds();
    VrHeadAngles angles = vrComputeAimAngles(pending.release.direction);
    uint8_t* projectile = m_weaponHand.thrownProjectile();

    Logger::info(str::format("VR: Threw '", weaponName(pending.form), "', hand at ", pending.handSpeed, " m/s, ",
      -angles.pitch * DegreesPerRadian, " degrees up (when the trigger let go ", pending.releasedSpeed, " m/s, ",
      pending.releasedPitch * DegreesPerRadian, " degrees up): ", speeds.given / VrGameUnitsPerMetre,
      " m/s (game's full throw ", speeds.full / VrGameUnitsPerMetre, " m/s), from its centre ",
      pending.centre.x * CentimetresPerUnit, " cm ahead, ", pending.centre.y * CentimetresPerUnit, " cm above, ",
      pending.centre.z * CentimetresPerUnit, " cm right of the controller",
      m_firesInUpdate ? ", in the player's update" : ", while the frame is drawn",
      projectile ? "" : ", no projectile was launched"));

    if (projectile) {
      m_watched.id     = readField<uint32_t>(projectile, VrGame::FormId);
      m_watched.origin = pending.release.origin;
      m_watched.start  = std::chrono::steady_clock::now();
      m_watched.logged = 0;
    }
  }


  void VrHandThrow::firePending() {
    if (!m_hasPending)
      return;

    m_hasPending = false;

    // The weapon may have changed while the throw waited
    const VrHeldThrown* held = m_weaponHand.heldThrown();

    if (!held || held->form != m_pending.form)
      return;

    // The player has moved since: the hand moved with it
    PendingThrow pending = m_pending;
    pending.release.origin = pending.release.origin
      + (readVector(player(), VrGame::RefLocation) - pending.player);

    fire(pending);
  }


  void VrHandThrow::watchThrown() {
    constexpr uint32_t WatchCount = sizeof(WatchTimes) / sizeof(WatchTimes[0]);

    if (!m_watched.id)
      return;

    float age = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_watched.start).count();

    if (age < WatchTimes[m_watched.logged])
      return;

    // Looked up by its id: the game may have deleted it
    uint8_t* projectile = reinterpret_cast<FormByIdFn>(VrGame::FormById)(m_watched.id);

    if (!projectile) {
      Logger::info(str::format("VR: The projectile thrown by hand is gone after ", WatchTimes[m_watched.logged], " s"));
      m_watched.id = 0;
      return;
    }

    VrVector3 position = readVector(projectile, VrGame::RefLocation);
    bool impacted = readField<uint8_t>(projectile, VrGame::ProjectileHasImpacted) != 0;

    // The first impact's reference
    auto impact = readField<const uint8_t*>(projectile, VrGame::ProjectileImpacts);
    auto target = impact ? readField<const uint8_t*>(impact, 0) : nullptr;
    uint32_t targetId = target ? readField<uint32_t>(target, VrGame::FormId) : 0;

    Logger::info(str::format("VR: The projectile thrown by hand after ", WatchTimes[m_watched.logged], " s: ",
      vrLength(position - m_watched.origin) * CentimetresPerUnit, " cm from where it left, ",
      (position.z - m_watched.origin.z) * CentimetresPerUnit, " cm higher, impacted: ", impacted ? "yes" : "no",
      impact ? str::format(", first impact on reference ", hexId(targetId),
        targetId == VrGame::PlayerRefId ? " (the player)" : "") : std::string()));

    if (++m_watched.logged == WatchCount)
      m_watched.id = 0;
  }


  void VrHandThrow::logArm(VrArmEvent event) {
    if (event == VrArmEvent::None || m_loggedArms >= MaxLoggedArms)
      return;

    const char* what = "";

    switch (event) {
      case VrArmEvent::PinTaken:   what = "The left hand took the pin"; break;
      case VrArmEvent::PinDropped: what = "The left hand let go of the pin"; break;
      case VrArmEvent::Lighting:   what = "The lighter's flame is at the fuse"; break;
      case VrArmEvent::Armed:      what = "Armed"; break;
      case VrArmEvent::None:       break;
    }

    Logger::info(str::format("VR: ", what, " (", vrThrowArmingName(m_arm.arming()), ")"));
    m_loggedArms++;
  }


  void VrHandThrow::logPinReach(const VrThrowArmInput& arm, VrArmEvent event) {
    bool pressed = (arm.grip && !m_gripHeld) || (arm.trigger && !m_triggerHeld);

    if (!pressed || event != VrArmEvent::None || m_arm.arming() != VrThrowArming::Pin
     || m_arm.armed() || m_arm.holdsPin() || m_loggedReaches >= MaxLoggedReaches)
      return;

    Logger::info(str::format("VR: The left hand closed ", vrLength(arm.hand - arm.target) * 100.0f,
      " cm from the pin's centre, it takes it within ", (arm.radius + m_config.pinReach) * 100.0f, " cm"));
    m_loggedReaches++;
  }


  void VrHandThrow::queueSounds(const std::vector<VrThrowSound>& sounds) {
    for (const VrThrowSound& sound : sounds)
      m_sounds.push_back({ sound.delay, sound });
  }


  void VrHandThrow::playSounds(const VrHandThrowInput& input) {
    // From the left hand, which acts on the weapon
    const VrGameCameraPose* hand = input.otherHand ? input.otherHand : input.hand;

    for (size_t i = 0; i < m_sounds.size(); ) {
      QueuedSound& queued = m_sounds[i];
      queued.delay -= input.dt;

      if (queued.delay > 0.0f || !hand) {
        i++;
        continue;
      }

      int32_t id = vrPlayFirstPersonSound(queued.sound.sound, hand->position, 1.0f);

      if (queued.sound.loops && id != -1)
        m_loopingSounds.push_back(id);

      m_sounds.erase(m_sounds.begin() + i);
    }
  }


  void VrHandThrow::stopSounds() {
    for (int32_t id : m_loopingSounds)
      vrStopGameSound(id);

    m_loopingSounds.clear();
    m_sounds.clear();
  }


  void __fastcall VrHandThrow::postAnimationHook(
          uint8_t*              actor,
          void*                 unused) {
    // The game fires the player's own throws in this call
    if (g_handThrow && actor == player()) {
      g_handThrow->firePending();
      g_handThrow->watchThrown();
    }

    reinterpret_cast<HandlePostAnimationFn>(VrGame::HandlePostAnimationActions)(actor, nullptr);
  }

}
