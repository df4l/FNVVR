#include <algorithm>
#include <cmath>

#include "vr_hand_hud.h"
#include "vr_math.h"
#include "vr_pipboy_gaze.h"

namespace dxvk {

  namespace {

    // Shorter side of the interface, and the longer one of a 4:3 screen,
    // in interface units
    constexpr float InterfaceShortSide = 960.0f;
    constexpr float InterfaceLongSide  = 1280.0f;

    // Margin around the health bar's group, which the compass and the
    // bracket images slightly overflow, in interface units
    constexpr float WristMargin = 12.0f;

    // The weapon condition's label is right-aligned 80 units into the
    // ActionPoints group and moves 50 units left when an ammunition type is
    // shown (HUDMainMenu::ShiftConditionTileOver), so it overflows the
    // group's left side, in interface units
    constexpr float ActionPointsMarginLeft = 50.0f;

    // The prompt's first line (the action, such as "Take") is drawn above
    // its group and the item details below it, in interface units
    constexpr float PromptMarginAbove = 80.0f;
    constexpr float PromptMarginSide  = 40.0f;

    // Around what the prompt is measured to draw, for the glyphs' edges,
    // in interface units
    constexpr float PromptContentMargin = 6.0f;

    // Above what the corners are measured to draw, in interface units
    constexpr float CornerContentMargin = 2.0f;

    // A quarter turn, in radians
    constexpr float QuarterTurn = 1.5707963f;

    // Steepest slope of the line from the head to the prompt that the
    // prompt still faces, in radians (80 degrees)
    constexpr float MaxPromptPitch = 1.3962634f;

    VrInterfaceRect clampRect(const VrInterfaceRect& rect, const VrVector2& size) {
      return {
        std::clamp(rect.left,   0.0f, size.x),
        std::clamp(rect.top,    0.0f, size.y),
        std::clamp(rect.right,  0.0f, size.x),
        std::clamp(rect.bottom, 0.0f, size.y) };
    }

  }


  VrVector2 vrComputeInterfaceSize(uint32_t width, uint32_t height) {
    if (!width || !height)
      return { InterfaceLongSide, InterfaceShortSide };

    // Same tests as InterfaceManager::GetScreenWidth and GetScreenHeight,
    // which give a square screen 1280 by 960 units
    float aspect = float(width) / float(height);

    return {
      height >= width ? InterfaceLongSide : InterfaceShortSide * aspect,
      width >= height ? InterfaceShortSide : InterfaceLongSide / aspect };
  }


  VrInterfaceRect vrComputeTextRect(
    const VrVector2&            position,
    const VrVector2&            size,
          VrTextAlign           align) {
    float left = position.x;

    if (align == VrTextAlign::Centre)
      left -= 0.5f * size.x;
    else if (align == VrTextAlign::Right)
      left -= size.x;

    return { left, position.y, left + size.x, position.y + size.y };
  }


  VrInterfaceRect vrUniteRects(
    const VrInterfaceRect&      a,
    const VrInterfaceRect&      b) {
    if (a.isEmpty())
      return b;

    if (b.isEmpty())
      return a;

    return {
      std::min(a.left,   b.left),
      std::min(a.top,    b.top),
      std::max(a.right,  b.right),
      std::max(a.bottom, b.bottom) };
  }


  namespace {

    void cropMeasuredPrompt(
      const VrInterfaceRect&      info,
      const VrInterfaceRect&      measured,
      const VrInterfaceRect     (&corners)[2],
      const VrInterfaceRect*    (&cornersMeasured)[2],
      const VrVector2&            size,
            VrHandHudCrops&       crops) {
      VrInterfaceRect area = clampRect({
        measured.left   - PromptContentMargin,
        measured.top    - PromptContentMargin,
        measured.right  + PromptContentMargin,
        measured.bottom + PromptContentMargin }, size);

      float centre     = 0.5f * (info.left + info.right);
      float lowerLeft  = area.left;
      float lowerRight = area.right;
      float split      = area.bottom;

      for (uint32_t i = 0; i < 2; i++) {
        VrInterfaceRect zone = corners[i];

        // The rows where the corner actually draws, when it was measured
        if (cornersMeasured[i] && !cornersMeasured[i]->isEmpty())
          zone.top = cornersMeasured[i]->top - CornerContentMargin;

        if (zone.isEmpty() || zone.top >= area.bottom || zone.bottom <= area.top)
          continue;

        if (zone.right <= centre && zone.right > lowerLeft)
          lowerLeft = zone.right;
        else if (zone.left >= centre && zone.left < lowerRight)
          lowerRight = zone.left;
        else
          continue;

        split = std::min(split, std::max(zone.top, area.top));
      }

      crops.promptPartCount = 0;

      if (split > area.top)
        crops.promptParts[crops.promptPartCount++] = { area.left, area.top, area.right, split };

      VrInterfaceRect lower = { lowerLeft, split, lowerRight, area.bottom };

      if (!lower.isEmpty())
        crops.promptParts[crops.promptPartCount++] = lower;

      crops.prompt = VrInterfaceRect();

      for (uint32_t i = 0; i < crops.promptPartCount; i++)
        crops.prompt = vrUniteRects(crops.prompt, crops.promptParts[i]);
    }

  }


