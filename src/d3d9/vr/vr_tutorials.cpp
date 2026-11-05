#include <cstdio>
#include <cstring>
#include <string>

#include "../../util/log/log.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_tutorials.h"

namespace dxvk {

  namespace {

    using DescriptionFn = const char* (__fastcall*)(uint8_t* description, void* unused, void* form, uint32_t chunk);
    using ControlTextFn = void (__cdecl*)(uint32_t action, char* text, bool withPrefix);
    using FormByIdFn    = uint8_t* (__cdecl*)(uint32_t formId);

    const VrTexts* g_texts = nullptr;

    const char* settingText(uintptr_t setting) {
      return *reinterpret_cast<const char* const*>(setting + VrGame::SettingValue);
    }

  }


  VrTutorials::~VrTutorials() {
    g_texts = nullptr;
  }


  std::unique_ptr<VrTutorials> VrTutorials::install(const VrTexts& texts) {
    if (!texts.messageCount() && !texts.controlCount()) {
      Logger::warn("VR: fnvvr\\texts_english.txt was not found, the tutorials keep the game's text");
      return nullptr;
    }

    std::unique_ptr<VrTutorials> tutorials(new VrTutorials());
    g_texts = &texts;

    if (!VrGameMemory::redirectVirtual(VrGame::MessageDescriptionSlot, VrGame::DescriptionGetText,
          reinterpret_cast<const void*>(&VrTutorials::descriptionHook))) {
      g_texts = nullptr;
      Logger::warn("VR: The message text function was not found, the tutorials keep the game's text");
      return nullptr;
    }

    if (!VrGameMemory::redirectCall(VrGame::ControlTextCallSite, VrGame::ControlPushText,
          reinterpret_cast<const void*>(&VrTutorials::controlTextHook)))
      Logger::warn("VR: The control name function was not found, messages can name the gamepad's buttons");

    return tutorials;
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
