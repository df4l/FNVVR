#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_options_file.h"
#include "vr_tutorials.h"

namespace dxvk {

  namespace {

    using DescriptionFn = const char* (__fastcall*)(uint8_t* description, void* unused, void* form, uint32_t chunk);
    using ControlTextFn = void (__cdecl*)(uint32_t action, char* text, bool withPrefix);
    using FormByIdFn    = uint8_t* (__cdecl*)(uint32_t formId);
    using CreateMenuFn  = bool (__cdecl*)(uint8_t* message, bool withManual);
    using AssignFn      = void* (__fastcall*)(void* string, void* unused, const char* text);

    const VrTutorialTexts* g_texts = nullptr;

    // An unused message of the game (HelpPipBoyRepair): its slot is never
    // raised, so it only opens when a pop-up of the mod asks for it
    constexpr uint32_t TipMessage = 0x0000016E;

    constexpr wchar_t SeenFile[] = L"tutorials_seen.txt";

    // The text the pop-up's message reads
    const std::string* g_tipBody = nullptr;

    constexpr char LanguageSettingName[] = "sLanguage:General";

    const char* settingText(uintptr_t setting) {
      return *reinterpret_cast<const char* const*>(setting + VrGame::SettingValue);
    }

    /** The game's language in lower case, empty if it cannot be read */
    std::string gameLanguage() {
      uintptr_t setting = VrGame::SettingLanguage;

      if (!VrGameMemory::readable(setting, VrGame::SettingName + sizeof(uintptr_t)))
        return std::string();

      uintptr_t name = *reinterpret_cast<const uintptr_t*>(setting + VrGame::SettingName);

      if (!VrGameMemory::readable(name, sizeof(LanguageSettingName))
       || std::memcmp(reinterpret_cast<const void*>(name), LanguageSettingName, sizeof(LanguageSettingName)))
        return std::string();

      const char* value = settingText(setting);

      if (!value || !VrGameMemory::readable(uintptr_t(value), 1))
        return std::string();

      std::string language = value;

      std::transform(language.begin(), language.end(), language.begin(),
        [] (unsigned char c) { return char(std::tolower(c)); });

      // It becomes part of a file's name
      bool plain = std::all_of(language.begin(), language.end(),
        [] (unsigned char c) { return std::isalnum(c) != 0; });

      return plain ? language : std::string();
    }

    bool readTexts(const std::string& language, VrTutorialTexts& texts) {
      std::string text;
      std::string name = "tutorials_" + language + ".txt";

      if (!VrOptionsFile::readText(str::tows(name.c_str()).c_str(), text))
        return false;

      texts.parse(text);
      return true;
    }

  }


  VrTutorials::~VrTutorials() {
    g_texts = nullptr;
    g_tipBody = nullptr;
  }


  std::unique_ptr<VrTutorials> VrTutorials::install() {
    std::unique_ptr<VrTutorials> tutorials(new VrTutorials());

    std::string language = gameLanguage();

    bool hasBase = readTexts("english", tutorials->m_texts);
    bool hasLanguage = !language.empty() && language != "english"
      && readTexts(language, tutorials->m_texts);

    if (!hasBase && !hasLanguage) {
      Logger::warn("VR: fnvvr\\tutorials_english.txt was not found, the tutorials keep the game's text");
      return nullptr;
    }

    g_texts = &tutorials->m_texts;

    if (!VrGameMemory::redirectVirtual(VrGame::MessageDescriptionSlot, VrGame::DescriptionGetText,
          reinterpret_cast<const void*>(&VrTutorials::descriptionHook))) {
      g_texts = nullptr;
      Logger::warn("VR: The message text function was not found, the tutorials keep the game's text");
      return nullptr;
    }

    if (!VrGameMemory::redirectCall(VrGame::ControlTextCallSite, VrGame::ControlPushText,
          reinterpret_cast<const void*>(&VrTutorials::controlTextHook)))
      Logger::warn("VR: The control name function was not found, messages can name the gamepad's buttons");

    std::string seen;

    if (VrOptionsFile::readText(SeenFile, seen))
      tutorials->m_tips.parseSeen(seen);

    tutorials->m_canShowTips =
         VrGameMemory::matches(VrGame::TutorialMenuCreate, VrGame::TutorialMenuCreatePrologue, sizeof(VrGame::TutorialMenuCreatePrologue))
      && VrGameMemory::matches(VrGame::GameStringAssign, VrGame::GameStringAssignPrologue, sizeof(VrGame::GameStringAssignPrologue));

    if (!tutorials->m_canShowTips)
      Logger::warn("VR: The tutorial window was not found, the mod's pop-ups stay hidden");

    Logger::info(str::format("VR: Tutorials: ", tutorials->m_texts.messageCount(), " messages and ",
      tutorials->m_texts.controlCount(), " control names, game language '", language, "'",
      hasLanguage || language == "english" ? "" : ", shown in English"));

    return tutorials;
  }


