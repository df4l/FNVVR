#pragma once

#include <functional>
#include <memory>

#include "vr_game_camera.h"

namespace dxvk {

  /**
   * \brief Culls the scene for the head instead of the game's camera
   *
   * Before it draws a frame, the game culls groups of objects (multibounds),
   * the terrain and the rooms of interiors against its camera, once per
   * frame, see findings/render.md. The eyes are only drawn later, so
   * whatever the game's camera does not see stays missing in both eyes even
   * though the per-object culling uses each eye's camera. The game's camera
   * follows the head while the head drives the player's look, but not in
   * dialogues, menus or the Pip-Boy, and its field of view is narrower than
   * the headset's, more so while a dialogue zooms in.
   *
   * The two calls of that pass are redirected: the world camera is moved to
   * the pose and frustum given by a callback for the duration of each call,
   * then restored.
   *
   * Only used on the game's main thread.
   */
  class VrCullingCamera {

  public:

    /**
     * \brief Gives the view to cull with
     *
     * Called with the pose the game placed its camera at. Returns \c false
     * to keep the game's own pose and frustum.
     */
    using ViewFn = std::function<bool (VrGameCameraPose& pose, VrGameFrustum& frustum)>;

    ~VrCullingCamera();

    /**
     * \brief Redirects the game's calls
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrCullingCamera> install(ViewFn view);

    /**
     * \brief Culls for the head again, after another camera was culled
     *
     * Expects the world camera at the pose the game placed it at.
     */
    void cullAgain();

  private:

    explicit VrCullingCamera(ViewFn view);

    ViewFn        m_view;
    VrGameCamera  m_camera;
    bool          m_logged = false;

    static void __fastcall occlusionPlaneHook(
            void*                 sceneNode,
            void*                 unused,
            void*                 camera);

    static void __fastcall multiBoundHook(
            void*                 tes,
            void*                 unused,
            void*                 camera);

    /**
     * \brief Moves the camera to the culling view if there is one
     * \returns \c true if the camera was moved, its previous state is
     *    then in \c saved
     */
    bool begin(void* camera, VrGameCameraState& saved);

  };

}
