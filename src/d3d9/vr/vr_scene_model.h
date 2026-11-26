#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "vr_game_sequence.h"
#include "vr_game_space.h"

namespace dxvk {

  /**
   * \brief A copy of a model of the game's files, put in a scene
   *
   * A copy of the model, without its collision, is either put beside a
   * node of a menu's scene (as a child of the same parent) and lit by the
   * lights of the menu's shadow scene node, or attached below a node of
   * the world, as JIP's AttachModel attaches a model to a bone.
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

    /**
     * \brief Attaches the model below a node, at a place in its space
     *
     * The model is read the first time. It is updated with the node from
     * then on, as a part of the model the node belongs to.
     *
     * \param [in] parent The node, of the world's scene
     * \param [in] local The model's transform in the node's space
     * \returns \c true if the model is attached
     */
    bool attach(void* parent, const VrSequencePose& local);

    /**
     * \brief A node of the copy by its name, \c nullptr if it has none or
     *    it was not read
     */
    void* find(const char* name) const;

    /**
     * \brief The copy's root, \c nullptr before it was read
     */
    void* root() const {
      return m_root;
    }

    /**
     * \brief Whether the model is attached or shown
     */
    bool placed() const {
      return m_parent != nullptr;
    }

  private:

    explicit VrSceneModel(const char* path);

    bool load();

    std::string m_path;
    void*       m_root   = nullptr;
    void*       m_parent = nullptr;
    bool        m_failed = false;
    bool        m_inMenu = false;

  };

}
