#include <algorithm>
#include <cstring>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_console_screen.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_space.h"

namespace dxvk {

  namespace {

    using HeapFn          = void* (__cdecl*)(size_t size);
    using MethodFn        = void  (__thiscall*)(void* object);
    using IdleFn          = bool  (__thiscall*)(void* object);
    using DrawFn          = void  (__thiscall*)(void* menu, void* texture, uint32_t clearFlags, void* target);
    using CompressXmlFn   = void* (__cdecl*)(const char* name, const char* text);
    using CacheSetAtFn    = void  (__thiscall*)(void* map, const char* name, void* storage);
    using CacheRemoveAtFn = bool  (__thiscall*)(void* map, const char* name);
    using ReadFileFn      = void* (__thiscall*)(void* parent, const char* name);
    using ChildByNameFn   = void* (__thiscall*)(void* tile, const char* name);
    using SetStringFn     = void  (__thiscall*)(void* tile, uint32_t trait, const char* text, bool propagate);
    using SetFloatFn      = void  (__thiscall*)(void* tile, uint32_t trait, float value, bool propagate);
    using GetFloatFn      = float (__thiscall*)(void* tile, uint32_t trait);
    using DestroyFn       = void  (__thiscall*)(void* object, uint32_t free);
    using AssignFn        = void* (__thiscall*)(void* pointer, void* object);
    using TextureManagerFn = void* (__cdecl*)();
    using BorrowFn        = void* (__thiscall*)(void* manager, void* renderer, uint32_t type,
      uint32_t a, uint32_t b, uint32_t c);
    using ReturnFn        = void  (__thiscall*)(void* manager, void* texture);
    using MainFn          = void  (__thiscall*)(void* main);
    using ScreenFn        = void  (__thiscall*)(void* main, void* screen, void* renderer);
    using OffscreenFn     = void  (__cdecl*)();

    // Name the tiles are stored and read under; the game looks its menus
    // up by the name after the last backslash
    constexpr char XmlName[]  = "fnvvr_console.xml";
    constexpr char RootName[] = "FNVVRConsole";

    // Layout of the game's terminal menu (computers_menu.xml), in its units
    constexpr uint32_t TextLeft      = 100;
    constexpr uint32_t HeaderTop     = 60;
    constexpr uint32_t LineHeight    = 25;
    constexpr uint32_t CharWidth     = 17;
    constexpr uint32_t CommandBottom = 30;
    constexpr uint32_t CursorSize    = 17;
    constexpr uint32_t CursorDrop    = 4;
    // A blank line between the header and the output
    constexpr uint32_t OutputTop     = HeaderTop + 3 * LineHeight;

    // Near and far planes of the eyes' camera on the terminal, in metres
    constexpr float NearPlane = 0.05f;
    constexpr float FarPlane  = 20.0f;

    VrConsoleScreen* g_screen = nullptr;

    void (__cdecl* g_afterOffscreen)() = nullptr;

    template<typename T>
    T& field(void* object, uintptr_t offset) {
      return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
    }

    template<typename Fn>
    Fn method(void* object, uintptr_t slot) {
      return *reinterpret_cast<Fn*>(field<uint8_t*>(object, 0) + slot);
    }

    template<typename Fn>
    Fn game(uintptr_t address) {
      return reinterpret_cast<Fn>(address);
    }

    uint8_t* interfaceManager() {
      return *reinterpret_cast<uint8_t* const*>(VrGame::InterfaceManager);
    }

    std::string outputName(uint32_t line) {
      return str::format("fnvvr_console_output_", line);
    }

