#pragma once

#include <cstdint>
#include <memory>

#include "vr_tips.h"
#include "vr_tutorial_text.h"

namespace dxvk {

  /**
   * \brief Shows the game's tutorials with the VR controls
   *
   * The game's messages name the buttons of the gamepad it believes it has,
   * and some describe controls that the headset replaces. The texts come
   * from fnvvr\tutorials_english.txt and the file of the game's language,
   * and are given to the game where it reads a message's text and a
   * control's name, so no game file changes. Messages without a text are
   * left as they are. See findings/tutorials.md.
   */
  class VrTutorials {

  public:

    ~VrTutorials();

    /**
     * \brief Reads the texts and hooks the game
     *
     * Needs the game's INI files to be read.
     *
     * \returns \c nullptr if there is no text or the executable is not the
     *   supported version
     */
    static std::unique_ptr<VrTutorials> install();

    /**
     * \brief Shows the mod's pop-up that the situation calls for
     *
     * Only on the game's main thread, outside the rendering of a pass.
     *
     * \param [in] input The frame's situation
     * \param [in] dt Time since the last frame, in seconds
     */
    void update(const VrTipInput& input, float dt);

  private:

    VrTutorials() = default;

    VrTutorialTexts m_texts;
    VrTipQueue      m_tips;
    bool            m_canShowTips = false;

    void show(VrTip tip);

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
