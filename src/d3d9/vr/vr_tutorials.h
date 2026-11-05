#pragma once

#include <cstdint>
#include <memory>

#include "vr_texts.h"

namespace dxvk {

  /**
   * \brief Shows the game's tutorials with the VR controls
   *
   * The game's messages name the buttons of the gamepad it believes it has,
   * and some describe controls that the headset replaces. The texts come
   * from the files of VrTexts, and are given to the game where it reads a
   * message's text and a control's name, so no game file changes. Messages without a text are
   * left as they are. See findings/tutorials.md.
   */
  class VrTutorials {

  public:

    ~VrTutorials();

    /**
     * \brief Hooks the game
     *
     * \param [in] texts The texts, which must outlive the returned object
     * \returns \c nullptr if there is no text or the executable is not the
     *   supported version
     */
    static std::unique_ptr<VrTutorials> install(const VrTexts& texts);

  private:

    VrTutorials() = default;

    static const char* __fastcall descriptionHook(
            uint8_t*              description,
            void*                 unused,
            void*                 form,
            uint32_t              chunk);

    static void __cdecl controlTextHook(
            uint32_t              action,
            char*                 text,
            bool                  withPrefix);

  };

}