    // The game's terminal menu without its lists: the 4:3 area of the
    // screen with the Pip-Boy's background, the header, the lines of output
    // and the command line in the terminal font, and the block cursor
    std::string buildXml() {
      std::string brightness = str::format("<brightness><copy src=\"", RootName, "\" trait=\"brightness\"/></brightness>");

      std::string xml = str::format(
        "<rect name=\"", RootName, "\">"
          "<locus>&true;</locus>"
          "<width><copy src=\"screen()\" trait=\"width\"/></width>"
          "<height><copy src=\"screen()\" trait=\"height\"/></height>"
          "<systemcolor>&terminal;</systemcolor>"
          "<brightness>"
            "<copy>255</copy><onlyif>&highdef;</onlyif>"
            "<add><copy>175</copy><onlyifnot>&highdef;</onlyifnot></add>"
          "</brightness>"
          "<rect name=\"fnvvr_console_area\">"
            "<depth>3</depth>"
            "<locus>&true;</locus>"
            "<height><copy src=\"screen()\" trait=\"height\"/><mult>0.75</mult></height>"
            "<width><copy src=\"me()\" trait=\"height\"/><mult>4</mult><div>3</div></width>"
            "<image name=\"fnvvr_console_background\">"
              "<filename>Interface\\Shared\\Background\\pipboy.dds</filename>"
              "<width><copy src=\"parent()\" trait=\"width\"/></width>"
              "<height><copy src=\"parent()\" trait=\"height\"/></height>"
              "<x>10</x>"
              "<zoom>&scale;</zoom>"
              "<depth>-3</depth>"
              "<alpha>255</alpha>"
              "<brightness>90</brightness>"
            "</image>");

      for (uint32_t i = 0; i < 2; i++) {
        xml += str::format(
            "<text name=\"fnvvr_console_header", i + 1, "\">"
              "<y>", HeaderTop + i * LineHeight, "</y>"
              "<font>5</font>", brightness,
            "</text>");
      }

      for (uint32_t i = 0; i < VrTerminalTextLayout::OutputLines; i++) {
        xml += str::format(
            "<text name=\"", outputName(i), "\">"
              "<x>", TextLeft, "</x>"
              "<y>", OutputTop + i * LineHeight, "</y>"
              "<font>5</font>", brightness,
            "</text>");
      }

      xml += str::format(
            "<text name=\"fnvvr_console_command\">"
              "<x>", TextLeft, "</x>"
              "<y><copy src=\"parent()\" trait=\"height\"/><sub>", LineHeight + CommandBottom, "</sub></y>"
              "<font>5</font>", brightness,
            "</text>"
            "<image name=\"fnvvr_console_cursor\">"
              "<filename>solid.dds</filename>"
              "<texatlas>Interface\\InterfaceShared.tai</texatlas>"
              "<x>", TextLeft, "</x>"
              "<y><copy src=\"sibling(fnvvr_console_command)\" trait=\"y\"/><add>", CursorDrop, "</add></y>"
              "<width>", CursorSize, "</width>"
              "<height>", CursorSize, "</height>"
              "<alpha>255</alpha>", brightness,
            "</image>"
          "</rect>"
        "</rect>");

      return xml;
    }

    void setFloat(void* tile, uint32_t trait, float value) {
      game<SetFloatFn>(VrGame::TileSetFloat)(tile, trait, value, true);
    }

    void setString(void* tile, const std::string& text) {
      game<SetStringFn>(VrGame::TileSetString)(tile, VrGame::TileTraitString, text.c_str(), true);
    }

    const char* settingText(uintptr_t setting) {
      return field<const char*>(reinterpret_cast<void*>(setting), VrGame::SettingValue);
    }

