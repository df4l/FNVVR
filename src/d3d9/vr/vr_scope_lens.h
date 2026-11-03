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
    /// Half the eyepiece's size, across and up
    float halfWidth = 0.0f;
    float halfHeight = 0.0f;
    /// The eyepiece is a rectangle, not a disc
    bool rectangular = false;
    /// Tangent, seen from the eye, of the edge of the scope's image
    float apparentTangent = 0.0f;
    /// Where the scope's image is drawn from
    VrVector3 objective;
    /// The image is amplified and green
    bool nightVision = false;
  };

  /**
   * \brief A vertex of the lens, as the device reads it
   *
   * Position in the eye's frame: +X right, +Y up, +Z forward. The colours are
   * premultiplied by the alpha of the lens's soft edge.
   */
  struct VrScopeLensVertex {
    float x, y, z;
    /// What the glass lets through, and the edge's coverage in its alpha
    uint32_t transmitted;
    /// What the glass reflects, added to the image
    uint32_t reflected;
    /// In the scope's image and the reticle's texture, which cover the same part of the world
    float u, v;
  };

  /// Rings of vertices of the lens, around its centre
  constexpr uint32_t VrScopeLensRings = 9;
  /// Vertices per ring
  constexpr uint32_t VrScopeLensSegments = 64;
  constexpr uint32_t VrScopeLensVertexCount = 1 + VrScopeLensRings * VrScopeLensSegments;
  constexpr uint32_t VrScopeLensTriangles = VrScopeLensSegments + 2 * VrScopeLensSegments * (VrScopeLensRings - 1);

  /// Vertices of the lens, the centre first and then ring by ring
  using VrScopeLensMesh = std::array<VrScopeLensVertex, VrScopeLensVertexCount>;

  /// The triangles of VrScopeLensMesh
  using VrScopeLensIndices = std::array<uint16_t, 3 * VrScopeLensTriangles>;

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
   * \brief A part of an eye's image, each side from 0 to 1
   */
  struct VrScopeLensBounds {
    float left = 0.0f;
    float top = 0.0f;
    float right = 1.0f;
    float bottom = 1.0f;
  };

  /**
   * \brief What the glass does at a point of the lens
   */
  struct VrScopeGlass {
    /// Coverage of the lens's soft edge, from 0 to 1
    float coverage = 1.0f;
    /// What the glass lets through, from 0 to 1, darker at the rim
    float transmission = 1.0f;
    /// What it reflects, per channel
    float reflection[3] = { 0.0f, 0.0f, 0.0f };
  };

  /**
   * \brief How much a scope magnifies
   *
   * The ratio of the game's default view to the weapon's, so that the
   * game's values per weapon keep their meaning.
   *
   * \param [in] sightFovDegrees The weapon's field of view while aiming
   * \param [in] defaultFovDegrees The game's default field of view
   */
  float vrComputeScopeMagnification(float sightFovDegrees, float defaultFovDegrees);

  /**
   * \brief Tangent, seen from the eye, of the edge of the scope's image
   *
   * Chosen as a scope's optics are: from the distance the eye is meant to be
   * at, the dark ring of the reticle, which is the field stop, lies just
   * inside the eyepiece. Nearer the ring shows whole, further the eyepiece
   * cuts the view down, and an eye off the axis sees the ring close in on one
   * side, then nothing. The image and the reticle's texture have the same edge.
   *
   * \param [in] halfWidth Half the eyepiece's width
   * \param [in] halfHeight Half the eyepiece's height
   */
  float vrComputeScopeApparentTangent(float halfWidth, float halfHeight);

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
   * \param [in] apparentTangent See vrComputeScopeApparentTangent
   * \param [in] width Width of the game's image
   * \param [in] height Height of the game's image
   * \param [in] area The square that holds the scope's view
   */
  VrFov vrComputeScopeRenderFov(float magnification, float apparentTangent,
    uint32_t width, uint32_t height, const VrScopeArea& area);

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
   * \brief Where the scope's image is drawn from, looking along the axis
   */
  VrGameCameraPose vrComputeScopeCamera(const VrScopeLens& lens);

  /**
   * \brief Whether an eye can see the scope's image through the lens
   *
   * Decides if the image is worth drawing: the lens is in the eye's view, not
   * too far to matter, and the eye is near enough to the axis that the
   * eyepiece opens on the image and not on the dark past its edge.
   *
   * \param [in] lens The scope
   * \param [in] eye The eye's pose in the game world
   * \param [in] fov The eye's field of view
   */
  bool vrScopeLensShowsImage(const VrScopeLens& lens, const VrGameCameraPose& eye, const VrFov& fov);

  /**
   * \brief Computes what the glass does at a point of the lens
   *
   * \param [in] lens The scope
   * \param [in] radial Where the point is, from 0 at the centre to 1 at the rim
   * \param [in] view Unit direction from the eye to the point
   */
  VrScopeGlass vrComputeScopeGlass(const VrScopeLens& lens, float radial, const VrVector3& view);

  /**
   * \brief Computes the lens for an eye
   *
   * The image lies at infinity: each point of the lens shows the direction
   * from the eye through it, relative to the axis, times the magnification.
   * The lens is flat and square to the axis, so these coordinates vary
   * linearly over it and the GPU interpolates them exactly. An eye off the
   * axis therefore sees the same world through the lens at a shifted place,
   * and sees past the image's edge first on the far side.
   *
   * \param [in] lens The scope
   * \param [in] eye The eye's pose in the game world
   * \param [out] mesh The lens
   * \returns \c false if the eye is not behind the eyepiece
   */
  bool vrComputeScopeLensMesh(
    const VrScopeLens&       lens,
    const VrGameCameraPose&  eye,
          VrScopeLensMesh&   mesh);

  /**
   * \brief The triangles of the lens
   */
  const VrScopeLensIndices& vrScopeLensIndices();

  /**
   * \brief The part of an eye's image a lens covers
   *
   * \param [in] mesh The lens
   * \param [in] fov The eye's field of view
   * \param [out] bounds The part of the image, the whole of it if the lens
   *    reaches behind the eye
   * \returns \c false if the lens is outside the image
   */
  bool vrComputeScopeLensBounds(const VrScopeLensMesh& mesh, const VrFov& fov, VrScopeLensBounds& bounds);

  /**
   * \brief Projection of an eye for the device, in row-vector order
   *
   * For positions in the frame of VrScopeLensVertex. Gives the depth the game
   * gives to its own objects drawn with the same near and far distances.
   *
   * \param [in] fov The eye's field of view
   * \param [in] nearDistance The game camera's near distance
   * \param [in] farDistance The game camera's far distance
   */
  std::array<float, 16> vrComputeEyeProjection(const VrFov& fov, float nearDistance, float farDistance);

}