  VrHandHudCrops vrComputeHandHudCrops(
    const VrInterfaceRect&      hitPoints,
    const VrInterfaceRect&      actionPoints,
    const VrInterfaceRect&      info,
    const VrVector2&            size,
    const VrHandHudContent&     content) {
    VrHandHudCrops crops;

    crops.wrist = clampRect({
      hitPoints.left   - WristMargin,
      hitPoints.top    - WristMargin,
      hitPoints.right  + WristMargin,
      hitPoints.bottom + WristMargin }, size);

    crops.actionPoints = clampRect({
      actionPoints.left   - ActionPointsMarginLeft,
      actionPoints.top    - WristMargin,
      actionPoints.right  + WristMargin,
      actionPoints.bottom + WristMargin }, size);

    if (content.info && !content.info->isEmpty()) {
      const VrInterfaceRect  corners[2]         = { crops.wrist, crops.actionPoints };
      const VrInterfaceRect* cornersMeasured[2] = { content.hitPoints, content.actionPoints };
      cropMeasuredPrompt(info, *content.info, corners, cornersMeasured, size, crops);
      return crops;
    }

    float centre     = 0.5f * (info.left + info.right);
    float halfWidth  = 0.5f * info.width() + PromptMarginSide;
    float top        = std::max(info.top - PromptMarginAbove, 0.0f);
    float bottom     = size.y;

    // Narrowed on both sides, so that the prompt stays centred
    for (const VrInterfaceRect& corner : { crops.wrist, crops.actionPoints }) {
      if (corner.isEmpty() || top >= corner.bottom || bottom <= corner.top)
        continue;

      if (corner.right <= centre)
        halfWidth = std::min(halfWidth, centre - corner.right);
      else if (corner.left >= centre)
        halfWidth = std::min(halfWidth, corner.left - centre);
    }

    halfWidth = std::min({ halfWidth, centre, size.x - centre });

    crops.prompt = clampRect({ centre - halfWidth, top, centre + halfWidth, bottom }, size);
    crops.promptParts[0]  = crops.prompt;
    crops.promptPartCount = crops.prompt.isEmpty() ? 0 : 1;
    return crops;
  }


  VrPromptImage vrComputePromptImage(
    const VrInterfaceRect*      parts,
          uint32_t              count,
    const VrVector2&            minimum) {
    VrPromptImage image;
    VrInterfaceRect bounds;
    count = std::min(count, uint32_t(image.targets.size()));

    for (uint32_t i = 0; i < count; i++)
      bounds = vrUniteRects(bounds, parts[i]);

    image.size.x = std::max(bounds.width(),  minimum.x);
    image.size.y = std::max(bounds.height(), minimum.y);

    // Rounded down, so that parts given in whole pixels stay on whole pixels
    float left = std::floor(0.5f * (image.size.x - bounds.width()))  - bounds.left;
    float top  = std::floor(0.5f * (image.size.y - bounds.height())) - bounds.top;

    for (uint32_t i = 0; i < count; i++) {
      image.targets[i] = {
        parts[i].left  + left, parts[i].top    + top,
        parts[i].right + left, parts[i].bottom + top };
    }

    return image;
  }


  VrWristStack vrComputeWristStack(
    const VrInterfaceRect&      health,
    const VrInterfaceRect&      actionPoints) {
    VrWristStack stack;
    stack.size.x = std::max(health.width(), actionPoints.width());
    stack.size.y = health.height() + actionPoints.height();

    // Rounded down, so that parts given in whole pixels stay on whole pixels
    float healthLeft = std::floor(0.5f * (stack.size.x - health.width()));
    float actionLeft = std::floor(0.5f * (stack.size.x - actionPoints.width()));

    stack.health       = { healthLeft, 0.0f, healthLeft + health.width(), health.height() };
    stack.actionPoints = { actionLeft, health.height(), actionLeft + actionPoints.width(), stack.size.y };
    return stack;
  }


