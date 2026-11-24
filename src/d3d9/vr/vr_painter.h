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
     * \brief What VrPainter::drawLens draws
     *
     * The vertices hold a position, a colour multiplied with the image, a
     * colour added to it, and the texture coordinates of the image and the
     * overlay.
     */
    struct LensDrawing {
      /// Render target, the same size as the depth buffer
      IDirect3DSurface9*  target;
      /// Depth buffer to test against, not written to
      IDirect3DSurface9*  depth;
      /// Black past its edges, or \c nullptr for a dark glass
      IDirect3DTexture9*  image;
      /// The image has mipmaps, seen from afar or at an angle
      bool                imageMipmapped;
      /// Laid over the image by its own alpha, opaque black past its edges, or \c nullptr
      const VrImage*      overlay;
      /// In row-vector order
      const float*        projection;
      /// Passes where the lens is nearer than the depth buffer, or as near
      D3DCMPFUNC          depthTest;
      /// Drawn in turn, the later over the earlier
      const void* const*  meshes;
      /// Which of them are not tested against the depth buffer: an eye at an
      /// eyepiece looks through it, whatever the model does around the eye
      const bool*         meshOverDepth;
      UINT                meshCount;
      UINT                vertexCount;
      UINT                vertexStride;
      const uint16_t*     indices;
      UINT                triangles;
      /// The colours are multiplied by 4, to amplify the image
      bool                amplified;
    };

    /**
     * \brief Clears a target and draws a lens into it
     *
     * The target ends up with the lens, premultiplied, where the lens is not
     * hidden, and transparent elsewhere.
     *
     * \returns \c false if nothing could be drawn
     */
    bool drawLens(const LensDrawing& lens);

    /**
     * \brief Fills a target with a pixel shader
     *
     * \param [in] target Render target surface
     * \param [in] shader The pixel shader
     * \param [in] first Its sampler 0, bilinear, clamped
     * \param [in] second Its sampler 1, bilinear, clamped
     * \param [in] constants Its constant 0
     * \returns \c false if nothing could be drawn
     */
    bool shade(
            IDirect3DSurface9*     target,
            IDirect3DPixelShader9* shader,
            IDirect3DTexture9*     first,
            IDirect3DTexture9*     second,
      const float*                 constants);

    /**
     * \brief Lays a part of a texture over the same part of a target
     *
     * The texture is premultiplied. The target's alpha stays.
     *
     * \param [in] target Render target surface
     * \param [in] source Texture, covering the whole target at any size
     * \param [in] left Left side of the part, from 0 to 1
     * \param [in] top Top side of the part, from 0 to 1
     * \param [in] right Right side of the part, from 0 to 1
     * \param [in] bottom Bottom side of the part, from 0 to 1
     * \returns \c false if nothing could be drawn
     */
    bool composite(
            IDirect3DSurface9* target,
            IDirect3DTexture9* source,
            float              left,
            float              top,
            float              right,
            float              bottom);

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
