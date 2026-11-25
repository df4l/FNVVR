#pragma once

#include <cstdint>
#include <string>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief A sound of one of the game's animations
   */
  struct VrAnimationSound {
    /// The TESSound form, or \c nullptr if the animation has none there
    const uint8_t* sound  = nullptr;
    float          volume = 1.0f;
    float          pitch  = 1.0f;
  };

  /**
   * \brief Finds a sound form by its editor ID
   *
   * \param [in] editorId The TESSound's editor ID
   * \returns The TESSound, or \c nullptr if there is none of that name
   */
  const uint8_t* vrFindGameSound(const std::string& editorId);

  /**
   * \brief File name of a sound form, for logs
   */
  const char* vrGameSoundFile(const uint8_t* sound);

  /**
   * \brief Plays a sound form once, as the game plays animation sounds
   *
   * \param [in] sound The TESSound
   * \param [in] flags BSAudio flags, without the sound's loop flag,
   *    which is added from the form
   * \param [in] position Where the sound is, in game units, for 3D sounds
   * \param [in] volume Volume, from 0 to 1
   * \param [in] frequency Playback speed, 1 for the recorded one
   * \returns The sound's ID, -1 if it could not be played
   */
  int32_t vrPlayGameSound(
    const uint8_t*              sound,
          uint32_t              flags,
    const VrVector3&            position,
          float                 volume,
          float                 frequency);

  /**
   * \brief Plays a sound form once as the interface plays its sounds
   *
   * Not placed in the world (2D), as a system sound, as the game's menus
   * and HUD messages play theirs by editor ID.
   *
   * \param [in] editorId The TESSound's editor ID
   * \returns \c false if there is no sound of that name
   */
  bool vrPlayInterfaceSound(const char* editorId);

  /**
   * \brief Plays a sound file once as the interface plays its sounds
   *
   * Not placed in the world (2D), as a system sound.
   *
   * \param [in] path The file, from the game's folder or, starting with
   *    "fx\\", from the sound folder (which the game adds)
   * \returns The sound's ID, -1 if it could not be played
   */
  int32_t vrPlaySoundFile(const char* path);

  /**
   * \brief Whether a sound form loops
   *
   * A looping sound played once goes on until it is stopped.
   */
  bool vrGameSoundLoops(const uint8_t* sound);

  /**
   * \brief Stops a sound played earlier
   *
   * \param [in] id The sound's ID, nothing is done for -1
   */
  void vrStopGameSound(int32_t id);

  /**
   * \brief Plays an animation's sound once for the player in first person
   *
   * As TESAnimGroup::PlaySounds plays the sounds of the player's
   * first-person animations.
   *
   * \param [in] sound The sound
   * \param [in] position Where the sound is, in game units, for 3D sounds
   * \param [in] speed How fast the animation would play
   * \returns The sound's ID, -1 if it could not be played
   */
  int32_t vrPlayFirstPersonSound(
    const VrAnimationSound&     sound,
    const VrVector3&            position,
          float                 speed);

}
