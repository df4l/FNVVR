#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

#include "../../util/log/log.h"
#include "../../util/sha1/sha1_util.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_large_address.h"
#include "vr_large_address_patch.h"
#include "vr_module_path.h"

namespace dxvk {

  namespace {

    using MainMenuIdleFn   = uint8_t (__fastcall*)(void* menu, void* unused);
    using MessageBoxFn     = bool (__cdecl*)(const char* text, const char* icon, const char* title,
      void (__cdecl* callback)(), uint32_t firstButton, uint32_t type, float alpha, float width, ...);
    using MessageResultFn  = int8_t (__cdecl*)();

    /// Highest address a process without the large address aware flag gets, plus one
    constexpr uintptr_t TwoGigabytes = 0x80000000u;

    /// Frames the main menu stays idle before the prompt, so it does not
    /// appear while the menu settles
    constexpr uint32_t ReadyFrames = 30;

    /// Values of the prompt's buttons: the first one gets FirstButton
    constexpr int8_t FirstButton = 1;
    constexpr int8_t PatchButton = FirstButton;
    constexpr int8_t NeverButton = FirstButton + 2;

    constexpr wchar_t BackupName[]       = L"FalloutNV_backup.exe";
    constexpr wchar_t SecondBackupName[] = L"FalloutNV_backup_fnvvr.exe";
    constexpr wchar_t PatchedTempName[]  = L"FalloutNV_fnvvr.tmp";

    struct OfferText {
      const char* name;
      const char* english;
    };

    /**
     * \brief What the main menu offers
     *
     * Both offers apply the same patch; they differ in why the player
     * would want it, so in their texts and in their "don't ask again" file.
     */
    struct Offer {
      const wchar_t* declinedFileName;
      const char*    logName;
      OfferText      prompt;
      OfferText      patch;
      OfferText      done;
      OfferText      failed;
      OfferText      declinedFile;
    };

    /// The process has 2 GB: the patch gives it 4 GB
    const Offer LargeAddressOffer = {
      L"no_large_address_prompt.txt", "4 GB patch",
      { "largeaddress.prompt",
        "Fallout: New Vegas can only use 2 GB of memory. VR and mods fill it quickly, and the game then crashes. "
        "FNVVR can let it use 4 GB, as the FNV 4GB Patcher does: FalloutNV.exe is modified and the original is "
        "kept as FalloutNV_backup.exe. The change takes effect the next time the game starts." },
      { "largeaddress.patch", "Use 4 GB" },
      { "largeaddress.done",
        "FalloutNV.exe can now use 4 GB of memory. Quit and start the game again for the change to take effect." },
      { "largeaddress.failed",
        "FalloutNV.exe could not be modified, the d3d9 log says why. "
        "The FNV 4GB Patcher from Nexus Mods makes the same change." },
      { "largeaddress.declinedfile",
        "While this file exists, FNVVR does not offer to let FalloutNV.exe use 4 GB of memory.\r\n"
        "Delete it to be asked again." },
    };

    /// The process already has 4 GB (Proton) and NVSE is installed: the
    /// patch makes the game load NVSE by itself through nvse_steam_loader.dll
    const Offer NvseOffer = {
      L"no_nvse_prompt.txt", "NVSE loading patch",
      { "nvseload.prompt",
        "NVSE is installed in the game folder. FNVVR can make the game load it by itself, so that it starts "
        "normally from Steam without nvse_loader.exe, as with the FNV 4GB Patcher: FalloutNV.exe is modified and "
        "the original is kept as FalloutNV_backup.exe. The change takes effect the next time the game starts." },
      { "nvseload.patch", "Load NVSE" },
      { "nvseload.done",
        "The game now loads NVSE by itself. Quit and start it again from Steam, without nvse_loader.exe." },
      { "nvseload.failed",
        "FalloutNV.exe could not be modified, the d3d9 log says why. "
        "The FNV 4GB Patcher from Nexus Mods makes the same change." },
      { "nvseload.declinedfile",
        "While this file exists, FNVVR does not offer to make the game load NVSE by itself.\r\n"
        "Delete it to be asked again." },
    };

    const VrTexts* g_texts = nullptr;
    const Offer*   g_offer = nullptr;

