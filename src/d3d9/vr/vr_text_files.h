#pragma once

#include "vr_texts.h"

namespace dxvk {

  /**
   * \brief Reads the texts in the game's language
   *
   * Reads fnvvr\texts_english.txt, then the file of the game's language over
   * it. Needs the game's INI files to be read, which comes before the game's
   * first call to Direct3DCreate9. See findings/tutorials.md.
   *
   * \returns The texts, empty if there is no file
   */
  VrTexts vrReadTexts();

}
