#pragma once

#include <memory>

#include "../d3d9_include.h"

#include "../../util/com/com_pointer.h"

#include "vr_render_scale_size.h"

namespace dxvk {

  class VrPainter;

  /**
   * \brief Draws the eyes' world at a lower resolution, switchable at once
   *
   * The game sizes the targets of its world render (scene, first person,
   * refraction, depth, blurs, image space) from its screen when it borrows
   * them from its texture manager, and the manager frees a size nobody
   * borrowed in the last frame. While an eye's world is drawn
   * (TESMain::DrawWorldStandard, from its main target on), the sizes taken
   * from the screen are scaled, so the next borrow gets the new size. The
   * image space manager's swap target, borrowed once at start, is replaced
   * for the eyes by one of the scaled size, and the last image space pass
   * draws into a scaled target that is then stretched (bilinear) into the
   * game's back buffer, where the eye copy takes it as before. The stretch
   * sharpens the image (contrast-adaptive, stronger at lower scales) to give
   * back part of what it softens.
   *
   * The scope's view, the Pip-Boy's world and the menus are drawn at the
   * full size. findings/performance.md §16.
   *
   * Only used on the game's main thread.
   */
  class VrRenderScale {

  public:

    ~VrRenderScale();

    VrRenderScale             (const VrRenderScale&) = delete;
    VrRenderScale& operator = (const VrRenderScale&) = delete;

    /**
     * \brief Redirects the game's calls
     * \returns \c nullptr if the executable is not the supported version
     */
    static std::unique_ptr<VrRenderScale> install(IDirect3DDevice9* device, VrPainter& painter);

    /**
     * \brief Sets the scale of the frame
     *
     * Called before the frame's eyes, so both have the same.
     *
     * \param [in] scale Share of each side, 1 for the game's own size
     */
    void setScale(float scale);

    /**
     * \brief Starts drawing an eye
     * \param [in] screen The game's screen size (its back buffer)
     */
    void beginEye(VrExtent screen);

    /**
     * \brief Ends the eye started by beginEye
     */
    void endEye();

    /**
     * \brief Size the eyes' world is drawn at this frame
     */
    VrExtent worldExtent(VrExtent screen) const {
      return vrScaledWorldExtent(screen, m_scale);
    }

    /**
     * \brief Gives the image space pass the target to draw into
     *
     * \param [in] destination The game's destination, \c 0 for its screen
     * \returns The destination to use
     */
    uintptr_t imageSpaceDestination(uintptr_t destination);

    /**
     * \brief Stretches the scaled image into the screen after the pass
     */
    void finishImageSpace();

  private:

    VrRenderScale(IDirect3DDevice9* device, VrPainter& painter);

    IDirect3DDevice9* m_device;
    VrPainter&        m_painter;

    // Stretches and sharpens, made at the first stretch
    Com<IDirect3DPixelShader9> m_sharpen;
    bool              m_sharpenTried  = false;

    float    m_scale        = 1.0f;
    VrExtent m_screen;
    VrExtent m_scaled;

    bool     m_inEye       = false;
    bool     m_inWorld     = false;
    bool     m_upscale     = false;

    // Targets of the scaled size this object borrowed, and the size they have
    void*    m_swapTarget  = nullptr;
    void*    m_finalTarget = nullptr;
    VrExtent m_targetSize;

    void*    m_gameSwapTarget = nullptr;
    bool     m_swapReplaced   = false;

    bool     m_loggedScale    = false;
    bool     m_loggedFailure  = false;

    bool stretch(IDirect3DTexture9* source, IDirect3DSurface9* screen);

    bool scaling() const {
      return m_inWorld && m_scaled.width != m_screen.width;
    }

    void beginWorld();

    void endWorld();

    void releaseTargets();

    static void __fastcall lookupHook(void* manager, void* unused, void* renderer, uint32_t type,
      uint32_t* width, uint32_t* height, uint32_t* format, uint32_t* entry, uint32_t* flags, uint32_t* samples);

    static void* __fastcall worldTargetHook(void* main, void* unused, void* renderer, void* texture,
      uint32_t disableImageSpace, uint32_t multisampled);

  };

}
