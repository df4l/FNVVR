#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_text_edit.h"
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

    switch (m_entry.update(VrGameTextEdit::isOpen(), VrGameTextEdit::keysPending(), event, dt)) {
      case VrTextEntryStep::Show:
        show();
        break;

      case VrTextEntryStep::Hide:
        m_backend.hideKeyboard();
        break;

      case VrTextEntryStep::Apply:
        apply(typed);
        break;

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
    request.text        = vrUtf8FromGameText(text);
    request.maxLength   = MaxTextLength;

    if (!m_backend.showKeyboard(request)) {
      m_entry.refuse();
      Logger::warn("VR: The runtime's keyboard cannot be shown, the text field is typed in with a keyboard");
      return;
    }

    Logger::info("VR: The runtime's keyboard is shown for the game's text field");
  }


  void VrTextFieldKeyboard::apply(const std::string& typed) {
    std::string title;
    std::string text;
    VrGameTextEdit::read(title, text);

    std::string gameText = vrGameTextFromUtf8(typed);
    VrGameTextEdit::pressKeys(vrTextEntryKeys(uint32_t(text.size()), gameText));

    Logger::info(str::format("VR: The keyboard's text (", gameText.size(),
      " characters) goes to the game's text field"));
  }

}
