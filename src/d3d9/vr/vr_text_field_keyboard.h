#pragma once

#include "vr_backend.h"
#include "vr_text_entry.h"

namespace dxvk {

  /**
   * \brief Types in the game's text field with the runtime's keyboard
   *
   * While the field the game asks the player's name with is open, the
   * runtime's own keyboard is shown with the field's question. Its keys are
   * typed in the field as they are pressed, so the field shows the text, and
   * confirming the keyboard enters it. As on a keyboard, the first character
   * replaces the name the field opened with. See VrTextEntry for when the
   * keyboard opens again, and findings/name-entry.md.
   *
   * Only used on the game's main thread.
   */
  class VrTextFieldKeyboard {

  public:

    explicit VrTextFieldKeyboard(IVRBackend& backend);

    ~VrTextFieldKeyboard();

    /**
     * \brief Shows, hides or reads the keyboard for a frame
     *
     * \param [in] dt Duration of a frame in seconds
     */
    void update(float dt);

    /**
     * \brief Whether the keyboard is shown, and has the input
     */
    bool shown() const {
      return m_entry.shown();
    }

  private:

    void show();

    void type(const std::string& typed, bool confirm);

    IVRBackend& m_backend;
    VrTextEntry m_entry;
    bool        m_loggedTyping = false;

  };

}