    bool supported() {
      struct Prologue {
        uintptr_t      address;
        const uint8_t* bytes;
        size_t         size;
      };

      const Prologue prologues[] = {
        { VrGame::TerminalConstruct,      VrGame::TerminalConstructPrologue,      sizeof(VrGame::TerminalConstructPrologue) },
        { VrGame::CompressAndStoreXml,    VrGame::CompressAndStoreXmlPrologue,    sizeof(VrGame::CompressAndStoreXmlPrologue) },
        { VrGame::XmlCacheSetAt,          VrGame::XmlCacheSetAtPrologue,          sizeof(VrGame::XmlCacheSetAtPrologue) },
        { VrGame::XmlCacheRemoveAt,       VrGame::XmlCacheRemoveAtPrologue,       sizeof(VrGame::XmlCacheRemoveAtPrologue) },
        { VrGame::TileReadFile,           VrGame::TileReadFilePrologue,           sizeof(VrGame::TileReadFilePrologue) },
        { VrGame::TileGetChildByName,     VrGame::TileGetChildByNamePrologue,     sizeof(VrGame::TileGetChildByNamePrologue) },
        { VrGame::TileSetString,          VrGame::TileSetStringPrologue,          sizeof(VrGame::TileSetStringPrologue) },
        { VrGame::TileSetFloat,           VrGame::TileSetFloatPrologue,           sizeof(VrGame::TileSetFloatPrologue) },
        { VrGame::TileGetFloat,           VrGame::TileGetFloatPrologue,           sizeof(VrGame::TileGetFloatPrologue) },
        { VrGame::GetTextureManager,      VrGame::GetTextureManagerBytes,         sizeof(VrGame::GetTextureManagerBytes) },
        { VrGame::BorrowRenderedTexture,  VrGame::BorrowRenderedTexturePrologue,  sizeof(VrGame::BorrowRenderedTexturePrologue) },
        { VrGame::ReturnRenderedTexture,  VrGame::ReturnRenderedTexturePrologue,  sizeof(VrGame::ReturnRenderedTexturePrologue) },
        { VrGame::DrawRenderedMenuParent, VrGame::DrawRenderedMenuParentPrologue, sizeof(VrGame::DrawRenderedMenuParentPrologue) },
        { VrGame::RenderPipBoy,           VrGame::RenderPipBoyPrologue,           sizeof(VrGame::RenderPipBoyPrologue) },
        { VrGame::ComputersHeaderUseSite, VrGame::ComputersHeaderUse,             sizeof(VrGame::ComputersHeaderUse) },
      };

      for (const auto& prologue : prologues) {
        if (!VrGameMemory::matches(prologue.address, prologue.bytes, prologue.size))
          return false;
      }

      return VrGameMemory::matchesUnlessRewritten(VrGame::HeapAllocate,
            VrGame::HeapAllocatePrologue, sizeof(VrGame::HeapAllocatePrologue))
          && VrGameMemory::matchesUnlessRewritten(VrGame::NiPointerAssign,
            VrGame::NiPointerAssignPrologue, sizeof(VrGame::NiPointerAssignPrologue))
          && VrGameMemory::readable(VrGame::InterfaceManager, sizeof(uintptr_t))
          && VrGameMemory::readable(VrGame::MainSingleton, sizeof(uintptr_t))
          && VrGameMemory::readable(VrGame::Renderer, sizeof(uintptr_t))
          && VrGameMemory::readable(VrGame::WorldSceneGraph, sizeof(uintptr_t))
          && VrGameMemory::readable(VrGame::InterfaceFxTexture, sizeof(uintptr_t))
          && VrGameMemory::readable(VrGame::ComputersHeader2, VrGame::SettingValue + sizeof(uintptr_t));
    }

  }


  std::unique_ptr<VrConsoleScreen> VrConsoleScreen::install(IDirect3DDevice9* device) {
    if (g_screen)
      return nullptr;

    if (!supported() || !VrGameMemory::redirectCall(VrGame::OffscreenInterfaceCallSite,
        VrGame::UpdateOffscreenInterface, reinterpret_cast<const void*>(&offscreenHook))) {
      Logger::warn("VR: The game's terminal was not found, the console is shown without it");
      return nullptr;
    }

    std::unique_ptr<VrConsoleScreen> screen(new VrConsoleScreen(device));
    screen->m_keyboard = VrTerminalKeyboardModel::create();
    g_screen = screen.get();
    return screen;
  }


  VrConsoleScreen::~VrConsoleScreen() {
    // The patched call stays in place and falls through to the game's
    // function. The game's objects are left to the game, which may be
    // shutting down.
    g_screen = nullptr;
  }


