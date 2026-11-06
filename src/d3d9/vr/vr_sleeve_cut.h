#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "vr_forearm_cut.h"

namespace dxvk {

  /**
   * \brief Draws the first-person sleeves cut across the forearms
   *
   * Some outfits have one sleeve mesh from the shoulder to the wrist, or one
   * mesh for both arms. The VR layer moves the forearms to the controllers,
   * so the rest of the arm cannot be drawn. A sleeve is drawn once for each
   * arm it covers: its bones outside that forearm are gathered at the
   * elbow, and a clip plane across the forearm cuts away everything on the
   * elbow's side, those gathered vertices included. The cut is a straight
   * edge, whatever the mesh, and no other mesh is touched.
   *
   * The sleeves are given again by each placement of the first-person
   * model, and nothing is cut while there are none.
   */
  class VrSleeveCut {

  public:

    ~VrSleeveCut();

    /**
     * \brief Takes the game's skinned draw calls
     *
     * \returns \c nullptr if the calls were not found or are already taken
     */
    static std::unique_ptr<VrSleeveCut> install();

    /**
     * \brief Forgets the sleeves of the previous placement
     */
    void clear();

    /**
     * \brief Sets the bones of one arm and where it is cut
     *
     * \param [in] arm 0 for the left arm, 1 for the right arm
     * \param [in] forearm The forearm bone, at the elbow
     * \param [in] hand The hand bone, at the wrist
     * \param [in] distance Distance of the cut from the wrist, in game units
     */
    void setArm(
            uint32_t              arm,
            uint8_t*              forearm,
            uint8_t*              hand,
            float                 distance);

    /**
     * \brief Draws a mesh cut across the given arms
     *
     * \param [in] geometry The sleeve's skinned geometry
     * \param [in] arms Bit 0 for the left arm, bit 1 for the right arm
     */
    void addSleeve(
            uint8_t*              geometry,
            uint32_t              arms);

  private:

    struct Arm {
      uint8_t* forearm  = nullptr;
      uint8_t* hand     = nullptr;
      float    distance = 0.0f;
    };

    struct Sleeve {
      uint8_t* geometry = nullptr;
      uint32_t arms     = 0;
    };

    struct SavedBone {
      uint8_t*  bone = nullptr;
      VrVector3 translate;
      float     scale = 1.0f;
    };

    Arm                    m_arms[2];
    std::vector<Sleeve>    m_sleeves;
    std::vector<SavedBone> m_saved;

    /** The cut of the arm being drawn, invalid outside a sleeve's draw */
    VrForearmCut           m_cut;

    bool                   m_loggedNoPlane  = false;

    VrSleeveCut() = default;

    void drawSleeve(
            uint8_t*              pass,
      const Sleeve&               sleeve,
            int                   testAlpha,
            int                   blendAlpha,
            int                   setupDrawMode);

    bool gatherBones(
            uint8_t*              skin,
      const Arm&                  arm,
      const VrVector3&            gather);

    void restoreBones();

    void drawCutPartition(
            uint8_t*              renderer,
            void*                 buffer,
            void*                 partition,
            void*                 data);

    static void __cdecl renderSkinnedHook(
            uint8_t*              pass,
            int                   testAlpha,
            int                   blendAlpha,
            int                   setupDrawMode);

    static void __fastcall drawPartitionHook(
            uint8_t*              renderer,
            void*                 unused,
            void*                 buffer,
            void*                 partition,
            void*                 data);

  };

}
