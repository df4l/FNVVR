#pragma once

#include <map>
#include <memory>
#include <string>

#include "../d3d9_include.h"
#include "../../util/com/com_pointer.h"

#include "vr_painter.h"
#include "vr_scope_lens.h"

namespace dxvk {

  /**
   * \brief Draws the world into a texture of its own for a weapon's scope
   *
   * The game draws the world into a texture of its choice when it captures
   * the menu background, at the same point of its main loop as the frame
   * itself. The scope uses the same call from the world camera as it is
   * placed, without the first-person model, HUD or present, see
   * findings/optics.md §7.
   *
   * That call draws at the game's full size, which the screen effects rely
   * on. The view is therefore wider than the scope's, so that the scope's
   * part falls on a small centred square, and only that square is drawn
   * and kept.
   *
   * Only used on the game's main thread, between two frames.
   */
  class VrScopeRender {

  public:

    /**
     * \brief Checks the game's functions and creates the render
     *
     * \param [in] device The game's device
     * \param [in] resolution Side in pixels of the image
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrScopeRender> install(IDirect3DDevice9* device, uint32_t resolution);

    /**
     * \brief The equipped weapon's field of view while aiming
     *
     * \returns The field of view in degrees, or 0 without a weapon or if it
     *    does not zoom
     */
    static float weaponSightFov();

    /**
     * \brief Path of the equipped weapon's scope model
     *
     * \returns The path relative to Data\Meshes, or \c nullptr
     */
    static const char* weaponScopeModel();

    /**
     * \brief Selects the reticle of a scope model
     *
     * Reads the model and its reticle the first time it is seen.
     *
     * \param [in] model Path relative to Data\Meshes, or \c nullptr
     */
    void selectReticle(const char* model);

    /**
     * \brief Chooses the area of the game's image and the view that fits it
     *
     * \param [in] magnification How much the scope magnifies
     * \param [out] fov Field of view to draw the game's whole image with
     * \returns \c false if the size of the game's image is not known
     */
    bool prepareView(float magnification, VrFov& fov);

    /**
     * \brief The area chosen by prepareView, where the scene has to be drawn
     */
    const VrScopeArea& area() const {
      return m_area;
    }

    /**
     * \brief Culls the scene for the world camera and draws the image
     *
     * The caller places the world camera first and culls again for the eyes
     * after, see VrCullingCamera::cullAgain.
     *
     * \param [in] main The game's Main object
     * \returns \c true if the image holds this frame's view
     */
    bool render(void* main);

    /**
     * \brief Draws the lens with the image over an eye's picture
     *
     * \param [in] target The eye's picture
     * \param [in] lens The scope
     * \param [in] eye The eye's pose in the game world
     * \param [in] eyeFov The eye's field of view
     */
    void drawLens(
            IDirect3DSurface9*  target,
      const VrScopeLens&        lens,
      const VrGameCameraPose&   eye,
      const VrFov&              eyeFov);

  private:

    VrScopeRender(IDirect3DDevice9* device, uint32_t resolution);

    IDirect3DDevice9*      m_device;
    VrPainter              m_painter;
    uint32_t               m_resolution;
    Com<IDirect3DTexture9> m_image;
    Com<IDirect3DTexture9> m_savedAdaptation;
    VrScopeArea            m_area;
    uint32_t               m_targetWidth  = 0;
    uint32_t               m_targetHeight = 0;
    std::map<std::string, VrImage> m_reticles;
    const VrImage*         m_reticle      = nullptr;
    bool                   m_imageReady   = false;
    bool                   m_loggedSize   = false;
    bool                   m_loggedThreads = false;
    bool                   m_loggedTexture = false;
    uint32_t               m_windowFrames = 0;
    double                 m_windowCull   = 0.0;
    double                 m_windowDraw   = 0.0;

    bool copyImage(void* destination);

    bool saveAdaptation();

    void restoreAdaptation();

    static VrImage readReticle(const std::string& model);

  };

}
