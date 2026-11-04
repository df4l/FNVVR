#pragma once

#include <cstdint>
#include <memory>

namespace dxvk {

  /**
   * \brief What the equipped weapon's scope is, as the game defines it
   */
  struct VrEquippedScope {
    /// Path of the weapon's model, relative to Data\Meshes
    const char* weaponModel = nullptr;
    /// Path of the scope's model, relative to Data\Meshes
    const char* scopeModel  = nullptr;
    /// The weapon's field of view while aiming, its scope mod counted, in degrees
    float       sightFov    = 0.0f;
    /// The scope also shows in the dark
    bool        nightVision = false;
  };

  /**
   * \brief Replaces the game's flat scope with a headset
   *
   * While it aims with a scope the game hides the first-person model, draws a
   * full-screen overlay and blurs the screen, which only make sense on a flat
   * screen: the scope is a lens on the weapon in VR. These are turned off
   * here, whatever else the aim state does (findings/optics.md §10).
   *
   * The aim state itself makes a weapon more precise. It also plays other
   * animations and slows the player, so holding a weapon with both hands
   * only brings its precision. Pistols that skip the first-person aim
   * animations get them while held with both hands: those put the left hand on.
   */
  class VrScopeGame {

  public:

    /**
     * \brief Redirects the game's calls
     *
     * \returns \c nullptr if a call was not found, the game's scope then stays
     */
    static std::unique_ptr<VrScopeGame> install();

    ~VrScopeGame();

    /**
     * \brief Sets whether the player holds the weapon with both hands
     */
    void setSteadied(bool steadied) {
      m_steadied = steadied;
    }

    /**
     * \brief Reads the equipped weapon's scope
     *
     * \param [out] scope The scope
     * \returns \c false without a weapon, if it has no scope, or if its scope
     *    is a mod that is not fitted
     */
    static bool readEquipped(VrEquippedScope& scope);

  private:

    VrScopeGame() = default;

    bool m_steadied = false;
    bool m_loggedSteadied = false;

    static void __cdecl setGunScopeVisibleHook(uint32_t visible);

    static void* __fastcall scopeModelChildHook(void* node, void* unused, uint32_t index);

    static void __fastcall ironSightsCullHook(void* object, void* unused, uint32_t culled);

    static void __fastcall imageSpaceIronSightsHook(void* player, void* unused);

    static uint32_t __fastcall nightScopeHook(const void* weapon, void* unused);

    static uint32_t __fastcall gunSkillIronSightsHook(void* actor, void* unused);

    static void __fastcall scopeWobbleHook(void* player, void* unused, float angle);

    static uint32_t __fastcall useAimAnimsHook(const void* weapon, void* unused);

    static uint32_t __fastcall aimModScopeHook(const void* weapon, void* unused);

  };

}
