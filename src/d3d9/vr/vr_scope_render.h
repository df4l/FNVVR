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
   * The lens is drawn into a target of its own right after the first-person
   * model, while the game's depth buffer holds the world and the model, so
   * that the hand or the scope's own body hide it. That target is laid over
   * the eye's finished picture: the scope's image has been through the game's
   * screen effects already.
   *
   * Only used on the game's main thread.
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
     * \param [in] apparentTangent Tangent of the edge of the scope's image
     * \param [out] fov Field of view to draw the game's whole image with
     * \returns \c false if the size of the game's image is not known
     */
    bool prepareView(float magnification, float apparentTangent, VrFov& fov);

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
     * \brief Forgets the image, the lens shows a dark glass until the next render
     */
    void forgetImage() {
      m_imageReady = false;
    }

    /**
     * \brief Draws the lens for an eye, tested against the current depth buffer
     *
     * To be called right after the first-person model was drawn.
     *
     * \param [in] lens The scope
     * \param [in] eye The eye's pose in the game world
     * \param [in] eyeFov The eye's field of view
     * \param [in] nearDistance The camera's near distance, if the device has no projection
     * \param [in] farDistance The camera's far distance, if the device has no projection
     */
    void drawLens(
      const VrScopeLens&        lens,
      const VrGameCameraPose&   eye,
      const VrFov&              eyeFov,
            float               nearDistance,
            float               farDistance);

    /**
     * \brief Lays the lens drawn by drawLens over the eye's picture
     *
     * \param [in] target The eye's picture
     */
    void presentLens(IDirect3DSurface9* target);

  private:

    VrScopeRender(IDirect3DDevice9* device, uint32_t resolution);

    IDirect3DDevice9*      m_device;
    VrPainter              m_painter;
    uint32_t               m_resolution;
    Com<IDirect3DTexture9> m_image;
    Com<IDirect3DTexture9> m_savedAdaptation;
    /// The lens as seen by an eye, and the surface it is drawn on: a
    /// multisampled one if the game's depth buffer is
    Com<IDirect3DTexture9> m_lensImage;
    Com<IDirect3DSurface9> m_lensSurface;
    D3DMULTISAMPLE_TYPE    m_lensSamples  = D3DMULTISAMPLE_NONE;
    VrScopeLensBounds      m_lensBounds;
    bool                   m_lensDrawn    = false;
    bool                   m_loggedLensDepth = false;
    bool                   m_loggedLensProjection = false;
    bool                   m_loggedLensTarget = false;
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

    bool prepareLensTarget(const D3DSURFACE_DESC& depth);

    bool saveAdaptation();

    void restoreAdaptation();

    static VrImage readReticle(const std::string& model);

  };

}
