#include <algorithm>
#include <cmath>

#include "../../util/log/log.h"

#include "vr_console_terminal.h"
#include "vr_game_addresses.h"
#include "vr_game_console.h"
#include "vr_game_file.h"
#include "vr_math.h"
#include "vr_terminal_keyboard.h"

namespace dxvk {

  namespace {

    // Steepest the keyboard tilts back to face the eyes, in radians (45 degrees)
    constexpr float MaxPitch = 0.7853982f;

    // A light tick in the hand that typed a key
    constexpr float   KeyHaptic         = 0.15f;
    constexpr int64_t KeyHapticDuration = 15'000'000;

    // The keyboard of the world's terminals and the terminal font (font 5),
    // see findings/console.md §7
    constexpr char KeyboardTexture[] = "Data\\Textures\\Terminals\\terminal01.dds";
    constexpr char FontFile[]        = "Data\\Textures\\Fonts\\Fixedsys_Comp_uniform_width.fnt";
    constexpr char FontTexture[]     = "Data\\Textures\\Fonts\\Fixedsys_Comp_uniform_width_0_Lod_A.dds";

    bool readImage(const char* path, VrImage& image) {
      std::vector<uint8_t> data;
      return VrGameFile::read(path, VrGame::ArchiveTextures, data)
          && vrDecodeDds(data.data(), data.size(), image);
    }

  }


  VrConsoleTerminal::VrConsoleTerminal(VrPainter& painter, IVRBackend& backend, const VrTexts& texts)
  : m_painter(painter), m_backend(backend), m_texts(texts), m_screen(VrConsoleScreen::install(painter.device())),
    m_flatPanel(painter, backend, VrPanelId::Terminal),
    m_flatList(VrTerminalImage::Width, VrTerminalImage::Height),
    m_modelList(0, 0) {
    m_flatPlacement.width  = KeyboardWidth;
    m_flatPlacement.height = KeyboardWidth * float(VrTerminalImage::Height) / float(VrTerminalImage::Width);
  }


  void VrConsoleTerminal::update(const VrPose& head, bool stereo, const VrPointerState& pointer, bool back, float dt) {
    if (m_failed || !VrGameConsole::isOpen()) {
      close(stereo, dt);
      return;
    }

    if (!m_loaded && !load()) {
      Logger::err("VR: Failed to read the terminal's keyboard and font, the console is not shown in the headset");
      m_failed = true;
      return;
    }

    if (!m_active) {
      place(head);
      m_active    = true;
      m_button    = pointer.button;
      m_blinkTime = 0.0f;
      m_pressTime = 0.0f;
      m_view      = VrTerminalKeyboardView();
      Logger::info("VR: The console's keyboard is shown");
    }

    int32_t hovered = pointer.cursorActive ? VrTerminalKeyboard::findKey(pointer.cursor) : -1;

    // A key types when the trigger is pressed on it
    bool pressed = pointer.button && !m_button;
    m_button = pointer.button;

    if (pressed && hovered >= 0)
      press(hovered, pointer);

    // The wheel stick scrolls the output a page per notch
    for (int32_t notch = pointer.wheel; notch >= VrMenuPointer::WheelNotch; notch -= VrMenuPointer::WheelNotch)
      VrGameConsole::pressKey(VrConsoleKey::PageUp);

    for (int32_t notch = pointer.wheel; notch <= -VrMenuPointer::WheelNotch; notch += VrMenuPointer::WheelNotch)
      VrGameConsole::pressKey(VrConsoleKey::PageDown);

    if (back)
      VrGameConsole::requestClose();

    m_blinkTime += dt;
    m_pressTime  = std::max(m_pressTime - dt, 0.0f);

    if (m_pressTime <= 0.0f)
      m_view.pressedKey = -1;

    m_view.hoveredKey = hovered;

    updateScreen(stereo, dt);

    bool changed = !m_viewDrawn || m_view != m_drawnView;
    m_drawnView = m_view;
    m_viewDrawn = true;

    bool shown   = m_screen && m_screen->hasKeyboard() && !m_modelFailed
      ? showModelKeyboard(changed)
      : showFlatKeyboard(changed);

    if (!shown) {
      Logger::err("VR: Failed to draw the console's keyboard, the console is not shown in the headset");
      m_failed = true;
      close(stereo, dt);
    }
  }


  void VrConsoleTerminal::drawEye(const VrEyeView& eye) {
    if (m_screen)
      m_screen->drawEye(eye);
  }


  void VrConsoleTerminal::close(bool stereo, float dt) {
    hideFlatKeyboard();
    m_active = false;

    if (!m_screen)
      return;

    // The keyboard folds before the terminal disappears, where it is seen
    if (stereo && !m_failed) {
      m_screen->close();
      m_screen->update(dt);
    } else {
      m_screen->hide();
    }
  }


  bool VrConsoleTerminal::showFlatKeyboard(bool changed) {
    m_placement  = m_flatPlacement;
    m_modelDrawn = false;

    if (!changed && m_flatPanel.shown())
      return true;

    m_flatList.clear();
    m_image.paint(m_flatList, m_view, 0.0f, 0.0f, nullptr, false);

    if (!m_flatPanel.paint(m_flatList))
      return false;

    m_flatPanel.show(m_placement.pose, KeyboardWidth);
    return true;
  }