  bool VrConsoleScreen::show(const VrPose& anchor, float width) {
    m_anchor = anchor;

    if (m_shown && !m_closing)
      return true;

    // Opened again while folding: the keyboard unfolds again
    if (m_shown) {
      m_closing = false;

      if (m_keyboard)
        m_keyboard->attach(field<void*>(m_menu, VrGame::TerminalModel));

      return true;
    }

    uint8_t* manager = interfaceManager();

    // A terminal of the world uses the same model
    if (m_failed || !manager || field<void*>(manager, VrGame::InterfaceRenderedMenu))
      return false;

    if (!createTiles() || !openMenu(width)) {
      Logger::err("VR: Failed to show the console on the game's terminal");
      destroyTiles();
      m_failed = true;
      return false;
    }

    m_textSet = false;
    setHeader();
    m_shown = true;

    m_debugTextCulled = false;
    hideDebugText();

    if (m_keyboard && m_keyboard->attach(field<void*>(m_menu, VrGame::TerminalModel)))
      Logger::info("VR: The console is shown on the game's terminal, with the keyboard of the world's terminals");
    else
      Logger::info("VR: The console is shown on the game's terminal");

    return true;
  }


  void VrConsoleScreen::close() {
    if (!hasKeyboard()) {
      hide();
      return;
    }

    m_keyboard->close();
    m_closing = true;
  }


  void VrConsoleScreen::update(float dt) {
    if (!m_shown || !m_keyboard)
      return;

    auto previous = m_keyboard->state();
    m_keyboard->update(dt);

    VrPanelPlacement plate;

    if (previous == VrTerminalKeyboardModel::State::Opening && keyboardPlacement(plate)) {
      Logger::info(str::format("VR: The console's keyboard is unfolded, its keys ", plate.width, " m by ",
        plate.height, " m at (", plate.pose.position.x, ", ", plate.pose.position.y, ", ", plate.pose.position.z, ")"));
    }

    if (m_closing && !hasKeyboard())
      hide();
  }


  bool VrConsoleScreen::keyboardPlacement(VrPanelPlacement& placement) const {
    if (!m_shown || m_closing || !m_keyboard
     || m_keyboard->state() != VrTerminalKeyboardModel::State::Open)
      return false;

    placement = VrTerminalKeyboardPlate::placement(m_keyboard->keyboardTransform(),
      m_anchor, m_target, m_unitsPerMetre);
    return true;
  }


  void VrConsoleScreen::hide() {
    if (!m_shown)
      return;

    if (m_keyboard)
      m_keyboard->detach();

    m_closing = false;

    method<MethodFn>(m_menu, VrGame::TerminalOnMenuClose)(m_menu);

    field<void*>(m_menu, VrGame::TerminalMenuTile) = nullptr;
    game<AssignFn>(VrGame::NiPointerAssign)(m_menu + VrGame::TerminalMenuRoot, nullptr);

    destroyTiles();
    releaseTexture();
    restoreDebugText();
    m_shown = false;
  }


  void VrConsoleScreen::setText(const VrTerminalScreenText& text) {
    if (!m_shown)
      return;

    // Every tile is set the first time, then only the ones that changed
    for (uint32_t i = 0; i < m_output.size(); i++) {
      const std::string& line  = i < text.output.size()   ? text.output[i]   : std::string();
      const std::string& drawn = i < m_text.output.size() ? m_text.output[i] : std::string();

      if (!m_textSet || line != drawn)
        setString(m_output[i], line);
    }

    if (!m_textSet || text.command != m_text.command)
      setString(m_command, text.command);

    if (!m_textSet || text.cursorColumn != m_text.cursorColumn)
      setFloat(m_cursor, VrGame::TileTraitX, float(TextLeft + text.cursorColumn * CharWidth));

    if (!m_textSet || text.cursorShown != m_text.cursorShown)
      setFloat(m_cursor, VrGame::TileTraitVisible, text.cursorShown ? 1.0f : 0.0f);

    m_text    = text;
    m_textSet = true;
  }


