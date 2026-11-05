#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

#include "../../util/log/log.h"
#include "../../util/sha1/sha1_util.h"
#include "../../util/util_string.h"

#include "vr_large_address.h"
#include "vr_large_address_patch.h"
#include "vr_main_menu_prompts.h"
#include "vr_module_path.h"

namespace dxvk {

  namespace {

    /// Highest address a process without the large address aware flag gets, plus one
    constexpr uintptr_t TwoGigabytes = 0x80000000u;

    /// Indices of the prompt's buttons
    constexpr int32_t PatchButton = 0;
    constexpr int32_t NeverButton = 2;

    constexpr wchar_t DeclinedFileName[] = L"no_large_address_prompt.txt";
    constexpr wchar_t BackupName[]       = L"FalloutNV_backup.exe";
    constexpr wchar_t SecondBackupName[] = L"FalloutNV_backup_fnvvr.exe";
    constexpr wchar_t PatchedTempName[]  = L"FalloutNV_fnvvr.tmp";

    const VrTexts* g_texts = nullptr;

    const char* text(const char* name, const char* english) {
      return g_texts->text(name, english);
    }

    enum class Step {
      WaitForMenu,  ///< The prompt is shown once the menu is ready
      Asked,        ///< The prompt is open
      Report,       ///< The result is shown once the menu is ready again
      Done,
    };

    Step g_step    = Step::WaitForMenu;
    bool g_patched = false;

    std::wstring executablePath() {
      std::vector<WCHAR> buffer(MAX_PATH);
      DWORD length = 0;

      while ((length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()))) == buffer.size())
        buffer.resize(buffer.size() * 2);