  VrPose vrComputeWristPanelPose(const VrControllerState& hand, float drop) {
    // A panel faces along +Z with its up axis on +Y. Turned by -90 degrees
    // around X, its up axis runs forward along the hand (-Z) and it faces
    // up (+Y). Turned around the hand's forward axis, it faces the hand's
    // right axis (+X) raised by the face-up angle.
    VrQuaternion alongHand = vrQuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, -QuarterTurn);
    VrQuaternion outwards  = vrQuaternionFromAxisAngle({ 0.0f, 0.0f, 1.0f },
      VrHandHudLayout::WristFaceUpAngle - QuarterTurn);

    VrPose pose;
    pose.orientation = vrNormalize(hand.aimPose.orientation * outwards * alongHand);
    pose.position    = vrComputeControllerPoint(hand, VrHandHudLayout::WristOffset)
                     - vrRotate(pose.orientation, { 0.0f, drop, 0.0f });
    return pose;
  }


  VrPose vrComputePromptPanelPose(const VrControllerState& hand, const VrPose& head) {
    VrVector3 centre = vrComputeControllerPoint(hand, VrHandHudLayout::FingertipOffset)
                     + VrVector3 { 0.0f, VrHandHudLayout::PromptLift, 0.0f };

    // Faces the head without rolling, as a panel placed on the line from
    // the head to the prompt does
    VrPose pose = vrComputePanelPoseTowards(head, centre,
      vrLength(centre - head.position), MaxPromptPitch);
    pose.position = centre;
    return pose;
  }


  bool VrWristGaze::update(const VrPose* head, const VrPose* panel, float dt) {
    bool looking = false;

    if (head && panel && vrIsLookingAt(*head, panel->position, MaxAngle, MaxDistance)) {
      VrVector3 normal = vrRotate(panel->orientation, { 0.0f, 0.0f, 1.0f });
      VrVector3 toHead = head->position - panel->position;
      looking = vrDot(normal, toHead) >= std::cos(MaxFacingAngle) * vrLength(toHead);
    }

    if (looking) {
      m_lookTime += dt;
      m_awayTime  = 0.0f;
    } else {
      m_lookTime  = 0.0f;
      m_awayTime += dt;
    }

    if (!m_visible && looking && m_lookTime >= ShowDelay)
      m_visible = true;

    if (m_visible && !looking && m_awayTime >= HideDelay)
      m_visible = false;

    return m_visible;
  }


  void VrWristGaze::reset() {
    m_visible  = false;
    m_lookTime = 0.0f;
    m_awayTime = 0.0f;
  }


  VrHand vrChooseActivateHand(
    const std::array<VrHandPickResult, VrHandCount>& picks,
          VrHand                  previous) {
    VrHand other = previous == VrHand::Right ? VrHand::Left : VrHand::Right;
    const VrHandPickResult& kept      = picks[uint32_t(previous)];
    const VrHandPickResult& candidate = picks[uint32_t(other)];

    if (!candidate.tracked)
      return previous;

    if (!kept.tracked)
      return other;

    if (kept.hit != candidate.hit)
      return candidate.hit ? other : previous;

    if (kept.hit && kept.fuzzy != candidate.fuzzy)
      return candidate.fuzzy ? previous : other;

    return previous;
  }


  bool VrPromptHand::update(
    const std::array<VrControllerState, VrHandCount>& controllers,
    const VrVector3&            target,
          VrHand&               hand) {
    const VrControllerState& current = controllers[uint32_t(m_hand)];
    VrHand other = m_hand == VrHand::Right ? VrHand::Left : VrHand::Right;
    const VrControllerState& alternative = controllers[uint32_t(other)];

    if (!current.isActive && !alternative.isActive)
      return false;

    if (!current.isActive) {
      m_hand = other;
    } else if (alternative.isActive) {
      float currentDistance     = vrLength(vrComputeControllerPoint(current,     VrHandHudLayout::FingertipOffset) - target);
      float alternativeDistance = vrLength(vrComputeControllerPoint(alternative, VrHandHudLayout::FingertipOffset) - target);

      if (alternativeDistance + SwitchMargin < currentDistance)
        m_hand = other;
    }

    hand = m_hand;
    return true;
  }

}
