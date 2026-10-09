#pragma once

#include <cstdint>
#include <vector>

namespace dxvk {

  /**
   * \brief Hides HUD element groups from one interface pass
   *
   * The game's HUD is a tree of tiles, one group per element (health and
   * compass, messages, objectives, ...), and the interface is drawn in a
   * single pass. Drawing one element on its own is done the way the game
   * hides menus from that pass: the app-culled flag of the other groups'
   * nodes is set for the pass and cleared afterwards. Groups that were
   * already culled are left alone. See findings/hud.md.
   *
   * Only used on the game's main thread, around the interface pass.
   */
  class VrHudLayers {

  public:

    /**
     * \brief Checks that the executable is the supported version
     * \returns \c false if the HUD layout is not the expected one
     */
    bool initialize();

    /**
     * \brief Culls every HUD group except the given ones
     *
     * \param [in] keep HUDMainMenu fields of the groups to keep visible
     * \param [in] count Number of entries in \c keep
     * \returns \c false if the HUD does not exist, nothing is culled then
     */
    bool isolate(const uintptr_t* keep, size_t count);

    /**
     * \brief Clears the flags set by the last isolate call
     */
    void restore();

  private:

    bool                    m_available = false;
    std::vector<uint32_t*>  m_culledFlags;

  };

}