  void VrConsoleScreen::drawEye(const VrEyeView& eye) {
    if (!m_shown || !m_texture)
      return;

    // The game shows the console's own lines in white over the view
    hideDebugText();

    uint8_t* manager = interfaceManager();
    void*    main      = *reinterpret_cast<void* const*>(VrGame::MainSingleton);
    void*    renderer  = *reinterpret_cast<void* const*>(VrGame::Renderer);
    uint8_t* graph     = *reinterpret_cast<uint8_t* const*>(VrGame::WorldSceneGraph);

    if (!manager || !main || !renderer || !graph)
      return;

    m_camera.apply(vrComputeMenuSceneCameraPose(m_anchor, eye.pose, m_target, m_unitsPerMetre),
      vrComputeGameFrustum(eye.fov));

    // The game draws the current rendered menu: the console's terminal is
    // that menu for these two calls. The world's culling process keeps the
    // camera they set, which is put back after.
    void*  culler = field<void*>(graph, VrGame::SceneGraphCuller);
    void*  camera = culler ? field<void*>(culler, VrGame::CullerCamera) : nullptr;
    void*& current = field<void*>(manager, VrGame::InterfaceRenderedMenu);
    void*  previous = current;
    current = m_menu;

    // The screen is drawn with the texture the game's interface texture
    // points to; the pointer is swapped without touching either count
    void*& texture = *reinterpret_cast<void**>(VrGame::InterfaceFxTexture);
    void*  gameTexture = texture;
    texture = m_texture;

    // The keyboard is drawn with the console's copy of the terminal texture
    if (m_keyboard)
      m_keyboard->beginDraw(m_keyboardTexture);

    // The terminal is nearer than the world, which never hides it. The
    // game's renderer is between its world and interface passes here, so
    // the device clears the depth buffer the eye is drawn with.
    m_device->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    game<MainFn>(VrGame::DrawRenderedMenuParent)(main);
    game<ScreenFn>(VrGame::RenderPipBoy)(main, field<void*>(m_menu, VrGame::TerminalScreen), renderer);

    if (m_keyboard)
      m_keyboard->endDraw();

    texture = gameTexture;
    current = previous;

    if (culler)
      field<void*>(culler, VrGame::CullerCamera) = camera;
  }


  void __cdecl VrConsoleScreen::offscreenHook() {
    game<OffscreenFn>(VrGame::UpdateOffscreenInterface)();

    if (g_screen)
      g_screen->renderScreen();

    if (g_afterOffscreen)
      g_afterOffscreen();
  }


  void VrConsoleScreen::setAfterOffscreen(void (__cdecl* callback)()) {
    g_afterOffscreen = callback;
  }


  bool VrConsoleScreen::hooksOffscreen() {
    return g_screen != nullptr;
  }


  bool VrConsoleScreen::storeXml() {
    // Tile::ReadFile frees the text it reads, so it is stored again for
    // each read and taken out of the game's cache right after
    std::string xml = buildXml();
    void* storage = game<CompressXmlFn>(VrGame::CompressAndStoreXml)(XmlName, xml.c_str());

    if (!storage)
      return false;

    game<CacheSetAtFn>(VrGame::XmlCacheSetAt)(reinterpret_cast<void*>(VrGame::XmlCache), XmlName, storage);
    return true;
  }


