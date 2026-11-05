#include <algorithm>
#include <cctype>
#include <string>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_options_file.h"
#include "vr_text_files.h"

namespace dxvk {

  namespace {

    constexpr char LanguageSettingName[] = "sLanguage:General";

    /** The game's language in lower case, empty if it cannot be read */
    std::string gameLanguage() {
      uintptr_t setting = VrGame::SettingLanguage;

      if (!VrGameMemory::readable(setting, VrGame::SettingName + sizeof(uintptr_t)))
        return std::string();

      uintptr_t name = *reinterpret_cast<const uintptr_t*>(setting + VrGame::SettingName);

      if (!VrGameMemory::readable(name, sizeof(LanguageSettingName))
       || !vrIsStringSettingName(reinterpret_cast<const char*>(name), LanguageSettingName))
        return std::string();

      const char* value = *reinterpret_cast<const char* const*>(setting + VrGame::SettingValue);

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

    bool readFile(const std::string& language, VrTexts& texts) {
      std::string text;
      std::string name = "texts_" + language + ".txt";

      if (!VrOptionsFile::readText(str::tows(name.c_str()).c_str(), text))
        return false;

      texts.parse(text);
      return true;
    }

  }


  VrTexts vrReadTexts() {
    VrTexts texts;
    std::string language = gameLanguage();

    readFile("english", texts);

    bool translated = language == "english" || (!language.empty() && readFile(language, texts));

    Logger::info(str::format("VR: Texts: ", texts.messageCount(), " messages, ", texts.controlCount(),
      " control names and ", texts.textCount(), " labels, game language '", language, "'",
      translated ? "" : ", shown in English"));

    return texts;
  }

}
