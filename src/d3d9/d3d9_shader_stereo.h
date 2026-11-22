#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

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
   * \brief Writes a position copied from a projected temporary directly
   *
   * The volumetric fog's vertex shaders project into a temporary, one
   * dp4 of c0 to c3 per component with an input register, copy it to the
   * position with one mov, and compute their screen lookup from the same
   * temporary. The copy is replaced by the four dp4 themselves, written
   * to the position: the same values, and a shader that
   * D3D9ProjectsWithFirstConstants takes, so the lookup moves to the
   * replayed eye with the position.
   *
   * \param [in] bytecode The shader's bytecode
   * \param [in] size Size of the bytecode in bytes
   * \returns The rewritten bytecode, empty if the shader is not written so
   */
  std::vector<uint8_t> D3D9ProjectPositionDirectly(
    const void*                     bytecode,
          size_t                    size);

  /**
   * \brief Moves a projecting vertex shader's other outputs to the stereo eye
   *
   * For shaders found by D3D9ProjectsWithFirstConstants, before the
   * legacy constants are lowered: every load of c0 to c3 the position is
   * not computed from becomes the row of the stereo matrix times the four
   * constants, so that what other outputs draw from the projection (the
   * water's refraction and depth lookups, the volumetric fog's scene depth
   * lookup) belongs to the replayed eye. The
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
