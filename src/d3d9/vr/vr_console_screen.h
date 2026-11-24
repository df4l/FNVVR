#pragma once

#include <array>
#include <memory>
#include <string>

#include "../d3d9_include.h"

#include "vr_game_camera.h"
#include "vr_menu_pointer.h"
#include "vr_terminal_keyboard_model.h"
#include "vr_terminal_text.h"
#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Shows the game's console on the game's own terminal, in 3D
   *
   * The terminal is the one the game shows for the terminals and the
   * hacking of its world: its monitor model, its green font and its
   * screen effects (blur, scan lines, flicker). The game is not told that
   * a terminal is open; the VR layer keeps a rendered terminal of the
   * game's own class with a tile tree of its own, built from an XML text
   * in the style of the game's terminal menu, and asks the game to draw it:
   * its screen once per frame where the game draws a terminal's screen,
   * and its model and screen in each eye with that eye's camera, where
   * the interface pass starts. See findings/console.md §6.
   *
   * The world terminal's keyboard hangs below the monitor
   * (VrTerminalKeyboardModel): it unfolds when the terminal is shown and
   * folds when the console closes, before the terminal disappears.
   *
   * Only used on the game's main thread.
   */
  class VrConsoleScreen {

  public:

    /**
     * \brief Hooks the game's offscreen interface render
     *
     * \param [in] device The game's device, which draws the eye images
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrConsoleScreen> install(IDirect3DDevice9* device);

    /**
     * \brief Sets what runs after the game's offscreen interface render
     *
     * For the world's terminals, which also need a screen for both eyes.
     * Outside a frame of the game's renderer, as that render needs.
     *
     * \param [in] callback Called each time, as long as the hook is installed
     */
    static void setAfterOffscreen(void (__cdecl* callback)());

    /**
     * \brief Checks that the hook of the offscreen render is installed
     */
    static bool hooksOffscreen();

    ~VrConsoleScreen();

    VrConsoleScreen(const VrConsoleScreen&) = delete;
    VrConsoleScreen& operator = (const VrConsoleScreen&) = delete;

    /**
     * \brief Shows the terminal
     *
     * Fails while the game shows a rendered menu of its own, such as a
     * terminal of its world.
     *
     * \param [in] anchor Where the middle of the game's view of the
     *    terminal is in the room, facing the viewer: its -Z axis points
     *    away from the viewer
     * \param [in] width Width in metres of what the game shows of the
     *    terminal, the monitor filling the view
     * \returns \c false if the terminal could not be shown
     */
    bool show(const VrPose& anchor, float width);

    /**
     * \brief Folds the keyboard, then hides the terminal
     *
     * Hides it at once when there is no keyboard to fold. The terminal
     * keeps its last text meanwhile.
     */
    void close();

    /**
     * \brief Hides the terminal until it is shown again
     */
    void hide();

    bool isShown() const {
      return m_shown;
    }

    /**
     * \brief Whether the 3D keyboard hangs below the monitor
     */
    bool hasKeyboard() const {
      return m_keyboard && m_keyboard->state() != VrTerminalKeyboardModel::State::Detached;
    }

    /**
     * \brief Plays the keyboard's animation
     *
     * Called once per frame while the terminal is shown or folding.
     *
     * \param [in] dt Duration of a frame in seconds
     */
    void update(float dt);

    /**
     * \brief Where the keyboard's plate is, once unfolded
     *
     * \param [out] placement The keyboard rectangle of the terminal
     *    texture (VrTerminalKeyboard::Plate) in the room
     * \returns \c false while the keyboard is not unfolded
     */
    bool keyboardPlacement(VrPanelPlacement& placement) const;

    /**
     * \brief Sets the texture the keyboard is drawn with
     *
     * \param [in] texture Copy of the terminal texture with the keyboard's
     *    keys, see VrTerminalTexture
     */
    void setKeyboardTexture(IDirect3DBaseTexture9* texture) {
      m_keyboardTexture = texture;
    }

    /**
     * \brief Sets what the screen shows from the next frame on
     */
    void setText(const VrTerminalScreenText& text);

    /**
     * \brief Draws the terminal into the eye image being drawn
     *
     * Called once per eye, after the world and before the interface.
     *
     * \param [in] eye Pose of the eye in tracking space and its field of view
     */
    void drawEye(const VrEyeView& eye);

  private:

    explicit VrConsoleScreen(IDirect3DDevice9* device)
    : m_device(device) { }

    IDirect3DDevice9* m_device;

    // The game's rendered terminal, kept from the first show on
    uint8_t*     m_menu = nullptr;

    // Texture the screen is rendered into while shown (NiPointer), kept
    // for both eyes: the game gives its own back at the end of each eye
    void*        m_texture = nullptr;

    // Tiles of the screen while shown, owned by the game's interface
    void*        m_root    = nullptr;
    void*        m_header[2] = { };
    std::array<void*, VrTerminalTextLayout::OutputLines> m_output = { };
    void*        m_command = nullptr;
    void*        m_cursor  = nullptr;

    bool         m_failed    = false;
    bool         m_shown     = false;
    bool         m_textSet   = false;
    bool         m_debugTextCulled    = false;
    bool         m_debugTextWasCulled = false;

    VrPose       m_anchor;
    VrVector3    m_target;
    float        m_unitsPerMetre = 0.0f;
    VrGameCamera m_camera;

    VrTerminalScreenText m_text;

    std::unique_ptr<VrTerminalKeyboardModel> m_keyboard;
    IDirect3DBaseTexture9* m_keyboardTexture = nullptr;
    bool         m_closing   = false;

    static void __cdecl offscreenHook();

    bool storeXml();

    bool createTiles();

    void destroyTiles();

    bool openMenu(float width);

    void setHeader();

    void renderScreen();

    void hideDebugText();

    void restoreDebugText();

    void releaseTexture();

  };

}
