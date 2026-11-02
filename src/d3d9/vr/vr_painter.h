#pragma once

#include <map>
#include <tuple>
#include <vector>

#include "../d3d9_include.h"

#include "../../util/com/com_pointer.h"

#include "vr_paint.h"

namespace dxvk {

  /**
   * \brief Draws paint lists with the game's device
   *
   * Every image the mod makes for its panels and textures is drawn by the
   * GPU: a VrPaintList is drawn into a render target texture with the
   * fixed-function pipeline, between a capture and a restore of the
   * device's state, so that the game's own drawing is not disturbed. The
   * images a list draws are made into textures the first time they are
   * drawn and kept.
   *
   * Only used on the game's main thread.
   */
  class VrPainter {

  public:

    explicit VrPainter(IDirect3DDevice9* device)
    : m_device(device) { }

    IDirect3DDevice9* device() const {
      return m_device;
    }

    /**
     * \brief Draws a paint list into the top level of a texture
     *
     * \param [in] target Render target texture, A8R8G8B8
     * \param [in] list What is drawn, in pixels of the target
     * \param [in] clear Whether the target is made transparent first
     * \returns \c false if nothing could be drawn
     */
    bool paint(IDirect3DTexture9* target, const VrPaintList& list, bool clear);

    /**
     * \brief Draws a textured triangle fan with a projection
     *
     * Opaque and without a depth test. The image is black past its edges.
     * The overlay is laid over it by its own alpha, and is opaque black past
     * its edges.
     *
     * \param [in] target Render target surface
     * \param [in] image Texture
     * \param [in] overlay Image laid over it, or \c nullptr
     * \param [in] projection Projection of the positions, in row-vector order
     * \param [in] vertices The fan's vertices: position, then the texture
     *    coordinates of the texture and of the overlay
     * \param [in] count Number of vertices
     * \param [in] stride Size of a vertex
     * \returns \c false if nothing could be drawn
     */
    bool drawFan(
            IDirect3DSurface9* target,
            IDirect3DTexture9* image,
      const VrImage*           overlay,
      const float*             projection,
      const void*              vertices,
            UINT               count,
            UINT               stride);

  private:

    struct DeviceVertex {
      float    x, y, z, rhw;
      D3DCOLOR colour;
      float    u, v;
    };

    using TextureKey = std::tuple<const uint32_t*, uint32_t, uint32_t, VrPaintImageUse>;

    /// The device's targets while something is drawn
    struct SavedTargets {
      Com<IDirect3DSurface9> colour[4];
      Com<IDirect3DSurface9> depth;
    };

    IDirect3DDevice9*                          m_device;
    Com<IDirect3DStateBlock9>                  m_state;
    std::map<TextureKey, Com<IDirect3DTexture9>> m_textures;
    std::vector<DeviceVertex>                  m_vertices;

    IDirect3DTexture9* texture(const VrImage& image, VrPaintImageUse use);

    bool captureState();

    void saveTargets(SavedTargets& saved, IDirect3DSurface9* target);

    void restoreTargets(SavedTargets& saved);

    void setStates(UINT width, UINT height);

    void setBlend(VrPaintBlend blend);

  };

}
