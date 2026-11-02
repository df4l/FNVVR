#pragma once

#include <array>
#include <cstdint>

#include "vr_game_space.h"

namespace dxvk {

  /**
   * \brief A scope on the weapon in the game world
   *
   * Lengths are in game units.
   */
  struct VrScopeLens {
    /// Centre of the eyepiece, forward along the scope's axis
    VrGameCameraPose eyepiece;
    /// Radius of the eyepiece
    float radius = 0.0f;
    /// From the eyepiece to the objective, along the axis
    float tubeLength = 0.0f;
  };

  /**
   * \brief A vertex of the lens disc, as the device reads it
   *
   * Position in the eye's frame: +X right, +Y up, +Z forward.
   */
  struct VrScopeLensVertex {
    float x, y, z;
    float u, v;
  };

  /// Segments of the lens disc's rim
  constexpr uint32_t VrScopeLensSegments = 48;

  /// Vertices of the lens disc as a triangle fan
  using VrScopeLensDisc = std::array<VrScopeLensVertex, VrScopeLensSegments + 2>;

  /**
   * \brief How the eye's directions map to the scope's image
   *
   * Texture coordinate per tangent of the angle from the scope's axis, as
   * the eye sees it through the lens.
   */
  struct VrScopeImageScale {
    float x = 1.0f;
    float y = 1.0f;
  };

  /**
   * \brief How much a scope magnifies
   *
   * The ratio of the game's default view to the weapon's, so that the
   * game's values per weapon keep their meaning.
   *
   * \param [in] sightFovDegrees The weapon's horizontal field of view
   */
  float vrComputeScopeMagnification(float sightFovDegrees);

  /**
   * \brief Scale of the image for a scope's render
   *
   * \param [in] renderFov Field of view the image was drawn with
   * \param [in] magnification How much the scope magnifies
   */
  VrScopeImageScale vrComputeScopeImageScale(const VrFov& renderFov, float magnification);

  /**
   * \brief Where the scope's image is drawn from: the objective, along the axis
   */
  VrGameCameraPose vrComputeScopeCamera(const VrScopeLens& lens);

  /**
   * \brief Computes the lens disc for an eye
   *
   * The image lies at infinity: each point of the disc shows the direction
   * from the eye through it, relative to the axis, times the magnification.
   * The disc is flat and square to the axis, so these coordinates vary
   * linearly over it and the GPU interpolates them exactly. An eye off the
   * axis therefore sees the same world through the disc at a shifted place,
   * and sees past the image's edge first on the far side.
   *
   * \param [in] lens The scope
   * \param [in] eye The eye's pose in the game world
   * \param [in] scale Scale of the image
   * \param [out] disc The disc
   * \returns \c false if the eye is not behind the eyepiece
   */
  bool vrComputeScopeLensDisc(
    const VrScopeLens&       lens,
    const VrGameCameraPose&  eye,
    const VrScopeImageScale& scale,
          VrScopeLensDisc&   disc);

  /**
   * \brief Projection of an eye for the device, in row-vector order
   *
   * For positions in the frame of VrScopeLensVertex.
   */
  std::array<float, 16> vrComputeEyeProjection(const VrFov& fov);

}