  void VrConsoleTerminal::hideFlatKeyboard() {
    m_flatPanel.hide();
  }


  bool VrConsoleTerminal::showModelKeyboard(bool changed) {
    hideFlatKeyboard();

    if (m_modelTexture == nullptr && !createModelTexture()) {
      // The flat keyboard is shown from the next frame on
      Logger::err("VR: Failed to create the texture of the console's 3D keyboard");
      m_modelFailed = true;
      m_screen->hide();
      m_placement = m_flatPlacement;
      return true;
    }

    if (changed || !m_modelDrawn) {
      m_modelImage.paint(m_modelList, m_image, m_view);

      if (!m_painter.paint(m_modelTexture.ptr(), m_modelList, false))
        return false;

      m_modelDrawn = true;
    }

    m_screen->setKeyboardTexture(m_modelTexture.ptr());

    // The lasers only type on the unfolded keyboard
    if (!m_screen->keyboardPlacement(m_placement)) {
      m_placement.width  = 0.0f;
      m_placement.height = 0.0f;
    }

    return true;
  }


  bool VrConsoleTerminal::load() {
    std::vector<uint8_t> font;
    VrBitmapFont metrics;
    VrImage keyboard, glyphs;

    m_loaded = VrGameFile::read(FontFile, VrGame::ArchiveTextures, font)
      && metrics.parse(font.data(), font.size())
      && readImage(FontTexture, glyphs)
      && readImage(KeyboardTexture, keyboard)
      && m_image.initialize(keyboard, metrics, glyphs, m_texts);

    if (m_loaded && !m_modelImage.initialize(keyboard))
      m_modelFailed = true;

    return m_loaded;
  }


  void VrConsoleTerminal::place(const VrPose& head) {
    // The monitor straight ahead at eye height, as the game shows its
    // terminals, and the keyboard in front of its bottom, lowered and
    // tilted back to face the eyes as on a desk
    m_screenAnchor = vrComputePanelPose(head, ScreenDistance);

    VrVector3 centre = vrComputePanelPose(head, KeyboardDistance).position;
    centre.y -= KeyboardDrop;

    m_flatPlacement.pose = vrComputePanelPoseTowards(head, centre, KeyboardDistance, MaxPitch);
    m_placement = m_flatPlacement;
  }


  void VrConsoleTerminal::press(int32_t index, const VrPointerState& pointer) {
    uint32_t count = 0;
    const VrTerminalKey& key = VrTerminalKeyboard::keys(count)[index];

    bool shifted = VrTerminalKeyboard::isShifted(key, m_view.shift, m_view.capsLock);

    switch (key.kind) {
      case VrTerminalKeyKind::Char:
        VrGameConsole::pressKey(VrTerminalKeyboard::code(key, shifted));
        m_view.shift = false;
        break;

      case VrTerminalKeyKind::Edit:
        VrGameConsole::pressKey(VrTerminalKeyboard::code(key, shifted));
        break;

      case VrTerminalKeyKind::Shift:
        m_view.shift = !m_view.shift;
        break;

      case VrTerminalKeyKind::CapsLock:
        m_view.capsLock = !m_view.capsLock;
        break;

      case VrTerminalKeyKind::Close:
        VrGameConsole::requestClose();
        break;
    }

    // The cursor stays lit while typing
    m_blinkTime = 0.0f;
    m_pressTime = PressTime;
    m_view.pressedKey = index;

    for (uint32_t i = 0; i < VrHandCount; i++) {
      if (pointer.beams[i].active)
        m_backend.applyHaptic(VrHand(i), KeyHaptic, KeyHapticDuration);
    }
  }


  void VrConsoleTerminal::updateScreen(bool stereo, float dt) {
    if (!m_screen)
      return;

    if (!stereo || !m_screen->show(m_screenAnchor, ScreenWidth)) {
      m_screen->hide();
      return;
    }

    m_screen->update(dt);

    std::vector<std::string> lines;
    VrGameConsole::readOutput(lines, VrTerminalTextLayout::OutputLines);

    std::string input;
    uint32_t cursor = 0;
    VrGameConsole::readInput(input, cursor);

    VrTerminalScreenText text;
    text.output = vrWrapTerminalLines(lines, VrTerminalTextLayout::Columns, VrTerminalTextLayout::OutputLines);
    vrLayoutTerminalCommand(input, cursor, VrTerminalTextLayout::Columns, text.command, text.cursorColumn);
    text.cursorShown = std::fmod(m_blinkTime, 2.0f * BlinkTime) < BlinkTime;

    m_screen->setText(text);
  }


  bool VrConsoleTerminal::createModelTexture() {
    UINT width  = m_modelImage.width();
    UINT height = m_modelImage.height();

    if (FAILED(m_painter.device()->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
          D3DPOOL_DEFAULT, &m_modelTexture, nullptr))) {
      m_modelTexture = nullptr;
      return false;
    }

    m_modelList  = VrPaintList(width, height);
    m_modelDrawn = false;
    return true;
  }

}
