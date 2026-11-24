#pragma once

#include <cstdint>
#include <memory>

#include "vr_game_space.h"

namespace dxvk {

  /**
   * \brief A model of the game's files shown in a menu's 3D scene
   *
   * A copy of the model, without its collision, is put beside a node of the
   * menu's scene (as a child of the same parent) and lit by the lights of
   * the menu's shadow scene node. It stays where it is put: it is updated
   * once, when it is shown.
   */
  class VrSceneModel {

  public:

    /**
     * \brief Checks that the game's model functions are there
     *
     * \param [in] path Path of the model, from the Data folder
     * \returns The model, or null if the functions were not found
     */
    static std::unique_ptr<VrSceneModel> create(const char* path);

    ~VrSceneModel();

    VrSceneModel(const VrSceneModel&) = delete;
    VrSceneModel& operator = (const VrSceneModel&) = delete;

    /**
     * \brief Shows the model beside a node of a menu's scene
     *
     * The model is read the first time. Its place is given in the space of
     * \c sibling: the model's origin goes to \c translate, scaled by \c scale,
     * its axes along the sibling's.
     *
     * \param [in] sibling A node of the menu's scene, with a parent
     * \param [in] translate Where the model's origin goes, in the sibling's space
     * \param [in] scale Size of the model in the sibling's space
     * \returns \c true if the model is shown
     */
    bool show(void* sibling, const VrVector3& translate, float scale);

    /**
     * \brief Takes the model out of the scene, if it is in one
     */
    void hide();

  private:

    explicit VrSceneModel(const char* path);

    bool load();

    const char* m_path;
    void*       m_root   = nullptr;
    void*       m_parent = nullptr;
    bool        m_failed = false;

  };

}
