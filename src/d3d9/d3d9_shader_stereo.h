#pragma once

#include <ir/ir_builder.h>

namespace dxvk {

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