      return std::wstring(buffer.data(), length);
    }

    std::wstring declinedFilePath() {
      std::wstring directory = vrModuleDirectory();
      return directory.empty() ? directory : directory + L"fnvvr\\" + DeclinedFileName;
    }

    bool readFile(const std::wstring& path, std::vector<uint8_t>& content) {
      HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

      if (file == INVALID_HANDLE_VALUE)
        return false;

      LARGE_INTEGER size = { };
      bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 0x10000000;

      if (ok) {
        content.resize(size_t(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(file, content.data(), DWORD(content.size()), &read, nullptr) && read == content.size();
      }

      CloseHandle(file);
      return ok;
    }

    bool writeFile(const std::wstring& path, const void* data, size_t size) {
      HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

      if (file == INVALID_HANDLE_VALUE)
        return false;

      DWORD written = 0;
      bool ok = WriteFile(file, data, DWORD(size), &written, nullptr) && written == size
        && FlushFileBuffers(file);
      CloseHandle(file);

      if (!ok)
        DeleteFileW(path.c_str());

      return ok;
    }

    std::string sha1Of(const std::vector<uint8_t>& content) {
      return Sha1Hash::compute(content.data(), content.size()).toString();
    }

    std::string lastError() {
      return str::format("error ", GetLastError());
    }

    /**
     * \brief Where the original goes
     *
     * FalloutNV_backup.exe, the name the FNV 4GB Patcher uses, unless a
     * different file already has that name.
     */
    std::wstring backupPath(const std::wstring& directory, const std::string& originalSha1) {
      std::wstring path = directory + BackupName;
      std::vector<uint8_t> existing;

      if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES
       || (readFile(path, existing) && sha1Of(existing) == originalSha1))
        return path;

      return directory + SecondBackupName;
    }

    /**
     * \brief Replaces FalloutNV.exe by its large address aware version
     *
     * Windows lets a running executable be renamed, not written, so the
     * patched file is written beside it, the original renamed to the backup
     * and the patched file renamed to FalloutNV.exe.
     */
    bool patchExecutable() {
      std::wstring path = executablePath();
      std::wstring directory = path.substr(0, path.find_last_of(L"\\/") + 1);
      std::vector<uint8_t> image;

      if (path.empty() || !readFile(path, image)) {
        Logger::err(str::format("VR: Could not read ", str::fromws(path.c_str()), ": ", lastError()));
        return false;
      }

      std::string originalSha1 = sha1Of(image);
      VrExecutableIdentity identity = vrIdentifyExecutable(originalSha1);

      if (identity.state != VrExecutableIdentity::State::Original
       || !vrApplyLargeAddressPatch(image, identity.version)) {
        Logger::err(str::format("VR: FalloutNV.exe changed since the game started (SHA-1 ", originalSha1, "), it is not patched"));
        return false;
      }

      std::wstring temp = directory + PatchedTempName;
      std::wstring backup = backupPath(directory, originalSha1);

      if (!writeFile(temp, image.data(), image.size())) {
        Logger::err(str::format("VR: Could not write ", str::fromws(temp.c_str()), ": ", lastError()));
        return false;
      }

      if (!MoveFileExW(path.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        Logger::err(str::format("VR: Could not rename FalloutNV.exe to ", str::fromws(backup.c_str()), ": ", lastError()));
        DeleteFileW(temp.c_str());
        return false;
      }

      if (!MoveFileExW(temp.c_str(), path.c_str(), 0)) {
        Logger::err(str::format("VR: Could not rename ", str::fromws(temp.c_str()), " to FalloutNV.exe: ", lastError()));
        MoveFileExW(backup.c_str(), path.c_str(), 0);
        DeleteFileW(temp.c_str());
        return false;
      }

      Logger::info(str::format("VR: FalloutNV.exe is now large address aware (SHA-1 ", sha1Of(image),
        "), the original is ", str::fromws(backup.c_str())));
      return true;
    }

    void writeDeclinedFile() {
      std::wstring path = declinedFilePath();

      if (path.empty())
        return;

      CreateDirectoryW(path.substr(0, path.find_last_of(L'\\')).c_str(), nullptr);

      const char* declined = text("largeaddress.declinedfile",
        "While this file exists, FNVVR does not offer to let FalloutNV.exe use 4 GB of memory.\r\n"
        "Delete it to be asked again.");

      if (writeFile(path, declined, std::strlen(declined)))
        Logger::info("VR: The player declined the 4 GB patch, it is not offered again");
      else
        Logger::warn(str::format("VR: Could not write ", str::fromws(path.c_str())));
    }

    void __cdecl onPromptAnswered() {
      int32_t button = VrMainMenuPrompts::answer();

      if (button == PatchButton) {
        g_patched = patchExecutable();
        g_step = Step::Report;
      } else {
        if (button == NeverButton)
          writeDeclinedFile();
        else
          Logger::info("VR: The player postponed the 4 GB patch");

        g_step = Step::Done;
      }
    }

    void showPrompt() {
      VrMainMenuPrompts::show(
        text("largeaddress.prompt",
          "Fallout: New Vegas can only use 2 GB of memory. VR and mods fill it quickly, and the game then crashes. "
          "FNVVR can let it use 4 GB, as the FNV 4GB Patcher does: FalloutNV.exe is modified and the original is "
          "kept as FalloutNV_backup.exe. The change takes effect the next time the game starts."),
        &onPromptAnswered,
        text("largeaddress.patch", "Use 4 GB"), text("largeaddress.later", "Not now"),
        text("largeaddress.never", "Don't ask again"));
    }

    bool promptStep(bool ready) {
      switch (g_step) {
        case Step::WaitForMenu:
          if (ready) {
            g_step = Step::Asked;
            showPrompt();
          }
          return true;

        case Step::Report:
          if (ready) {
            g_step = Step::Done;
            VrMainMenuPrompts::show(g_patched
              ? text("largeaddress.done",
                  "FalloutNV.exe can now use 4 GB of memory. Quit and start the game again for the change to take effect.")
              : text("largeaddress.failed",
                  "FalloutNV.exe could not be modified, the d3d9 log says why. "
                  "The FNV 4GB Patcher from Nexus Mods makes the same change."),
              nullptr, text("largeaddress.ok", "OK"));
          }
          return true;

        case Step::Asked:
          return true;

        case Step::Done:
          break;
      }

      return false;
    }

  }


  bool VrLargeAddress::install(const VrTexts& texts) {
    g_texts = &texts;

    SYSTEM_INFO system = { };
    GetSystemInfo(&system);

    uintptr_t top = reinterpret_cast<uintptr_t>(system.lpMaximumApplicationAddress);
    Logger::info(str::format("VR: Address space up to 0x", std::hex, top));

    if (top >= TwoGigabytes)
      return false;

    BOOL wow64 = FALSE;

    if (!IsWow64Process(GetCurrentProcess(), &wow64) || !wow64) {
      Logger::info("VR: 32-bit Windows, the game cannot get more than 2 GB of memory");
      return false;
    }

    std::wstring declined = declinedFilePath();

    if (!declined.empty() && GetFileAttributesW(declined.c_str()) != INVALID_FILE_ATTRIBUTES) {
      Logger::info(str::format("VR: The game has 2 GB of memory; the 4 GB patch is not offered while ",
        str::fromws(declined.c_str()), " exists"));
      return false;
    }

    std::vector<uint8_t> image;

    if (!readFile(executablePath(), image)) {
      Logger::warn(str::format("VR: Could not read the executable: ", lastError()));
      return false;
    }

    std::string sha1 = sha1Of(image);
    VrExecutableIdentity identity = vrIdentifyExecutable(sha1);

    if (identity.state != VrExecutableIdentity::State::Original) {
      Logger::warn(str::format("VR: The game has 2 GB of memory and FalloutNV.exe (SHA-1 ", sha1,
        ") is not a version FNVVR can patch; the FNV 4GB Patcher gives it 4 GB"));
      return false;
    }

    if (!VrMainMenuPrompts::add(&promptStep)) {
      Logger::warn("VR: The main menu was not found, the 4 GB patch is not offered");
      return false;
    }

    Logger::info("VR: The game has 2 GB of memory, the main menu offers to patch FalloutNV.exe");
    return true;
  }

}
