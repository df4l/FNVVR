#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_file.h"
#include "vr_game_memory.h"

namespace dxvk {

  namespace {

    using GetFileFn = void* (__cdecl*)(const char* path, uint32_t mode, uint32_t bufferSize, uint32_t archiveType);
    using SizeFn    = uint32_t (__fastcall*)(void* file, void* unused);
    using ReadFn    = uint32_t (__cdecl*)(void* file, void* buffer, uint32_t bytes, uint32_t* sizes, uint32_t count);
    using DestroyFn = void (__fastcall*)(void* file, void* unused, uint32_t free);

    // Larger files are not among the ones the VR layer reads
    constexpr uint32_t MaxFileSize = 64u << 20;

    template<typename T>
    T& field(void* object, uintptr_t offset) {
      return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
    }

    template<typename Fn>
    Fn method(void* object, uintptr_t slot) {
      return *reinterpret_cast<Fn*>(field<uint8_t*>(object, 0) + slot);
    }

    void destroy(void* file) {
      method<DestroyFn>(file, 0)(file, nullptr, 1);
    }

  }


  bool VrGameFile::read(const char* path, uint32_t archiveType, std::vector<uint8_t>& data) {
    data.clear();

    if (!VrGameMemory::matches(VrGame::FileFinderGetFile,
        VrGame::FileFinderGetFilePrologue, sizeof(VrGame::FileFinderGetFilePrologue)))
      return false;

    void* file = reinterpret_cast<GetFileFn>(VrGame::FileFinderGetFile)(
      path, VrGame::FileReadOnly, VrGame::FileBufferSize, archiveType);

    if (!file)
      return false;

    if (!field<bool>(file, VrGame::FileGood)) {
      destroy(file);
      return false;
    }

    uint32_t size = method<SizeFn>(file, VrGame::FileGetSize)(file, nullptr);

    if (!size || size > MaxFileSize) {
      if (size)
        Logger::warn(str::format("VR: '", path, "' is ", size >> 20, " MB, over the ", MaxFileSize >> 20, " MB the VR layer reads"));

      destroy(file);
      return false;
    }

    // One component of one byte: nothing to swap
    data.resize(size);
    uint32_t componentSize = 1;
    uint32_t read = field<ReadFn>(file, VrGame::FileRead)(file, data.data(), size, &componentSize, 1);
    field<uint32_t>(file, VrGame::FilePosition) += read;

    destroy(file);

    if (read != size) {
      data.clear();
      return false;
    }

    return true;
  }

}
