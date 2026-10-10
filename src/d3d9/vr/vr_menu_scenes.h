#pragma once

namespace dxvk {

  /**
   * \brief Draws the 3D scenes of menus such as lockpicking
   *
   * Some menus show a 3D model behind their interface: the lock and the
   * pins in the lockpicking menu, the cards and tables of the casino games.
   * The game draws these scenes in the branch of Main::Swap that shows the
   * frozen menu background instead of the world. That background is turned
   * off in VR, so the game draws the world and never reaches the scenes.
   * The VR layer calls the game's own scene functions instead, for the
   * menus that are open. The dark disc the lock model has around it, meant
   * for the frozen background, is hidden. See findings/menu-scenes.md.
   *
   * Only used on the game's main thread, while the game draws a frame.
   */
  class VrMenuScenes {

  public:

    /**
     * \brief Checks that the executable is the supported version
     * \returns \c false if the scene functions were not found
     */
    bool initialize();

    /**
     * \brief Checks whether a menu with a 3D scene is open
     */
    bool isAnyOpen() const;

    /**
     * \brief Draws the scene of every open menu
     *
     * Each scene clears the depth buffer and draws into the bound
     * render target with the menu's own camera.
     */
    void render() const;

  private:

    bool m_available = false;

  };

}
