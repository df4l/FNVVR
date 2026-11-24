#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_text_edit.h"
#include "vr_terminal_keyboard.h"
#include "vr_text_field_keyboard.h"

namespace dxvk {

  namespace {

    // Characters the keyboard takes at most; the game cuts the text
    // further to the width of its field
    constexpr uint32_t MaxTextLength = 64;

  }


  VrTextFieldKeyboard::VrTextFieldKeyboard(IVRBackend& backend)
  : m_backend(backend) { }


  VrTextFieldKeyboard::~VrTextFieldKeyboard() {
    if (m_entry.shown())
      m_backend.hideKeyboard();
  }


  void VrTextFieldKeyboard::update(float dt) {
    std::string typed;
    VrKeyboardEvent event = m_backend.takeKeyboardEvent(typed);

    bool open   = VrGameTextEdit::isOpen();
    bool typing = open && m_entry.shown();

    VrTextEntryStep step = m_entry.update(open, VrGameTextEdit::keysPending(), event, dt);

    // The keys pressed up to the confirmation or the close go in first
    if (typing)
      type(typed, step == VrTextEntryStep::Confirm);

    switch (step) {
      case VrTextEntryStep::Show:
        show();
        break;

      case VrTextEntryStep::Hide:
        m_backend.hideKeyboard();
        break;

      case VrTextEntryStep::Confirm:
      case VrTextEntryStep::None:
        break;
    }
  }


  void VrTextFieldKeyboard::show() {
    std::string title;
    std::string text;
    VrGameTextEdit::read(title, text);

    VrKeyboardRequest request;
    request.description = vrUtf8FromGameText(title);
    request.maxLength   = MaxTextLength;

    if (!m_backend.showKeyboard(request)) {
      m_entry.refuse();
      Logger::warn("VR: The runtime's keyboard cannot be shown, the text field is typed in with a keyboard");
      return;
    }

    Logger::info("VR: The runtime's keyboard is shown for the game's text field");
  }


  void VrTextFieldKeyboard::type(const std::string& typed, bool confirm) {
    std::vector<uint32_t> keys = vrTextEntryKeys(typed);

    if (confirm)
      keys.push_back(VrConsoleKey::Enter);

    if (!keys.empty())
      VrGameTextEdit::pressKeys(keys);

    if (!keys.empty() && !m_loggedTyping) {
      Logger::info("VR: The keyboard's keys are typed in the game's text field as they are pressed");
      m_loggedTyping = true;
    }

    if (confirm)
      Logger::info("VR: The keyboard is confirmed, the game's text field is entered");
  }

}
