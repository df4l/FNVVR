#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

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
    /// In the scope's image and the reticle's texture, which cover the same part of the world
    float u, v;
  };

  /// Segments of the lens disc's rim
  constexpr uint32_t VrScopeLensSegments = 48;

  /// Vertices of the lens disc as a triangle fan
  using VrScopeLensDisc = std::array<VrScopeLensVertex, VrScopeLensSegments + 2>;

  /**
   * \brief The part of the game's image that holds the scope's view
   *
   * In pixels, right and bottom excluded.
   */
  struct VrScopeArea {
    uint32_t left = 0;
    uint32_t top = 0;
    uint32_t right = 0;
    uint32_t bottom = 0;
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
   * \brief Tangent, seen from the eye, of the edge of the scope's image
   *
   * The image and the reticle's texture have the same edge. It does not
   * depend on the weapon: as in the game, a stronger scope shows a smaller
   * part of the world in the same apparent field. The reticle's dark ring
   * lies inside it and acts as the scope's field stop, so an eye off the axis
   * sees the ring close in on one side.
   */
  float vrScopeApparentTangent();

  /**
   * \brief Chooses the centred square the scope's view is drawn in
   *
   * \param [in] width Width of the game's image
   * \param [in] height Height of the game's image
   * \param [in] resolution Wanted side of the square
   */
  VrScopeArea vrComputeScopeArea(uint32_t width, uint32_t height, uint32_t resolution);

  /**
   * \brief Field of view of the game's whole image for a scope's render
   *
   * The game's screen effects need its image at full size, so the view is
   * made wider than the scope's: the scope's view falls on the area, and the
   * rest of the image is not drawn.
   *
   * \param [in] magnification How much the scope magnifies
   * \param [in] width Width of the game's image
   * \param [in] height Height of the game's image
   * \param [in] area The square that holds the scope's view
   */
  VrFov vrComputeScopeRenderFov(float magnification, uint32_t width, uint32_t height, const VrScopeArea& area);

  /**
   * \brief Finds the reticle's texture in a scope's model
   *
   * The game's scope models name their reticle among their textures, next
   * to the textures of the surround, which VR has no use for.
   *
   * \param [in] data The model's file
   * \param [in] size Size of the file in bytes
   * \returns The texture's path as the model gives it, empty if it has none
   */
  std::string vrFindScopeReticleTexture(const uint8_t* data, size_t size);

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
   * \param [out] disc The disc
   * \returns \c false if the eye is not behind the eyepiece
   */
  bool vrComputeScopeLensDisc(
    const VrScopeLens&       lens,
    const VrGameCameraPose&  eye,
          VrScopeLensDisc&   disc);

  /**
   * \brief Projection of an eye for the device, in row-vector order
   *
   * For positions in the frame of VrScopeLensVertex.
   */
  std::array<float, 16> vrComputeEyeProjection(const VrFov& fov);

}
