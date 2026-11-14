#pragma once

namespace dxvk {

  /**
   * \brief Lays the game's interface out at 4:3 at least
   *
   * The game sizes its interface from the window: 960 units high and as wide
   * as the window's aspect makes it, but 1280 units wide on a portrait
   * window. Its interface camera, though, always shows 960 units of height,
   * so on a portrait window, such as a headset eye, it only shows the middle
   * of the menus. The game's functions that give the interface's shape are
   * replaced by ones that use the window's aspect, or 4:3 when the window is
   * narrower (see VrInterfaceMinAspect): the interface is then laid out as on
   * the former 4:3 target and stretched over the narrower window. The game
   * scales its scissor rectangles per axis, so they follow.
   *
   * Nothing changes on a window that is 4:3 or wider.
   */
  class VrInterfaceShape {

  public:

    /**
     * \brief Replaces the game's interface shape functions
     *
     * Must run before the game creates its interface manager, which builds
     * the interface camera. Either all functions are replaced or none.
     *
     * \returns \c true if they were replaced
     */
    static bool install();

  };

}
