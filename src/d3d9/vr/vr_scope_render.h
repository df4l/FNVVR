#pragma once

#include <memory>

#include "../d3d9_include.h"
#include "../../util/com/com_pointer.h"

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
   * Only used on the game's main thread, between two frames.
   */
  class VrScopeRender {

  public:

    /**
     * \brief Checks the game's functions and creates the render
     *
     * \param [in] device The game's device
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrScopeRender> install(IDirect3DDevice9* device);

    /**
     * \brief The equipped weapon's field of view while aiming
     *
     * \returns The field of view in degrees, or 0 without a weapon or if it
     *    does not zoom
     */
    static float weaponSightFov();

    /**
     * \brief Width over height of the image
     */
    float aspect() const;

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
     * \brief Copies the image into a part of a surface
     *
     * \param [in] target Surface to draw on
     * \param [in] rect Part of the surface
     */
    void drawInto(IDirect3DSurface9* target, const RECT& rect);

  private:

    explicit VrScopeRender(IDirect3DDevice9* device);

    IDirect3DDevice9*      m_device;
    Com<IDirect3DTexture9> m_image;
    bool                   m_imageReady   = false;
    bool                   m_loggedSize   = false;
    bool                   m_loggedThreads = false;
    bool                   m_loggedTexture = false;
    uint32_t               m_windowFrames = 0;
    double                 m_windowCull   = 0.0;
    double                 m_windowDraw   = 0.0;

    bool copyImage(void* destination);

  };

}
