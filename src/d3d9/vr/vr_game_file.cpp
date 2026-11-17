#include <cstring>
#include <string>

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
    using HashFn    = void (__cdecl*)(char* path, void* directoryHash, void* fileHash);
    using EntryFn   = void* (__cdecl*)(uint32_t typeIndex, void* directoryHash, void* fileHash, char* path);
    using EntryFileFn = void* (__cdecl*)(uint32_t typeIndex, void* entry, uint32_t bufferSize, const char* name);

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

    bool hasArchiveLookup() {
      return VrGameMemory::matches(VrGame::BsHashMakeDirAndFile,
          VrGame::BsHashMakeDirAndFilePrologue, sizeof(VrGame::BsHashMakeDirAndFilePrologue))
        && VrGameMemory::matches(VrGame::ArchiveEntryForFile,
          VrGame::ArchiveEntryForFilePrologue, sizeof(VrGame::ArchiveEntryForFilePrologue))
        && VrGameMemory::matches(VrGame::ArchiveFileForEntry,
          VrGame::ArchiveFileForEntryPrologue, sizeof(VrGame::ArchiveFileForEntryPrologue));
    }

    // The texture loader's lookup: the first archive of the type holding
    // the file, as FileFinder::GetFile, but without its test of bit 31 of
    // the entry's offset, which can hide a file the game itself shows
    void* openFromArchives(const char* path, uint32_t archiveType) {
      if (!archiveType || (archiveType & (archiveType - 1u)) || !hasArchiveLookup())
        return nullptr;

      uint32_t typeIndex = 0;

      while (!(archiveType & (1u << typeIndex)))
        typeIndex += 1;

      // Archive paths start below the Data folder
      std::string name = path;

      if (name.size() > 5 && !_strnicmp(name.c_str(), "Data\\", 5))
        name.erase(0, 5);

      uint8_t directoryHash[VrGame::BsHashSize];
      uint8_t fileHash[VrGame::BsHashSize];
      reinterpret_cast<HashFn>(VrGame::BsHashMakeDirAndFile)(&name[0], directoryHash, fileHash);

      void* entry = reinterpret_cast<EntryFn>(VrGame::ArchiveEntryForFile)(
        typeIndex, directoryHash, fileHash, &name[0]);

      if (!entry)
        return nullptr;

      return reinterpret_cast<EntryFileFn>(VrGame::ArchiveFileForEntry)(
        typeIndex, entry, VrGame::ArchiveDefaultBuffer, nullptr);
    }

  }


  bool VrGameFile::read(const char* path, uint32_t archiveType, std::vector<uint8_t>& data) {
    data.clear();

    if (!VrGameMemory::matches(VrGame::FileFinderGetFile,
        VrGame::FileFinderGetFilePrologue, sizeof(VrGame::FileFinderGetFilePrologue)))
      return false;

    void* file = openFromArchives(path, archiveType);

    if (!file) {
      file = reinterpret_cast<GetFileFn>(VrGame::FileFinderGetFile)(
        path, VrGame::FileReadOnly, VrGame::FileBufferSize, archiveType);
    }

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
