#pragma once

namespace dxvk {

  /**
   * \brief Whether the AMD RESZ depth resolve is offered on any GPU
   *
   * The Depth Resolve plugin (DepthResolve.dll) copies the depth buffer
   * through RESZ, and closes the game when RESZ is not offered. DXVK
   * offers it only on an adapter reported as AMD, and the game is shown
   * its real GPU (the launcher restarts it when the adapter name
   * changes), so the plugin refuses Nvidia cards. The copy itself does
   * not depend on the vendor.
   *
   * True only while that plugin is loaded: without it, RESZ stays an
   * AMD-only hack as in upstream DXVK.
   */
  bool vrOffersResz();

}
