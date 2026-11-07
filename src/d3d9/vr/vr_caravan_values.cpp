#include <string>

#include "../../util/log/log.h"

#include "vr_caravan_values.h"

namespace dxvk {

  namespace {

    const VrColour Plate = { 0.0f, 0.0f, 0.0f, 0.6f };
    const VrColour Bust  = { 0.9f, 0.15f, 0.1f, 1.0f };

    // Size of a value's plate, in parts of the image
    constexpr float PlateWidth  = 0.11f;
    constexpr float PlateHeight = 0.065f;

    bool sameBoard(const VrCaravanBoard& a, const VrCaravanBoard& b) {
      for (uint32_t i = 0; i < VrCaravanBoard::Tracks; i++) {
        if (a.values[i] != b.values[i]
         || a.spots[i].x != b.spots[i].x
         || a.spots[i].y != b.spots[i].y)
          return false;
      }

      return true;
    }

  }


  void vrPaintCaravanValues(VrPaintList& list, const VrCaravanBoard& board,
      const VrColour& colour, const VrPaintFont* font) {
    float width  = float(list.width());
    float height = float(list.height());

    list.clear();

    if (!font)
      return;

    for (uint32_t i = 0; i < VrCaravanBoard::Tracks; i++) {
      if (board.values[i] < 0)
        continue;

      float cx = board.spots[i].x * width;
      float cy = board.spots[i].y * height;
      float hw = 0.5f * PlateWidth * width;
      float hh = 0.5f * PlateHeight * height;

      list.fillRoundedRect(cx - hw, cy - hh, cx + hw, cy + hh, 0.4f * hh, Plate);
      list.drawTextBlock(*font, std::to_string(board.values[i]), cx, cy, 2.0f * hw, 1.0f, 0.5f, 1,
        board.values[i] > VrCaravanBoard::MostValue ? Bust : colour);
    }
  }


  VrCaravanValues::VrCaravanValues(VrPainter& painter, IVRBackend& backend, VrGameImages& images)
  : m_panel(painter, backend, VrPanelId::CaravanValues),
    m_list(VrCaravanBoard::ImageSize, VrCaravanBoard::ImageSize),
    m_images(images) { }


  bool VrCaravanValues::show(const VrCaravanBoard& board) {
    // Without a font the game's tiles keep the values
    if (m_failed || !m_images.font()) {
      hide();
      return false;
    }

    if (!m_drawn || !sameBoard(board, m_drawnBoard)) {
      vrPaintCaravanValues(m_list, board, VrGameImages::hudColour(), m_images.font());

      if (!m_panel.paint(m_list)) {
        Logger::err("VR: Failed to draw the values of Caravan's tracks, the game shows them");
        m_failed = true;
        hide();
        return false;
      }

      m_drawnBoard = board;
      m_drawn      = true;
    }

    return m_panel.show(board.placement.pose, board.placement.width);
  }


  void VrCaravanValues::hide() {
    m_panel.hide();
  }

}