  bool VrConsoleScreen::createTiles() {
    void* parent = field<void*>(interfaceManager(), VrGame::InterfaceMenusRoot);

    if (!parent || !storeXml())
      return false;

    // A parser put there by a plugin may not look in the game's cache, and
    // the game then reads a null result from it
    uintptr_t parser = VrGame::TileParseFile;
    bool past = VrGameMemory::callTarget(VrGame::TileParseFileCallSite, &parser)
      && VrGameMemory::isOtherModule(parser)
      && VrGameMemory::redirectCall(VrGame::TileParseFileCallSite, parser,
           reinterpret_cast<const void*>(VrGame::TileParseFile));

    m_root = game<ReadFileFn>(VrGame::TileReadFile)(parent, XmlName);

    if (past) {
      VrGameMemory::redirectCall(VrGame::TileParseFileCallSite, VrGame::TileParseFile,
        reinterpret_cast<const void*>(parser));
      Logger::info("VR: The console's tiles were read past another mod's XML parser");
    }

    game<CacheRemoveAtFn>(VrGame::XmlCacheRemoveAt)(reinterpret_cast<void*>(VrGame::XmlCache), XmlName);

    if (!m_root)
      return false;

    // Hidden from the interface: only the terminal's own render shows it
    setFloat(m_root, VrGame::TileTraitVisible, 0.0f);

    auto child = [this] (const std::string& name) {
      return game<ChildByNameFn>(VrGame::TileGetChildByName)(m_root, name.c_str());
    };

    m_header[0] = child("fnvvr_console_header1");
    m_header[1] = child("fnvvr_console_header2");
    m_command   = child("fnvvr_console_command");
    m_cursor    = child("fnvvr_console_cursor");

    bool found = m_header[0] && m_header[1] && m_command && m_cursor
      && field<void*>(m_root, VrGame::TileNode);

    for (uint32_t i = 0; i < m_output.size(); i++) {
      m_output[i] = child(outputName(i));
      found = found && m_output[i];
    }

    return found;
  }


  void VrConsoleScreen::destroyTiles() {
    if (m_root)
      method<DestroyFn>(m_root, 0)(m_root, 1);

    m_root      = nullptr;
    m_header[0] = nullptr;
    m_header[1] = nullptr;
    m_command   = nullptr;
    m_cursor    = nullptr;
    m_output.fill(nullptr);
  }


  bool VrConsoleScreen::openMenu(float width) {
    if (!m_menu) {
      void* memory = game<HeapFn>(VrGame::HeapAllocate)(VrGame::TerminalSize);

      if (!memory)
        return false;

      game<MethodFn>(VrGame::TerminalConstruct)(memory);
      m_menu = static_cast<uint8_t*>(memory);
    }

    game<AssignFn>(VrGame::NiPointerAssign)(m_menu + VrGame::TerminalMenuRoot,
      field<void*>(m_root, VrGame::TileNode));

    if (!field<bool>(m_menu, VrGame::TerminalInitialized))
      method<MethodFn>(m_menu, VrGame::TerminalInitialize)(m_menu);

    void* camera = field<void*>(m_menu, VrGame::TerminalCamera);
    void* model  = field<void*>(m_menu, VrGame::TerminalModel);

    if (!field<bool>(m_menu, VrGame::TerminalInitialized) || !camera || !model
     || !field<void*>(m_menu, VrGame::TerminalScreen)) {
      game<AssignFn>(VrGame::NiPointerAssign)(m_menu + VrGame::TerminalMenuRoot, nullptr);
      return false;
    }

    // The menu tile's Menu is read while opening, and this tile tree has
    // none: the tile is only given for the screen's effects
    method<MethodFn>(m_menu, VrGame::TerminalOnMenuOpen)(m_menu);
    field<void*>(m_menu, VrGame::TerminalMenuTile) = m_root;

    m_camera.attach(camera);

    if (m_unitsPerMetre <= 0.0f) {
      // The game's view of the model: its camera at the origin looking
      // along +X, the model in front of it, the frustum's right edge a
      // tangent. The width of that view at the model is `width` metres.
      std::memcpy(&m_target, static_cast<uint8_t*>(model) + VrGame::ObjectWorldTranslation, sizeof(m_target));
      float right = field<float>(camera, VrGame::CameraFrustum + sizeof(float));
      float view  = 2.0f * m_target.x * right;

      if (view <= 0.0f || width <= 0.0f) {
        method<MethodFn>(m_menu, VrGame::TerminalOnMenuClose)(m_menu);
        field<void*>(m_menu, VrGame::TerminalMenuTile) = nullptr;
        game<AssignFn>(VrGame::NiPointerAssign)(m_menu + VrGame::TerminalMenuRoot, nullptr);
        return false;
      }

      m_unitsPerMetre = view / width;

      Logger::info(str::format("VR: Console terminal: model at (", m_target.x, ", ", m_target.y, ", ",
        m_target.z, "), view ", view, " units wide, ", m_unitsPerMetre, " units per metre"));
    }

    m_camera.setDepthRange(NearPlane * m_unitsPerMetre, FarPlane * m_unitsPerMetre);
    return true;
  }


