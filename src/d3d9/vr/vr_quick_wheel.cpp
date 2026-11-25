#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_ammo.h"
#include "vr_math.h"
#include "vr_quick_wheel.h"

namespace dxvk {

  namespace {

    // Steepest the wheel tilts to face the eyes, in radians (70 degrees)
    constexpr float MaxPitch = 1.22f;

    const VrQuickWheelPicking Picking;

  }


  VrQuickWheel::VrQuickWheel(VrPainter& painter, IVRBackend& backend, VrGameImages& images)
  : m_panel(painter, backend, VrPanelId::QuickWheel),
    m_list(VrQuickWheelImageLayout::ImageSize, VrQuickWheelImageLayout::ImageSize),
    m_images(images) { }


  VrQuickWheelEvent VrQuickWheel::update(const VrQuickWheelInput& input) {
    m_drawsWeapon = false;

    bool pressed = input.held && !m_held;
    m_held = input.held;

    if (m_failed)
      return VrQuickWheelEvent::None;

    if (!m_open) {
      if (!pressed || !input.head || !input.hand)
        return VrQuickWheelEvent::None;

      if (!VrGameHotKeys::available()) {
        m_failed = true;
        return VrQuickWheelEvent::None;
      }

      open(input);
      return VrQuickWheelEvent::Opened;
    }

    // Tracking lost
    if (!input.head || !input.hand) {
      close();
      return VrQuickWheelEvent::Closed;
    }

    VrVector3 local = vrTransformPoint(vrInverse(m_pose), input.hand->position);
    int32_t picked = vrPickQuickWheelCell({ local.x, local.y }, m_view.picked, Picking);

    VrQuickWheelEvent event = VrQuickWheelEvent::None;

    if (picked != m_view.picked) {
      m_view.picked = picked;
      m_drawn = false;

      if (picked >= 0)
        event = VrQuickWheelEvent::Picked;
    }

    if (!input.held) {
      VrQuickWheelEvent chosen = choose(m_view.picked);
      close();
      return chosen;
    }

    show();
    return event;
  }


  void VrQuickWheel::close() {
    m_open = false;
    m_panel.hide();
  }


  void VrQuickWheel::open(const VrQuickWheelInput& input) {
    VrGameHotKeys::read(m_items);
    fillView(input);

    // Where the hand is, facing the head
    const VrVector3& centre = input.hand->position;
    m_pose = vrComputePanelPoseTowards(*input.head, centre, vrLength(centre - input.head->position), MaxPitch);
    m_pose.position = centre;

    m_view.picked = -1;
    m_drawn = false;
    m_open  = true;

    show();

    Logger::info("VR: The quick wheel opens");
  }


  void VrQuickWheel::fillView(const VrQuickWheelInput& input) {
    m_view = VrQuickWheelView();
    m_view.colour = VrGameImages::hudColour();
    m_swapsAmmo   = false;

    for (uint32_t cell = 0; cell < VrQuickWheelCellCount; cell++) {
      uint32_t slot = vrQuickWheelSlot(cell);
      VrQuickWheelCellView& view = m_view.cells[cell];

      if (slot == VrGame::HotKeyAmmoSwapSlot) {
        // The ammunition in the weapon, which the cell swaps when the
        // magazine does not
        const uint8_t* current = VrGameAmmo::available() ? VrGameAmmo::current() : nullptr;

        if (current) {
          std::vector<VrAmmoType> types = VrGameAmmo::types();
          uint32_t others = 0;

          for (const VrAmmoType& type : types) {
            if (type.form != current && type.held > 0)
              others += 1;
          }

          const uint8_t* entry[3] = { nullptr, nullptr, current };
          VrHotKeyItem ammo = VrGameHotKeys::describe(reinterpret_cast<uint8_t*>(entry));

          m_swapsAmmo = !input.manualReload && others > 0;

          view.filled = true;
          view.usable = m_swapsAmmo;
          view.name   = ammo.name;
          view.icon   = m_images.picture(ammo.icon);

          for (const VrAmmoType& type : types) {
            if (type.form == current)
              view.count = uint32_t(std::max(type.held, 0));
          }
        } else {
          view.usable = false;
        }

        continue;
      }

      const VrHotKeyItem& item = m_items[slot];
      view.filled = item.present;
      view.worn   = item.present && item.worn;
      view.usable = item.present;
      view.name   = item.name;
      view.count  = item.count;
      view.icon   = item.present ? m_images.picture(item.icon) : nullptr;

      // The slot's key, as the keyboard numbers them
      if (!view.icon)
        view.mark = item.present ? item.name : std::to_string(slot + 1);
    }
  }


  VrQuickWheelEvent VrQuickWheel::choose(int32_t cell) {
    if (cell < 0) {
      Logger::info("VR: The quick wheel closes with nothing chosen");
      return VrQuickWheelEvent::Closed;
    }

    uint32_t slot = vrQuickWheelSlot(uint32_t(cell));

    if (slot == VrGame::HotKeyAmmoSwapSlot) {
      if (!m_swapsAmmo)
        return VrQuickWheelEvent::Failed;

      VrGameAmmo::swapToNext();
      Logger::info("VR: The quick wheel swaps the ammunition");
      return VrQuickWheelEvent::Used;
    }

    const VrHotKeyItem& item = m_items[slot];

    if (!item.present)
      return VrQuickWheelEvent::Failed;

    bool weapon = item.formType == VrGame::FormTypeWeapon;

    // The weapon in the hand stays equipped: choosing it draws it
    if (weapon && item.worn) {
      m_drawsWeapon = true;
      Logger::info(str::format("VR: '", item.name, "' is chosen on the quick wheel, it is already equipped"));
      return VrQuickWheelEvent::Used;
    }

    if (!weapon && m_consume && m_consume(slot, item)) {
      m_drawsWeapon = false;
      Logger::info(str::format("VR: '", item.name, "' is chosen on the quick wheel, to be consumed by hand"));
      return VrQuickWheelEvent::Used;
    }

    if (!VrGameHotKeys::use(slot)) {
      Logger::info(str::format("VR: '", item.name, "' cannot be used now"));
      return VrQuickWheelEvent::Failed;
    }

    m_drawsWeapon = weapon;
    Logger::info(str::format("VR: '", item.name, "' is chosen on the quick wheel"));
    return VrQuickWheelEvent::Used;
  }


  void VrQuickWheel::show() {
    if (!m_drawn) {
      vrPaintQuickWheel(m_list, m_view, m_images.font());

      if (!m_panel.paint(m_list)) {
        Logger::err("VR: Failed to draw the quick wheel, it is not shown");
        m_failed = true;
        close();
        return;
      }

      m_drawn = true;
    }

    m_panel.show(m_pose, VrQuickWheelImageLayout::Width);
  }

}
