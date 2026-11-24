#pragma once

#include <cstdint>
#include <memory>

namespace dxvk {

  /**
   * \brief Hides the player's weapon from the game while a bare hand strikes
   *
   * The game's hits take the attacker's weapon from its process
   * (GetCurrentWeapon, findings/melee.md §15): Actor::CombatHit for the
   * damage, the skill, the sounds and the reaction, Actor::ObjectHit for
   * the push and the impact. A fist that holds no weapon (the left one
   * whatever the right hand holds, the right one with the weapon
   * holstered) strikes as the game's unarmed attack: while it strikes,
   * the player's process gives no weapon. The HighProcess entry of that
   * virtual function is replaced; it answers as the game's outside a
   * strike, on the other threads and for the other actors.
   *
   * The blood and impact of a hit may run later, from the game's task queue,
   * and read the current weapon again: they are given the same answer when
   * the hit itself was dealt without a weapon.
   */
  class VrBareHand {

  public:

    ~VrBareHand();

    /**
     * \brief Replaces the process's weapon function
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrBareHand> install();

    /**
     * \brief Whether the player's weapon can be hidden
     *
     * \param [in] player The PlayerCharacter
     */
    bool covers(const uint8_t* player) const;

    /**
     * \brief Hides the player's weapon while it lives
     *
     * Made around one hit, on the thread that deals it.
     */
    class Strike {

    public:

      /**
       * \param [in] bareHand Hides the weapon, or \c nullptr to leave it
       * \param [in] player The PlayerCharacter
       */
      Strike(VrBareHand* bareHand, const uint8_t* player);

      ~Strike();

      Strike             (const Strike&) = delete;
      Strike& operator = (const Strike&) = delete;

    private:

      // What was hidden when this one started: it may be made inside another
      uintptr_t m_previous = 0;
      bool      m_hides    = false;

    };

  private:

    VrBareHand() = default;

    static void* __fastcall currentWeaponHook(
            uintptr_t             process,
            void*                 unused);

    static void __fastcall queuedBloodHook(
            void*                 target,
            void*                 unused,
            float                 damage,
            const uint8_t*        hit);

    static void __fastcall queuedBloodOnWeaponHook(
            void*                 attacker,
            void*                 unused,
            float                 damage,
            const uint8_t*        hit,
            void*                 target);

  };

}
