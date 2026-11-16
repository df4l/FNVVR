#pragma once

#include <cstdint>
#include <memory>

namespace dxvk {

  /**
   * \brief Turns off the game's cameras that leave the first person
   *
   * Three cameras of the game move the view away from the head:
   *
   * - The idle camera: after some time without input, the game switches to
   *   third person and turns the camera around the player (auto vanity
   *   mode). The game's own bDisableAutoVanityMode:General INI setting
   *   turns it off and is set here.
   * - The kill camera: when the player kills an actor outside V.A.T.S., the
   *   game may show the kill in slow motion. Its mode (none, player,
   *   cinematic) is read from iKillCamera:GamePlay into the player when the
   *   player is created; the cinematic mode, the default, plays the kill
   *   through V.A.T.S. with third-person cameras. The player's mode is set
   *   to the player camera, the slow motion in first person, through the
   *   game's own PlayerCharacter::SetKillCamModeCallback, which ends a
   *   kill camera already running. The INI value is left alone, so the
   *   game's preference file keeps the user's choice.
   * - The third-person view: the game's POV control, or a save made in
   *   third person, asks for it. While the player plays (no menu, no
   *   V.A.T.S. camera, not dead), the game's own
   *   PlayerCharacter::SetFirstPerson puts the first person back. The
   *   game's temporary third person, used to sit down and get up, is left
   *   alone: the game ends it by itself. When it ends it while the view
   *   is already back in first person, the game keeps the third-person
   *   model shown and the hands culled: PlayerCharacter::Show1stPerson
   *   then shows the first-person model again.
   *
   * The cameras of V.A.T.S. itself are not changed here. See
   * findings/camera.md §7.
   *
   * Only used on the game's main thread.
   */
  class VrComfortSettings {

  public:

    /**
     * \brief Checks the game's code and turns the idle camera off
     *
     * Called once the game has read its INI files.
     *
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrComfortSettings> install();

    /**
     * \brief Keeps the kill camera and the view in first person
     *
     * Called once per frame, before the frame is drawn. The player is
     * created after the device, and a value set by a script or the console
     * is put back.
     *
     * \param [in] inGame The game is played, with no menu open
     */
    void update(bool inGame);

  private:

    VrComfortSettings() = default;

    bool m_loggedKillCam = false;
    bool m_loggedView    = false;
    bool m_loggedTemp    = false;
    bool m_loggedModel   = false;

    void keepFirstPerson(uintptr_t player);

    void showFirstPersonModel(uintptr_t player);

  };

}