  void VrTutorials::update(const VrTipInput& input, float dt) {
    if (!m_canShowTips)
      return;

    VrTip tip = m_tips.update(input, dt);

    if (tip != VrTip::None)
      show(tip);
  }


  void VrTutorials::show(VrTip tip) {
    // Seen whatever comes next: a pop-up that fails must not come back each frame
    VrOptionsFile::writeText(SeenFile, m_tips.formatSeen());

    const VrTipText* text = m_texts.tip(vrTipName(tip));
    uint8_t* message = reinterpret_cast<FormByIdFn>(VrGame::FormById)(TipMessage);

    if (!text || !message) {
      Logger::warn(str::format("VR: The pop-up '", vrTipName(tip), "' has no text or no message"));
      return;
    }

    reinterpret_cast<AssignFn>(VrGame::GameStringAssign)(message + VrGame::MessageNameString, nullptr, text->title.c_str());
    g_tipBody = &text->body;

    if (reinterpret_cast<CreateMenuFn>(VrGame::TutorialMenuCreate)(message, false))
      Logger::info(str::format("VR: The pop-up '", vrTipName(tip), "' is shown"));
    else
      Logger::warn(str::format("VR: The pop-up '", vrTipName(tip), "' could not open"));
  }


  const char* __fastcall VrTutorials::descriptionHook(
          uint8_t*              description,
          void*                 unused,
          void*                 form,
          uint32_t              chunk) {
    auto original = reinterpret_cast<DescriptionFn>(VrGame::DescriptionGetText);

    uint8_t* message = description - VrGame::MessageDescription;
    uint32_t formId = *reinterpret_cast<const uint32_t*>(message + VrGame::FormId);
    uint32_t source = vrTutorialTextSource(formId);

    if (formId == TipMessage && g_tipBody)
      return g_tipBody->c_str();

    if (g_texts) {
      if (const std::string* text = g_texts->message(source))
        return text->c_str();
    }

    if (source != formId) {
      if (uint8_t* other = reinterpret_cast<FormByIdFn>(VrGame::FormById)(source))
        return original(other + VrGame::MessageDescription, nullptr, nullptr, chunk);
    }

    return original(description, unused, form, chunk);
  }


  void __cdecl VrTutorials::controlTextHook(
          uint32_t              action,
          char*                 text,
          bool                  withPrefix) {
    const std::string* name = g_texts ? g_texts->control(action) : nullptr;

    if (!name) {
      reinterpret_cast<ControlTextFn>(VrGame::ControlPushText)(action, text, withPrefix);
      return;
    }

    if (!withPrefix) {
      std::strcpy(text, name->c_str());
      return;
    }

    // The buffer holds the game's "press Left Mouse Button" and the like
    uintptr_t setting = action == VrGame::ControlActionAttack
      ? VrGame::SettingControlTriggerPrefix
      : VrGame::SettingControlTextPrefix;

    std::sprintf(text, "%s %s", settingText(setting), name->c_str());
  }

}
