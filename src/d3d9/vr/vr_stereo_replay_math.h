#pragma once

#include <array>
#include <string>

#include "vr_game_space.h"

namespace dxvk {

  /**
   * \brief How d3d9.vrStereoReplayTest forces the replay
   */
  enum class VrStereoReplayTest : uint32_t {
    /// The performance mode decides
    None,
    /// Every frame is replayed while the performance mode is on
    Force,
    /// As Force, with the left eye's world shown unmoved in the right eye,
    /// to tell a wrong matrix from a wrong replay
    Unmoved,
  };

  /**
   * \brief What the game's renderer set for the last camera it drew with
   *
   * NiDX9Renderer::m_kD3DView and m_kD3DProj, row-major Direct3D
   * matrices (row vectors: clip = position * view * projection).
   */
  struct VrRendererCamera {
    std::array<float, 16> view = { };
    std::array<float, 16> projection = { };
  };

  /**
   * \brief One eye's camera as the VR layer gives it to the game
   */
  struct VrStereoEyeCamera {
    VrGameCameraPose pose;
    VrGameFrustum    frustum;
  };

  /**
   * \brief Builds the clip-space matrix from the left eye to the right eye
   *
   * Stereo replay draws the world once, for the left eye, and draws the
   * same commands again for the right eye with every clip position
   * multiplied by this matrix (findings/performance.md, D1). For a point
   * at any distance, P_R V_R (P_L V_L)^-1 maps its left clip position to
   * its right one, since the eyes are a rigid offset apart.
   *
   * The left projection and view are read from the renderer after the
   * left eye's world passes, so that the matrix uses what the game really
   * drew with (near and far planes included). They are checked against
   * the left eye's camera first: a renderer left with another camera
   * gives no matrix.
   *
   * \param [in] renderer The renderer's camera after the left eye's world
   * \param [in] left Left eye's camera
   * \param [in] right Right eye's camera, in the same game space
   * \param [out] rows Matrix, row by row: right[i] = dot(rows[i], left)
   * \param [out] mismatch Why no matrix was built, if one was not
   * \returns \c true if the matrix was built
   */
  bool vrComputeStereoReplayMatrix(
    const VrRendererCamera&   renderer,
    const VrStereoEyeCamera&  left,
    const VrStereoEyeCamera&  right,
          std::array<float, 16>& rows,
          std::string&        mismatch);

  /**
   * \brief Builds the projection the game's renderer builds for a camera
   *
   * Same as NiDX9Renderer::Do_SetCameraData for a perspective frustum
   * without the position adjustment (bPosAdjust, never set by the game).
   *
   * \param [in] frustum Frustum extents as tangents
   * \param [in] nearPlane Distance of the near plane
   * \param [in] farPlane Distance of the far plane
   * \returns Row-major Direct3D projection
   */
  std::array<float, 16> vrComputeGameProjection(
    const VrGameFrustum&      frustum,
          float               nearPlane,
          float               farPlane);

  /**
   * \brief Builds the view the game's renderer builds for a camera
   *
   * Relative to the camera's position, as the game draws (kPosAdjust).
   *
   * \param [in] pose Camera's axes
   * \returns Row-major Direct3D view
   */
  std::array<float, 16> vrComputeGameView(
    const VrGameCameraPose&   pose);

  /**
   * \brief Builds a camera view whose frustum holds both eyes' frustums
   *
   * With the stereo replay, the game culls its objects for the left eye
   * only and the right eye gets the same draws: what only the right eye
   * sees, along its outer edge, would be missing. The view keeps the left
   * eye's axes, takes the widest extent of the two frustums on each side,
   * and moves back from between the eyes until both eyes' frustums are
   * inside it (a few game units for a headset's eye distance). Eyes that
   * are not parallel widen every side by the angle between them.
   *
   * \param [in] left Left eye's camera
   * \param [in] right Right eye's camera, in the same game space
   * \param [out] pose Camera pose to cull with
   * \param [out] frustum Frustum to cull with
   * \returns \c false if a frustum does not hold its own axis (no view built)
   */
  bool vrComputeStereoCullingView(
    const VrStereoEyeCamera&  left,
    const VrStereoEyeCamera&  right,
          VrGameCameraPose&   pose,
          VrGameFrustum&      frustum);

}
