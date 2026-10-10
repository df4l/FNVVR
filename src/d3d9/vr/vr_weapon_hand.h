#pragma once

#include <cstdint>
#include <memory>

#include "vr_weapon_aim.h"

namespace dxvk {

  /**
   * \brief Puts the first-person weapon in the hand and shoots where it points
   *
   * Two calls of the game are redirected, see findings/weapon.md:
   *
   * - Main::PlaceCamera draws the first-person model (arms and weapon)
   *   around the origin and updates it right before it is culled. After
   *   that update the whole model is moved so that its weapon node lands
   *   on the hand, turned from the game's camera to the hand, and updated
   *   again. The first-person camera is moved to the eye, so that the model
   *   is seen with the right parallax in each eye.
   * - TESObjectWEAP::Fire creates the player's projectiles from the muzzle
   *   the game animated in front of its camera. They start from the muzzle
   *   drawn in the hand instead, along the direction the hand points, with
   *   the game's spread kept.
   *
   * Only used on the game's main thread.
   */
  class VrWeaponHand {

  public:

    ~VrWeaponHand();

    /**
     * \brief Redirects the game's calls
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrWeaponHand> install();

    /**
     * \brief Sets the poses for the frame about to be drawn
     *
     * \param [in] gameCamera Camera the game placed the first-person model
     *    for, before the VR layer replaced it
     * \param [in] hand Pose of the hand holding the weapon in the game world,
     *    or \c nullptr if the weapon stays where the game puts it
     */
    void beginFrame(
      const VrGameCameraPose&     gameCamera,
      const VrGameCameraPose*     hand);

    /**
     * \brief Called before the game places the first-person camera for an eye
     *
     * \param [in] main The game's Main object
     * \param [in] eyePosition Position of the eye being drawn, in the game world
     */
    void beforePlaceCamera(
            void*                 main,
      const VrVector3&            eyePosition);

  private:

    VrWeaponHand() = default;

    VrGameCameraPose m_gameCamera;
    VrGameCameraPose m_hand;
    bool             m_hasHand = false;

    // Set before PlaceCamera, used by the update it makes
    uint8_t*         m_main = nullptr;
    VrVector3        m_rootPosition;
    VrVector3        m_eyePosition;

    // Muzzle relative to the first-person root, and the direction the
    // hand pointed, when the weapon was last drawn in the hand
    VrVector3        m_muzzleOffset;
    VrVector3        m_aimDirection;
    bool             m_hasAim = false;

    bool             m_loggedActive = false;
    bool             m_loggedParent = false;

    static void __fastcall updateHook(
            uint8_t*              object,
            void*                 unused,
            void*                 updateData);

    static void* __cdecl createProjectileHook(
            void* projectile, void* shooter, void* target, void* weapon,
            float x, float y, float z, float heading, float pitch,
            uint32_t a9, uint32_t a10, uint32_t a11, uint32_t a12,
            float spreadHeading, float spreadPitch, uint32_t a15);

    void placeModel(uint8_t* root, void* updateData);

    bool holdsWeapon(const uint8_t* player) const;

  };

}
