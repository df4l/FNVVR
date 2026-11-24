#include <algorithm>

#include "vr_npc_limb.h"

namespace dxvk {

  void vrLimbLoosen(
          uint32_t              count,
    const uint16_t*             parents,
    const bool*                 dynamic,
          uint32_t              held,
          float*                weights) {
    auto loosen = [weights] (uint32_t body, float weight) {
      weights[body] = std::min(weights[body], weight);
    };

    if (dynamic[held])
      loosen(held, VrLimbFreeWeight);

    // What hangs from the limb
    for (uint32_t body = 0; body < count; body++) {
      if (!dynamic[body] || body == held)
        continue;

      for (uint32_t parent = parents[body]; parent < count; parent = parents[parent]) {
        if (parent == held) {
          loosen(body, VrLimbSoftWeight);
          break;
        }
      }
    }

    // What the limb hangs from, up to the bodies the animation places
    for (uint32_t body = parents[held]; body < count && dynamic[body]; body = parents[body]) {
      uint32_t children = 0;

      for (uint32_t child = 0; child < count; child++)
        children += dynamic[child] && parents[child] == body ? 1 : 0;

      loosen(body, children > 1 ? VrLimbSoftWeight : VrLimbFreeWeight);
    }
  }


  float vrLimbReturnWeight(float weight, float back) {
    return weight + (1.0f - weight) * back;
  }


  VrGrabSpringConstants vrLimbSpring(const VrGrabSpringConstants& spring) {
    VrGrabSpringConstants limb = spring;
    limb.damping    = 0.8f;
    limb.elasticity = 0.6f;
    return limb;
  }


  float vrLimbMaxAcceleration(float fullAcceleration, float branchMass, float limbMass) {
    if (!(limbMass > 0.0f))
      return fullAcceleration;

    return fullAcceleration * std::max(branchMass / limbMass, 1.0f);
  }

}
