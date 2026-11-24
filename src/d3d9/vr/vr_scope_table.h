#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief How an item with a scope in the game's data is looked through
   */
  enum class VrOpticKind : uint8_t {
    /// One eyepiece on the weapon, an image at infinity
    Scope,
    /// Two eyepieces, brought to the eyes: the view is then held by the head
    Binoculars,
    /// A small picture tube on the back, showing the view from a lens
    Screen,
  };

  /**
   * \brief Where a weapon's scope is in the weapon's model
   *
   * In the model's units and frame, which is the weapon node's. The lens lies
   * square to the barrel, whichever axis of the model that is. The animations
   * move the binoculars' node within the model, so theirs are in that node's
   * frame: x along the view, y to the left, z up.
   */
  struct VrScopePlacement {
    VrOpticKind kind = VrOpticKind::Scope;
    /// Centre of the eyepiece's opening, or of the screen
    VrVector3 eyepiece;
    /// Centre of the second eyepiece of binoculars
    VrVector3 otherEyepiece;
    /// Where a view not taken along the shots is drawn from
    VrVector3 objective;
    /// Half the opening's size, across and up. A disc if they are equal.
    float     halfWidth   = 0.0f;
    float     halfHeight  = 0.0f;
    /// The opening is a rectangle
    bool      rectangular = false;

    /// A model the game's scope applies to, but which has nothing to look through
    bool hasLens() const {
      return halfWidth > 0.0f && halfHeight > 0.0f;
    }
  };

  /**
   * \brief The parts of the game's binoculars model
   *
   * The game's animations hold them by the right barrel, in the fist, so the
   * model stays where they put it (findings/optics.md §13).
   */
  struct VrBinocularsModel {
    /// The node holding the shape, its axes along the view, to the left and up
    static constexpr char NodeName[]   = "##Binoculars";
    /// A second shape, a marker at the model's origin that is no part of them
    static constexpr char MarkerName[] = "Binoculars:0";
  };

  /**
   * \brief The scope of each weapon model
   *
   * The game's data has no node for most scopes, so their place is listed by
   * model: measured from the rings of vertices of the game's models, and
   * checked against B42 Optics' hand calibration where it has one
   * (findings/optics.md §11). A text file can add or correct models.
   */
  class VrScopeTable {

  public:

    /**
     * \brief Reads corrections, which take precedence over the shipped values
     *
     * One line per model, the path as the game gives it, in any case:
     * \code
     * weapons\2handrifle\mine.nif = x y z radius
     * weapons\2handrifle\mine.nif = x y z halfWidth halfHeight
     * weapons\2handrifle\mine.nif = none
     * \endcode
     * Lines that are empty or start with a semicolon are ignored, as are
     * those that do not parse.
     *
     * \param [in] text The file's text
     * \returns How many models were read
     */
    size_t parse(const std::string& text);

    /**
     * \brief Finds the scope of a weapon model
     *
     * \param [in] model The model's path
     * \param [out] placement Where its scope is
     * \returns \c false if the model is not known
     */
    bool find(const char* model, VrScopePlacement& placement) const;

    /**
     * \brief A guess for a model that is not known, from where its barrel ends
     *
     * Scopes of the game's rifles sit about this far above the barrel, and
     * around the model's origin.
     *
     * \param [in] muzzle The barrel's end in the model
     */
    static VrScopePlacement guess(const VrVector3& muzzle);

  private:

    std::map<std::string, VrScopePlacement> m_corrections;

  };

}
