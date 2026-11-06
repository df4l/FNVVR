#pragma once

#include "vr_backend.h"
#include "vr_text_entry.h"

namespace dxvk {

  /**
   * \brief Types in the game's text field with the runtime's keyboard
   *
   * While the field the game asks the player's name with is open, the
   * runtime's own keyboard is shown with the field's question and text.
   * The confirmed text replaces the field's and is confirmed in the game,
   * as if typed and entered. See VrTextEntry for when the keyboard opens
   * again, and findings/name-entry.md.
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

    void apply(const std::string& typed);

    IVRBackend& m_backend;
    VrTextEntry m_entry;

  };

}
