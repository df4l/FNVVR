#pragma once

#include <cstdint>
#include <vector>

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
   * Rendered menus other than the Pip-Boy, such as the Reflectron of the
   * character creation, are drawn by the game with a camera of their own
   * that ignores the head, so they would stick to the face in both eyes.
   * Their draw in the eye images is skipped, the model and then its screen
   * with the menu and the face, and they are drawn with the menu scenes
   * instead.
   *
   * Only used on the game's main thread, while the game draws a frame.
   */
  class VrMenuScenes {

  public:

    ~VrMenuScenes();

    /**
     * \brief Checks that the executable is the supported version
     *
     * Also moves the rendered menus' draw out of the eye images.
     *
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

    /**
     * \brief Checks whether the open rendered menu is drawn with the scenes
     *
     * Its screen is then left out of the eye images as well.
     */
    static bool movesRenderedMenu();

    /**
     * \brief Leaves a menu's scene to the eye images
     *
     * See VrMinigames. That scene is no longer drawn into the menu panel.
     *
     * \param [in] menuId The menu, or 0 for none
     * \param [in] terminal Whether the rendered terminal is left out as well
     */
    void leaveToEyes(uint32_t menuId, bool terminal) {
      m_eyeMenuId   = menuId;
      m_eyeTerminal = terminal;
    }

    /**
     * \brief Hides the dark disc of the lock model
     *
     * \param [out] hidden Flags to pass to showHidden after the draw
     */
    static void hideLockBackdrop(std::vector<uint32_t*>& hidden);

    static void showHidden(const std::vector<uint32_t*>& hidden);

    /**
     * \brief Hides the objects of a name below an object
     *
     * \param [in] root The object it starts from
     * \param [in] name Name of the objects
     * \param [out] hidden Flags to pass to showHidden
     */
    static void hideObject(uintptr_t root, const char* name, std::vector<uint32_t*>& hidden);

  private:

    bool m_available         = false;
    bool m_renderedMenuMoved = false;
    uint32_t m_eyeMenuId     = 0;
    bool m_eyeTerminal       = false;

    static void __fastcall renderedMenuHook(
            void*                 main,
            void*                 unused);

    static bool isRenderedMenuOpen();

  };

}
