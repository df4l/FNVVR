#pragma once

#include <string>

#include "vr_dds.h"

namespace dxvk {

  /**
   * \brief Whether a reticle's texture is a sheet of parts, not a whole reticle
   *
   * The other reticles are one picture covering the lens. The Gauss rifles'
   * scope model builds its reticle from parts of a single texture.
   *
   * \param [in] texture The texture's path, as the model gives it
   */
  bool vrIsReticleSheet(const std::string& texture);

  /**
   * \brief Assembles the Gauss rifles' reticle from its sheet
   *
   * Lays out the parts as the meshes of the scope's model do, in the form of
   * the other reticles: the opening's edge at the same part of the image, each
   * pixel's alpha being how much of the view it hides. Done once, when the
   * texture is decoded (findings/optics.md §14).
   *
   * \param [in] sheet The decoded texture
   * \returns The reticle
   */
  VrImage vrComposeReticleSheet(const VrImage& sheet);

}
