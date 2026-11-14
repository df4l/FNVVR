#pragma once

#include <string>
#include <vector>

#include "vr_scope_table.h"
#include "vr_types.h"

namespace dxvk {

  /**
   * \brief A drawn part of a weapon model
   *
   * Its vertices in the frame of the weapon's model, which is the frame
   * of VrScopePlacement.
   */
  struct VrScopeShape {
    std::vector<VrVector3> points;
    /// The part's name holds "scope", in any case
    bool namedScope = false;
  };

  /**
   * \brief The barrel in the frame of the weapon's model
   *
   * Taken from the projectile node: the shots leave from the muzzle along
   * forward. The three axes are unit vectors, square to each other.
   */
  struct VrScopeBarrel {
    VrVector3 muzzle;
    VrVector3 forward;
    VrVector3 up;
    VrVector3 right;
  };

  /**
   * \brief What the search found, for the log
   */
  struct VrScopeFinding {
    /// Distance of the scope's axis above the barrel and to its right
    float above = 0.0f;
    float right = 0.0f;
    /// Rings of vertices in the tube that holds the eyepiece
    uint32_t rings = 0;
    /// The eyepiece's glass was found, how far in front of the lens
    bool  hasGlass = false;
    float glassDepth = 0.0f;
  };

  /**
   * \brief Finds the eyepiece of a scope in a weapon model's geometry
   *
   * For models of other mods, which the shipped table does not know
   * (findings/optics.md §16). A scope is modelled on a lathe: its vertices
   * lie on circles around its axis, in planes square to the barrel. Circles
   * sharing an axis above the barrel form a tube. The eyepiece is its rear
   * end: the lens is placed just inside the rim, or just in front of the
   * eyepiece's glass when that is near, and its radius is the narrowest
   * opening between the eye and the lens. A tube with something across the
   * line of sight behind it, or nothing inside, is no scope.
   *
   * Checked offline against the shipped table and against B42 Optics' hand
   * calibration of The Frontier's weapons (../tools/scope-survey/).
   *
   * \param [in] shapes The model's drawn parts
   * \param [in] barrel The barrel, in the same frame
   * \param [out] placement The scope's eyepiece, a disc
   * \param [out] finding What was found, for the log
   * \returns \c false if the model shows no scope that can be looked through
   */
  bool vrFindScope(
    const std::vector<VrScopeShape>& shapes,
    const VrScopeBarrel&             barrel,
          VrScopePlacement&          placement,
          VrScopeFinding&            finding);

  /**
   * \brief Whether a part's name marks it as part of a scope
   */
  bool vrIsScopeName(const char* name);

}
