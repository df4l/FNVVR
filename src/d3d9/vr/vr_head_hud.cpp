#include <algorithm>
#include <cmath>

#include "vr_head_hud.h"

namespace dxvk {

  namespace {

    // Height of the XP meter's bracket image (glow_XP_bar_over.dds in
    // hud_main_menu.xml), in interface units
    constexpr float XpMeterHeight = 256.0f;

    // The level number left of the bar is right-aligned 70 units into the
    // group, so a long number reaches slightly past its left side
    constexpr float XpMeterMarginLeft = 12.0f;

    // Space kept between the messages or objectives and the meters, in
    // interface units
    constexpr float HudCornerGap = 10.0f;

  }


  VrInterfaceRect vrComputeRadiationMeterCrop(
    const VrInterfaceRect&      group,
    const VrVector2&            size) {
    return {
      std::clamp(group.left,                          0.0f, size.x),
      std::clamp(group.top,                           0.0f, size.y),
      size.x,
      std::clamp(group.top + VrRadiationMeterHeight,  0.0f, size.y) };
  }


  VrInterfaceRect vrComputeXpMeterCrop(
    const VrInterfaceRect&      group,
    const VrVector2&            size) {
    return {
      std::clamp(group.left - XpMeterMarginLeft, 0.0f, size.x),
      std::clamp(group.top,                      0.0f, size.y),
      size.x,
      std::clamp(group.top + XpMeterHeight,      0.0f, size.y) };
  }


  bool vrComputeHudMetersCrop(
    const VrInterfaceRect*      radiation,
    const VrInterfaceRect*      xp,
    const VrVector2&            size,
          VrInterfaceRect&      crop) {
    if (radiation && xp) {
      VrInterfaceRect a = vrComputeRadiationMeterCrop(*radiation, size);
      VrInterfaceRect b = vrComputeXpMeterCrop(*xp, size);

      crop = {
        std::min(a.left,   b.left),
        std::min(a.top,    b.top),
        std::max(a.right,  b.right),
        std::max(a.bottom, b.bottom) };
    } else if (radiation) {
      crop = vrComputeRadiationMeterCrop(*radiation, size);
    } else if (xp) {
      crop = vrComputeXpMeterCrop(*xp, size);
    } else {
      return false;
    }

    return true;
  }


  float vrComputeHudCornerRight(
    const VrInterfaceRect*      messages,
    const VrInterfaceRect*      objectives) {
    if (!messages && !objectives)
      return 0.0f;

    float right = 0.0f;

    if (messages)
      right = std::max(right, messages->right);

    if (objectives)
      right = std::max(right, objectives->right);

    return right + HudCornerGap;
  }


  float vrComputeHeadHudSplit(
          float                 height,
          float                 cornerBottom,
    const VrInterfaceRect*      sneak,
    const VrInterfaceRect*      subtitles) {
    float middle = std::floor(0.5f * height);
    float split  = std::max(middle, cornerBottom + HudCornerGap);

    // The sneak meter, just below the middle, moves whole or not at all
    if (split > middle && sneak && sneak->top < split && sneak->bottom > middle)
      split = std::max(split, sneak->bottom + HudCornerGap);

    if (subtitles)
      split = std::min(split, subtitles->top);

    return std::clamp(split, middle, height);
  }


  VrHeadHudLayout vrComputeHeadHudLayout(
    const VrVector2&            size,
          float                 split,
          float                 shift,
    const VrInterfaceRect*      meters,
          float                 cornerRight) {
    VrHeadHudLayout layout;

    split = std::clamp(std::floor(split), 0.0f, size.y);
    shift = std::clamp(shift, 0.0f, std::floor(0.5f * size.x));

    layout.copies[layout.count++] = {
      { 0.0f, split, size.x, size.y },
      { 0.0f, split, size.x, size.y } };

    // Room between the messages and objectives and the meters, shared by
    // both moves: the meters give theirs up first, then the corner, since
    // a copy replaces what it covers
    float room        = meters ? std::max(meters->left - cornerRight, 0.0f) : shift;
    float cornerShift = std::min(shift, room);
    float metersShift = std::min(shift, room - cornerShift);

    // The upper half without the meters' columns, so that the meters are
    // not copied twice
    float upperRight = size.x - cornerShift;

    if (meters)
      upperRight = std::min(upperRight, meters->left);

    layout.copies[layout.count++] = {
      { 0.0f,        0.0f, upperRight,               split },
      { cornerShift, 0.0f, upperRight + cornerShift, split } };

    if (meters) {
      VrInterfaceRect source = {
        meters->left, std::max(meters->top, 0.0f),
        std::min(meters->right, size.x), std::min(meters->bottom, split) };

      if (!source.isEmpty()) {
        layout.copies[layout.count++] = { source,
          { source.left - metersShift, source.top, source.right - metersShift, source.bottom } };
      }
    }

    return layout;
  }

}
