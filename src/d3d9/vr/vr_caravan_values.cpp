#include <string>

#include "../../util/log/log.h"

#include "vr_caravan_values.h"

namespace dxvk {

  namespace {

    const VrColour Plate = { 0.0f, 0.0f, 0.0f, 0.6f };
    const VrColour Bust  = { 0.9f, 0.15f, 0.1f, 1.0f };

    // Size of a number's plate, in pixels
    constexpr float PlateWidth  = 56.0f;
    constexpr float PlateHeight = 33.0f;

    bool sameSpot(const VrVector2& a, const VrVector2& b) {
      return a.x == b.x && a.y == b.y;
    }

    bool sameBoard(const VrCaravanBoard& a, const VrCaravanBoard& b) {
      for (uint32_t i = 0; i < VrCaravanBoard::Tracks; i++) {
        if (a.values[i] != b.values[i] || !sameSpot(a.spots[i], b.spots[i]))
          return false;
      }

      for (uint32_t i = 0; i < VrCaravanBoard::Piles; i++) {
        if (a.piles[i] != b.piles[i] || !sameSpot(a.pileSpots[i], b.pileSpots[i]))
          return false;
      }

      return true;
    }

    void paintNumber(VrPaintList& list, const VrPaintFont& font, const VrVector2& spot,
        int32_t number, const VrColour& colour) {
      float cx = spot.x * float(list.width());
      float cy = spot.y * float(list.height());
      float hw = 0.5f * PlateWidth;
      float hh = 0.5f * PlateHeight;

      list.fillRoundedRect(cx - hw, cy - hh, cx + hw, cy + hh, 0.4f * hh, Plate);
      list.drawTextBlock(font, std::to_string(number), cx, cy, 2.0f * hw, 1.0f, 0.5f, 1, colour);
    }

  }


  void vrPaintCaravanValues(VrPaintList& list, const VrCaravanBoard& board,
      const VrColour& colour, const VrPaintFont* font) {
    list.clear();

    if (!font)
      return;

    for (uint32_t i = 0; i < VrCaravanBoard::Tracks; i++) {
      if (board.values[i] >= 0) {
        paintNumber(list, *font, board.spots[i], board.values[i],
          board.values[i] > VrCaravanBoard::MostValue ? Bust : colour);
      }
    }

    for (uint32_t i = 0; i < VrCaravanBoard::Piles; i++)
      paintNumber(list, *font, board.pileSpots[i], board.piles[i], colour);
  }


  VrCaravanValues::VrCaravanValues(VrPainter& painter, IVRBackend& backend, VrGameImages& images)
  : m_panel(painter, backend, VrPanelId::CaravanValues),
    m_list(VrCaravanBoard::ImageWidth, VrCaravanBoard::ImageHeight),
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
        Logger::err("VR: Failed to draw the values of Caravan's tracks and piles, the game shows them");
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
