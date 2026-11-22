#pragma once

#include <cstddef>

#include <ir/ir_builder.h>

namespace dxvk {

  /**
   * \brief Whether a vertex shader projects with constants c0 to c3
   *
   * True when each component of the position output is written once, by a
   * dp4 of the matching constant (c0 for x to c3 for w) with plain
   * addressing, and no def overrides those constants. The four constants
   * are then the rows of the shader's projection, which other outputs may
   * use too (the water shader hands them to its pixel shader for the
   * refraction and depth lookups).
   *
   * \param [in] bytecode The shader's bytecode
   * \param [in] size Size of the bytecode in bytes
   */
  bool D3D9ProjectsWithFirstConstants(
    const void*                     bytecode,
          size_t                    size);

  /**
   * \brief Moves a projecting vertex shader's other outputs to the stereo eye
   *
   * For shaders found by D3D9ProjectsWithFirstConstants, before the
   * legacy constants are lowered: every load of c0 to c3 the position is
   * not computed from becomes the row of the stereo matrix times the four
   * constants, so that what other outputs draw from the projection (the
   * water's refraction and depth lookups) belongs to the replayed eye. The
   * position itself is left to D3D9ApplyStereoTransform, which runs on
   * every vertex shader.
   *
   * \param [in] builder Shader being lowered
   * \param [in] entryPoint The shader's entry point
   * \returns \c false if the shader was left as it is
   */
  bool D3D9ApplyStereoProjection(
          dxbc_spv::ir::Builder&    builder,
          dxbc_spv::ir::SsaDef      entryPoint);

  /**
   * \brief Multiplies a vertex shader's position output by the stereo matrix
   *
   * The matrix is the identity, except while the VR layer replays the
   * left eye's commands for the right eye (see
   * D3D9DeviceEx::ReplayVrStereoCapture). Clip distances are computed
   * before from the position as written, so the game's clip planes keep
   * their meaning. The front-end stores the four components one after
   * the other at the end of the shader; a shader written otherwise is
   * left as it is.
   *
   * \param [in] builder Shader being lowered
   * \param [in] entryPoint The shader's entry point
   */
  void D3D9ApplyStereoTransform(
          dxbc_spv::ir::Builder&    builder,
          dxbc_spv::ir::SsaDef      entryPoint);

}