    const char* text(const char* name, const char* english) {
      return g_texts->text(name, english);
    }

    const char* text(const OfferText& offerText) {
      return text(offerText.name, offerText.english);
    }

    enum class Step {
      WaitForMenu,  ///< The prompt is shown once the menu is ready
      Asked,        ///< The prompt is open
      Report,       ///< The result is shown once the menu is ready again
      Done,
    };

    Step     g_step        = Step::WaitForMenu;
    uint32_t g_readyFrames = 0;
    bool     g_patched     = false;

    std::wstring executablePath() {
      std::vector<WCHAR> buffer(MAX_PATH);
      DWORD length = 0;

      while ((length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()))) == buffer.size())
        buffer.resize(buffer.size() * 2);

      return std::wstring(buffer.data(), length);
    }

    std::wstring directoryOf(const std::wstring& path) {
      return path.substr(0, path.find_last_of(L"\\/") + 1);
    }

    bool fileExists(const std::wstring& path) {
      return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    std::wstring declinedFilePath(const Offer& offer) {
      std::wstring directory = vrModuleDirectory();
      return directory.empty() ? directory : directory + L"fnvvr\\" + offer.declinedFileName;
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

      if (!fileExists(path)
       || (readFile(path, existing) && sha1Of(existing) == originalSha1))
        return path;

      return directory + SecondBackupName;
    }

    /**
     * \brief Replaces FalloutNV.exe by its patched version
     *
     * Windows lets a running executable be renamed, not written, so the
     * patched file is written beside it, the original renamed to the backup
     * and the patched file renamed to FalloutNV.exe.
     */
    bool patchExecutable() {
      std::wstring path = executablePath();
      std::wstring directory = directoryOf(path);
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

      Logger::info(str::format("VR: FalloutNV.exe is now patched (SHA-1 ", sha1Of(image),
        "), the original is ", str::fromws(backup.c_str())));
      return true;
    }

    void writeDeclinedFile() {
      std::wstring path = declinedFilePath(*g_offer);

      if (path.empty())
        return;

      CreateDirectoryW(path.substr(0, path.find_last_of(L'\\')).c_str(), nullptr);

      const char* declined = text(g_offer->declinedFile);

      if (writeFile(path, declined, std::strlen(declined)))
        Logger::info(str::format("VR: The player declined the ", g_offer->logName, ", it is not offered again"));
      else
        Logger::warn(str::format("VR: Could not write ", str::fromws(path.c_str())));
    }

    void __cdecl onPromptAnswered() {
      int8_t button = reinterpret_cast<MessageResultFn>(VrGame::GetMessageMenuResult)();

      if (button == PatchButton) {
        g_patched = patchExecutable();
        g_step = Step::Report;
      } else {
        if (button == NeverButton)
          writeDeclinedFile();
        else
          Logger::info(str::format("VR: The player postponed the ", g_offer->logName));

        g_step = Step::Done;
      }
    }

    void showMessageBox(const char* text, void (__cdecl* callback)(), const char* button) {
      reinterpret_cast<MessageBoxFn>(VrGame::ShowMessageBox)(text, nullptr, nullptr, callback,
        FirstButton, VrGame::MessageBoxStartMenuType, 0.0f, 0.0f, button, nullptr);
    }

    void showPrompt() {
      reinterpret_cast<MessageBoxFn>(VrGame::ShowMessageBox)(
        text(g_offer->prompt), nullptr, nullptr, &onPromptAnswered,
        FirstButton, VrGame::MessageBoxStartMenuType, 0.0f, 0.0f,
        text(g_offer->patch), text("largeaddress.later", "Not now"),
        text("largeaddress.never", "Don't ask again"), nullptr);
    }

    /// The main menu has faded in, has no message box and is not loading content
    bool menuReady(const uint8_t* menu) {
      uint32_t flags = *reinterpret_cast<const uint32_t*>(menu + VrGame::StartMenuFlags);
      uint32_t busy = VrGame::StartMenuFadeIn | VrGame::StartMenuContentMessage | VrGame::StartMenuPreFade;

      return !(flags & busy) && !*reinterpret_cast<void* const*>(VrGame::MessageMenuGlobal);
    }

    void onMainMenu(const uint8_t* menu) {
      switch (g_step) {
        case Step::WaitForMenu:
          g_readyFrames = menuReady(menu) ? g_readyFrames + 1 : 0;

          if (g_readyFrames >= ReadyFrames) {
            g_step = Step::Asked;
            showPrompt();
          }
          break;

        case Step::Report:
          if (menuReady(menu)) {
            g_step = Step::Done;
            showMessageBox(text(g_patched ? g_offer->done : g_offer->failed),
              nullptr, text("largeaddress.ok", "OK"));
          }
          break;

        case Step::Asked:
        case Step::Done:
          break;
      }
    }

    uint8_t __fastcall mainMenuIdleHook(void* menu, void* unused) {
      uint8_t inMainMenu = reinterpret_cast<MainMenuIdleFn>(VrGame::MainMenuIdle)(menu, unused);

      if (inMainMenu)
        onMainMenu(static_cast<const uint8_t*>(menu));

      return inMainMenu;
    }

  }


  bool VrLargeAddress::install(const VrTexts& texts) {
    g_texts = &texts;

    SYSTEM_INFO system = { };
    GetSystemInfo(&system);

    uintptr_t top = reinterpret_cast<uintptr_t>(system.lpMaximumApplicationAddress);
    Logger::info(str::format("VR: Address space up to 0x", std::hex, top));

    std::wstring executable = executablePath();
    std::wstring gameDirectory = directoryOf(executable);

    if (top < TwoGigabytes) {
      BOOL wow64 = FALSE;

      if (!IsWow64Process(GetCurrentProcess(), &wow64) || !wow64) {
        Logger::info("VR: 32-bit Windows, the game cannot get more than 2 GB of memory");
        return false;
      }

      g_offer = &LargeAddressOffer;
    } else if (fileExists(gameDirectory + L"nvse_loader.exe")
            && fileExists(gameDirectory + L"nvse_steam_loader.dll")) {
      // An original executable with 4 GB means Proton, which gives every
      // executable 4 GB: NVSE then has to be started by nvse_loader.exe
      g_offer = &NvseOffer;
    } else {
      return false;
    }

    std::wstring declined = declinedFilePath(*g_offer);

    if (!declined.empty() && fileExists(declined)) {
      Logger::info(str::format("VR: The ", g_offer->logName, " is not offered while ",
        str::fromws(declined.c_str()), " exists"));
      return false;
    }

    std::vector<uint8_t> image;

    if (!readFile(executable, image)) {
      Logger::warn(str::format("VR: Could not read the executable: ", lastError()));
      return false;
    }

    std::string sha1 = sha1Of(image);
    VrExecutableIdentity identity = vrIdentifyExecutable(sha1);

    if (identity.state != VrExecutableIdentity::State::Original) {
      if (g_offer == &LargeAddressOffer) {
        Logger::warn(str::format("VR: The game has 2 GB of memory and FalloutNV.exe (SHA-1 ", sha1,
          ") is not a version FNVVR can patch; the FNV 4GB Patcher gives it 4 GB"));
      } else if (identity.state == VrExecutableIdentity::State::Patched) {
        Logger::info("VR: FalloutNV.exe is patched, it loads NVSE through nvse_steam_loader.dll");
      } else {
        Logger::info(str::format("VR: NVSE is installed and FalloutNV.exe (SHA-1 ", sha1,
          ") is not a version FNVVR can patch to load it"));
      }
      return false;
    }

    if (!VrGameMemory::matches(VrGame::ShowMessageBox,
          VrGame::ShowMessageBoxPrologue, sizeof(VrGame::ShowMessageBoxPrologue))
     || !VrGameMemory::matches(VrGame::GetMessageMenuResult,
          VrGame::GetMessageMenuResultPrologue, sizeof(VrGame::GetMessageMenuResultPrologue))
     || !VrGameMemory::redirectCall(VrGame::MainMenuIdleCallSite, VrGame::MainMenuIdle,
          reinterpret_cast<const void*>(&mainMenuIdleHook))) {
      Logger::warn(str::format("VR: The main menu was not found, the ", g_offer->logName, " is not offered"));
      return false;
    }

    Logger::info(str::format("VR: The main menu offers the ", g_offer->logName, " of FalloutNV.exe"));
    return true;
  }

}
