#include <cstring>

#include "vr_game_addresses.h"
#include "vr_game_sound.h"

namespace dxvk {

  namespace {

    // TESDataHandler::GetSound, __thiscall with the editor ID
    using FindSoundFn = const uint8_t* (__fastcall*)(void* dataHandler, void* unused, const char* editorId);

    // TESSound::GetFilename, __thiscall without arguments
    using GetFilenameFn = const char* (__fastcall*)(const uint8_t* sound, void* unused);

    // TESSound::ConvertLoopFlag, __cdecl with the sound's flags
    using ConvertLoopFlagFn = uint32_t (__cdecl*)(uint32_t flags);

    // BSAudio::GetSoundHandleByFilename, __thiscall with four stack arguments
    using GetSoundFn = void* (__fastcall*)(void* audio, void* unused,
      void* handle, const char* file, uint32_t flags, const uint8_t* sound);

    // BSSoundHandle functions, __thiscall
    using SetPositionFn  = uint32_t (__fastcall*)(void* handle, void* unused, float x, float y, float z);
    using SetFloatFn     = uint32_t (__fastcall*)(void* handle, void* unused, float value);
    using PlayFn         = uint32_t (__fastcall*)(void* handle, void* unused, uint32_t loop);
    using StopFn         = uint32_t (__fastcall*)(void* handle, void* unused);

    /// BSSoundHandle: the sound's ID, -1 without one, and its state
    struct SoundHandle {
      int32_t  id      = -1;
      uint8_t  assume  = 0;
      uint32_t state   = 0;
    };

    static_assert(sizeof(SoundHandle) == 12);

  }


  const uint8_t* vrFindGameSound(const std::string& editorId) {
    auto dataHandler = *reinterpret_cast<void* const*>(VrGame::DataHandler);

    if (!dataHandler || editorId.empty())
      return nullptr;

    return reinterpret_cast<FindSoundFn>(VrGame::DataHandlerGetSound)(dataHandler, nullptr, editorId.c_str());
  }


  const char* vrGameSoundFile(const uint8_t* sound) {
    const char* file = sound ? reinterpret_cast<GetFilenameFn>(VrGame::SoundGetFilename)(sound, nullptr) : nullptr;
    return file ? file : "";
  }


  int32_t vrPlayGameSound(
    const uint8_t*              sound,
          uint32_t              flags,
    const VrVector3&            position,
          float                 volume,
          float                 frequency) {
    auto audio = *reinterpret_cast<void* const*>(VrGame::AudioSingleton);

    if (!sound || !audio)
      return -1;

    uint32_t soundFlags;
    std::memcpy(&soundFlags, sound + VrGame::SoundFormFlags, sizeof(soundFlags));
    uint32_t loop = reinterpret_cast<ConvertLoopFlagFn>(VrGame::SoundConvertLoopFlag)(soundFlags);

    SoundHandle handle;
    reinterpret_cast<GetSoundFn>(VrGame::AudioGetSoundByFilename)(audio, nullptr,
      &handle, vrGameSoundFile(sound), flags | loop, sound);

    reinterpret_cast<SetPositionFn>(VrGame::SoundHandleSetPosition)(&handle, nullptr, position.x, position.y, position.z);
    reinterpret_cast<SetFloatFn>(VrGame::SoundHandleSetVolume)(&handle, nullptr, volume);
    reinterpret_cast<SetFloatFn>(VrGame::SoundHandleSetFrequency)(&handle, nullptr, frequency);
    reinterpret_cast<PlayFn>(VrGame::SoundHandlePlay)(&handle, nullptr, loop != 0);
    return handle.id;
  }


  bool vrPlayInterfaceSound(const char* editorId) {
    const uint8_t* sound = vrFindGameSound(editorId);

    if (!sound)
      return false;

    vrPlayGameSound(sound, VrGame::SoundFlag2D | VrGame::SoundFlagSystem | VrGame::SoundFlagOneShot,
      VrVector3(), 1.0f, 1.0f);
    return true;
  }


  int32_t vrPlaySoundFile(const char* path) {
    auto audio = *reinterpret_cast<void* const*>(VrGame::AudioSingleton);

    if (!audio || !path || !*path)
      return -1;

    SoundHandle handle;
    reinterpret_cast<GetSoundFn>(VrGame::AudioGetSoundByFilename)(audio, nullptr, &handle, path,
      VrGame::SoundFlag2D | VrGame::SoundFlagSystem | VrGame::SoundFlagOneShot, nullptr);

    if (handle.id == -1)
      return -1;

    reinterpret_cast<PlayFn>(VrGame::SoundHandlePlay)(&handle, nullptr, 0);
    return handle.id;
  }


  bool vrGameSoundLoops(const uint8_t* sound) {
    if (!sound)
      return false;

    uint32_t soundFlags;
    std::memcpy(&soundFlags, sound + VrGame::SoundFormFlags, sizeof(soundFlags));
    return reinterpret_cast<ConvertLoopFlagFn>(VrGame::SoundConvertLoopFlag)(soundFlags) != 0;
  }


  int32_t vrPlayFirstPersonSound(
    const VrAnimationSound&     sound,
    const VrVector3&            position,
          float                 speed) {
    if (!sound.sound)
      return -1;

    uint32_t soundFlags;
    std::memcpy(&soundFlags, sound.sound + VrGame::SoundFormFlags, sizeof(soundFlags));

    uint32_t flags = ((soundFlags & VrGame::SoundForm2D) ? VrGame::SoundFlag2D : VrGame::SoundFlag3D)
      | VrGame::SoundFlagOneShot | VrGame::SoundFlagFirstPerson
      | VrGame::SoundFlagIgnoreTimescale | VrGame::SoundFlagAnimationDriven;

    return vrPlayGameSound(sound.sound, flags, position, sound.volume, sound.pitch * speed);
  }


  void vrStopGameSound(int32_t id) {
    if (id == -1)
      return;

    SoundHandle handle;
    handle.id = id;
    reinterpret_cast<StopFn>(VrGame::SoundHandleStop)(&handle, nullptr);
  }

}