  void VrConsoleScreen::setHeader() {
    void* area = game<ChildByNameFn>(VrGame::TileGetChildByName)(m_root, "fnvvr_console_area");
    float areaWidth = area ? game<GetFloatFn>(VrGame::TileGetFloat)(area, VrGame::TileTraitWidth) : 0.0f;

    const uintptr_t settings[2] = { VrGame::ComputersHeader1, VrGame::ComputersHeader2 };

    for (uint32_t i = 0; i < 2; i++) {
      const char* text = settingText(settings[i]);
      std::string line = text ? text : "";

      setString(m_header[i], line);

      // Centred as the game centres it, on its characters' fixed width
      setFloat(m_header[i], VrGame::TileTraitX,
        std::max(0.5f * (areaWidth - float(CharWidth * line.size())), 0.0f));
    }
  }


  void VrConsoleScreen::renderScreen() {
    if (!m_shown)
      return;

    uint8_t* manager = interfaceManager();
    void*    renderer  = *reinterpret_cast<void* const*>(VrGame::Renderer);

    if (!manager || !renderer)
      return;

    // The screen's light, pulse and rolling scan line only move while the
    // game believes a rendered menu is open
    uint8_t& inRenderedMenu = field<uint8_t>(manager, VrGame::InterfaceInRenderedMenu);
    uint8_t  previous = inRenderedMenu;
    inRenderedMenu = 1;
    method<IdleFn>(m_menu, VrGame::TerminalOnIdle)(m_menu);
    inRenderedMenu = previous;

    // Borrowed as the game borrows its interface texture for its own
    // terminals, but kept until the console is hidden: the game gives its
    // own back after each eye, and the right eye does not render it again
    if (!m_texture) {
      void* textures = game<TextureManagerFn>(VrGame::GetTextureManager)();
      void* borrowed = textures ? game<BorrowFn>(VrGame::BorrowRenderedTexture)(textures, renderer,
        VrGame::InterfaceFxTextureType, 0, 0, 0) : nullptr;

      if (borrowed)
        game<AssignFn>(VrGame::NiPointerAssign)(&m_texture, borrowed);
    }

    if (m_texture)
      method<DrawFn>(m_menu, VrGame::TerminalDraw)(m_menu, m_texture, 1, nullptr);
  }


  void VrConsoleScreen::hideDebugText() {
    uint8_t* manager = interfaceManager();
    uint8_t* root = manager ? field<uint8_t*>(manager, VrGame::InterfaceDebugTextRoot) : nullptr;

    if (!root)
      return;

    // FORenderedMenu::Render shows the node again after its pass when the
    // game's debug text is on, so it is hidden again in each eye
    auto& flags = field<uint32_t>(root, VrGame::ObjectFlags);

    if (!m_debugTextCulled) {
      m_debugTextWasCulled = (flags & VrGame::ObjectFlagAppCulled) != 0;
      m_debugTextCulled = true;
    }

    flags |= VrGame::ObjectFlagAppCulled;
  }


  void VrConsoleScreen::restoreDebugText() {
    uint8_t* manager = interfaceManager();
    uint8_t* root = manager ? field<uint8_t*>(manager, VrGame::InterfaceDebugTextRoot) : nullptr;

    if (root && m_debugTextCulled && !m_debugTextWasCulled)
      field<uint32_t>(root, VrGame::ObjectFlags) &= ~VrGame::ObjectFlagAppCulled;

    m_debugTextCulled = false;
  }


  void VrConsoleScreen::releaseTexture() {
    if (!m_texture)
      return;

    void* textures = game<TextureManagerFn>(VrGame::GetTextureManager)();

    if (textures)
      game<ReturnFn>(VrGame::ReturnRenderedTexture)(textures, m_texture);

    game<AssignFn>(VrGame::NiPointerAssign)(&m_texture, nullptr);
  }

}
