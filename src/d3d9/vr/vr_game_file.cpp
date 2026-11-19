#include <cstring>
#include <sstream>
#include <string>

#include <windows.h>

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

    constexpr uint8_t  JumpOpcode = 0xE9;
    constexpr uint32_t JumpLength = 5;

    template<typename T>
    T& field(void* object, uintptr_t offset) {
      return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
    }

    template<typename T>
    T readAt(uintptr_t address) {
      T value;
      std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
      return value;
    }

    template<typename Fn>
    Fn method(void* object, uintptr_t slot) {
      return *reinterpret_cast<Fn*>(field<uint8_t*>(object, 0) + slot);
    }

    void destroy(void* file) {
      method<DestroyFn>(file, 0)(file, nullptr, 1);
    }

    bool hasFinder() {
      return VrGameMemory::matches(VrGame::FileFinderGetFile,
        VrGame::FileFinderGetFilePrologue, sizeof(VrGame::FileFinderGetFilePrologue));
    }

    uintptr_t jumpTarget(uintptr_t address) {
      if (!VrGameMemory::readable(address, JumpLength) || readAt<uint8_t>(address) != JumpOpcode)
        return 0;

      return address + JumpLength + readAt<int32_t>(address + 1);
    }

    // The game's function, or a plugin's hook on it, which the game itself
    // then goes through. Only asked once FileFinder::GetFile has told the
    // executable is the supported one
    bool isGameOrHooked(uintptr_t address, const uint8_t* bytes, size_t size) {
      if (VrGameMemory::matches(address, bytes, size))
        return true;

      uintptr_t target = jumpTarget(address);
      return target && VrGameMemory::isOtherModule(target);
    }

    bool hasArchiveLookup() {
      return VrGameMemory::matches(VrGame::BsHashMakeDirAndFile,
          VrGame::BsHashMakeDirAndFilePrologue, sizeof(VrGame::BsHashMakeDirAndFilePrologue))
        && isGameOrHooked(VrGame::ArchiveEntryForFile,
          VrGame::ArchiveEntryForFilePrologue, sizeof(VrGame::ArchiveEntryForFilePrologue))
        && isGameOrHooked(VrGame::ArchiveFileForEntry,
          VrGame::ArchiveFileForEntryPrologue, sizeof(VrGame::ArchiveFileForEntryPrologue));
    }

    // Archive paths start below the Data folder
    std::string archivePath(const char* path) {
      std::string name = path;

      if (name.size() > 5 && !_strnicmp(name.c_str(), "Data\\", 5))
        name.erase(0, 5);

      return name;
    }

    bool isSingleType(uint32_t archiveType) {
      return archiveType && !(archiveType & (archiveType - 1u));
    }

    uint32_t typeIndexOf(uint32_t archiveType) {
      uint32_t typeIndex = 0;

      while (!(archiveType & (1u << typeIndex)))
        typeIndex += 1;

      return typeIndex;
    }

    struct BsHashes {
      uint8_t directory[VrGame::BsHashSize];
      uint8_t file[VrGame::BsHashSize];
    };

    BsHashes hashesOf(std::string& name) {
      BsHashes hashes = { };
      reinterpret_cast<HashFn>(VrGame::BsHashMakeDirAndFile)(&name[0], hashes.directory, hashes.file);
      return hashes;
    }

    void* findArchiveEntry(std::string& name, uint32_t typeIndex, BsHashes& hashes) {
      return reinterpret_cast<EntryFn>(VrGame::ArchiveEntryForFile)(
        typeIndex, hashes.directory, hashes.file, &name[0]);
    }

    void* openArchiveEntry(uint32_t typeIndex, void* entry) {
      return reinterpret_cast<EntryFileFn>(VrGame::ArchiveFileForEntry)(
        typeIndex, entry, VrGame::ArchiveDefaultBuffer, nullptr);
    }

    void* openWithFinder(const char* path, uint32_t archiveType) {
      return reinterpret_cast<GetFileFn>(VrGame::FileFinderGetFile)(
        path, VrGame::FileReadOnly, VrGame::FileBufferSize, archiveType);
    }

    // The texture loader's lookup: the first archive of the type holding
    // the file, as FileFinder::GetFile, but without its test of bit 31 of
    // the entry's offset, which can hide a file the game itself shows
    void* openFromArchives(const char* path, uint32_t archiveType) {
      if (!isSingleType(archiveType) || !hasArchiveLookup())
        return nullptr;

      std::string name = archivePath(path);
      BsHashes hashes = hashesOf(name);
      uint32_t typeIndex = typeIndexOf(archiveType);
      void* entry = findArchiveEntry(name, typeIndex, hashes);

      return entry ? openArchiveEntry(typeIndex, entry) : nullptr;
    }

    std::string moduleOf(uintptr_t address) {
      MEMORY_BASIC_INFORMATION info = { };
      char name[MAX_PATH] = { };

      if (VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)))
        GetModuleFileNameA(reinterpret_cast<HMODULE>(info.AllocationBase), name, sizeof(name));

      return name[0] ? name : "unnamed memory";
    }

    // "the game's", or what holds the function's place instead
    std::string describeFunction(uintptr_t address, const uint8_t* bytes, size_t size) {
      if (VrGameMemory::matches(address, bytes, size))
        return "the game's";

      if (!VrGameMemory::readable(address, JumpLength))
        return "not readable";

      std::ostringstream text;
      text << "replaced, it starts with" << std::hex;

      for (uint32_t i = 0; i < JumpLength; i++)
        text << ' ' << uint32_t(readAt<uint8_t>(address + i));

      if (uintptr_t target = jumpTarget(address))
        text << ", a jump to 0x" << target << " in '" << moduleOf(target) << "'";

      return text.str();
    }

    std::string describeOpened(void* file) {
      if (!file)
        return "nothing";

      bool good = field<bool>(file, VrGame::FileGood);
      uint32_t size = good ? method<SizeFn>(file, VrGame::FileGetSize)(file, nullptr) : 0;
      destroy(file);

      return good ? str::format("a file of ", size, " bytes") : std::string("a file it could not read");
    }

    const uint8_t* findHashed(uintptr_t entries, uint32_t count, const uint8_t* hash) {
      if (!entries || !count || !VrGameMemory::readable(entries, size_t(count) * VrGame::ArchiveEntrySize))
        return nullptr;

      for (uint32_t i = 0; i < count; i++) {
        auto entry = reinterpret_cast<const uint8_t*>(entries + size_t(i) * VrGame::ArchiveEntrySize);

        if (!std::memcmp(entry, hash, VrGame::BsHashSize))
          return entry;
      }

      return nullptr;
    }

    // Reads the archive list without calling the game: every archive whose
    // directory list has the file's folder, and the file's raw entry there
    void logArchives(const std::string& name, uint32_t archiveType, const BsHashes& hashes) {
      uint32_t archives = 0;
      uint32_t ofType   = 0;
      uint32_t holding  = 0;
      std::string all;

      uintptr_t node = VrGameMemory::readable(VrGame::ArchiveList, sizeof(uint32_t))
        ? readAt<uint32_t>(VrGame::ArchiveList) : 0;

      while (node && VrGameMemory::readable(node, 2 * sizeof(uint32_t))) {
        uintptr_t archive = readAt<uint32_t>(node + VrGame::ArchiveListItem);
        node = readAt<uint32_t>(node + VrGame::ArchiveListNext);

        if (!archive || !VrGameMemory::readable(archive, VrGame::ArchiveFlags + 1))
          continue;

        char path[VrGame::ArchiveNameSize + 1] = { };
        std::memcpy(path, reinterpret_cast<const void*>(archive + VrGame::ArchiveName), VrGame::ArchiveNameSize);

        uint32_t types   = readAt<uint16_t>(archive + VrGame::ArchiveTypeBits);
        uint32_t flags   = readAt<uint8_t>(archive + VrGame::ArchiveFlags);
        uint32_t header  = readAt<uint32_t>(archive + VrGame::ArchiveHeaderFlags);
        uint32_t folders = readAt<uint32_t>(archive + VrGame::ArchiveDirectoryCount);

        archives += 1;
        ofType   += (types & archiveType) ? 1 : 0;
        all += str::format(all.empty() ? "" : ", ", path, " (", std::hex, types, ")");

        const uint8_t* folder = findHashed(readAt<uint32_t>(archive + VrGame::ArchiveDirectories), folders, hashes.directory);

        if (!folder)
          continue;

        holding += 1;

        uint32_t files = readAt<uint32_t>(uintptr_t(folder) + VrGame::ArchiveDirectoryFiles);
        const uint8_t* file = findHashed(readAt<uint32_t>(uintptr_t(folder) + VrGame::ArchiveDirectoryArray), files, hashes.file);

        std::string entry = file
          ? str::format("its entry has size 0x", std::hex, readAt<uint32_t>(uintptr_t(file) + VrGame::ArchiveFileEntrySize),
              " and offset 0x", readAt<uint32_t>(uintptr_t(file) + VrGame::ArchiveFileEntryOffset))
          : std::string("the file is not in it");

        Logger::warn(str::format("VR:   '", path, "' (types 0x", std::hex, types, ", flags 0x", flags,
          ", header flags 0x", header, ") has the folder of '", name, "', ", entry));
      }

      Logger::warn(str::format("VR:   ", archives, " archives loaded, ", ofType, " of type ", archiveType,
        ", ", holding, " with the file's folder; invalidation of older files ",
        readAt<bool>(VrGame::ArchiveInvalidateOlder) ? "on" : "off", "; archives (types): ", all));
    }

  }


  bool VrGameFile::read(const char* path, uint32_t archiveType, std::vector<uint8_t>& data) {
    data.clear();

    if (!hasFinder())
      return false;

    void* file = openFromArchives(path, archiveType);

    if (!file)
      file = openWithFinder(path, archiveType);

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

    // One component of one byte: nothing to swap. The size can be larger
    // than the content: an uncompressed archive with embedded file names
    // counts the name in front of the data, which the game skips on open
    // and then never reads past the entry (ArchiveFile::ReadF)
    data.resize(size);
    uint32_t componentSize = 1;
    uint32_t read = field<ReadFn>(file, VrGame::FileRead)(file, data.data(), size, &componentSize, 1);
    field<uint32_t>(file, VrGame::FilePosition) += read;

    destroy(file);

    data.resize(read);
    return read != 0;
  }


  void VrGameFile::explainMissing(const char* path, uint32_t archiveType) {
    if (!hasFinder() || !isSingleType(archiveType)
     || !VrGameMemory::matches(VrGame::BsHashMakeDirAndFile,
          VrGame::BsHashMakeDirAndFilePrologue, sizeof(VrGame::BsHashMakeDirAndFilePrologue)))
      return;

    std::string name = archivePath(path);
    BsHashes hashes = hashesOf(name);
    uint32_t typeIndex = typeIndexOf(archiveType);

    std::string entryLookup = describeFunction(VrGame::ArchiveEntryForFile,
      VrGame::ArchiveEntryForFilePrologue, sizeof(VrGame::ArchiveEntryForFilePrologue));
    std::string entryOpen = describeFunction(VrGame::ArchiveFileForEntry,
      VrGame::ArchiveFileForEntryPrologue, sizeof(VrGame::ArchiveFileForEntryPrologue));

    std::string loader = "nothing, it is replaced by code that is not a plugin's hook";

    if (hasArchiveLookup()) {
      void* entry = findArchiveEntry(name, typeIndex, hashes);

      loader = entry
        ? str::format("an entry (size 0x", std::hex, readAt<uint32_t>(uintptr_t(entry) + VrGame::ArchiveFileEntrySize),
            ", offset 0x", readAt<uint32_t>(uintptr_t(entry) + VrGame::ArchiveFileEntryOffset), "), opened as ",
            describeOpened(openArchiveEntry(typeIndex, entry)))
        : std::string("no entry");
    }

    std::string finder = describeOpened(openWithFinder(path, archiveType));

    Logger::warn(str::format("VR: Lookup of '", name, "': the texture loader's archive lookup (entry search ",
      entryLookup, ", opening ", entryOpen, ") found ", loader, "; FileFinder::GetFile found ", finder));

    logArchives(name, archiveType, hashes);
  }

}
