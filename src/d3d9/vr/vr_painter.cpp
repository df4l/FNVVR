#include <algorithm>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_painter.h"

namespace dxvk {

  namespace {

    constexpr DWORD VertexFormat     = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
    constexpr DWORD RenderTargetCount = 4;

    constexpr DWORD AllChannels = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN
                                | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA;
    constexpr DWORD ColourChannels = AllChannels & ~D3DCOLORWRITEENABLE_ALPHA;

    // The texel of an image as the GPU reads it, see VrPaintImageUse
    uint32_t textureTexel(uint32_t pixel, VrPaintImageUse use) {
      if (use == VrPaintImageUse::Picture || use == VrPaintImageUse::PictureWithMipmaps)
        return pixel;

      uint32_t coverage = pixel >> 24;

      if (use == VrPaintImageUse::Shape) {
        uint32_t bright = std::max({ (pixel >> 16) & 0xFF, (pixel >> 8) & 0xFF, pixel & 0xFF });
        coverage = (coverage * bright + 127) / 255;
      }

      // White, premultiplied by the coverage
      return coverage << 24 | coverage << 16 | coverage << 8 | coverage;
    }

  }


  bool VrPainter::paint(IDirect3DTexture9* target, const VrPaintList& list, bool clear) {
    if (!target)
      return false;

    Com<IDirect3DSurface9> surface;

    if (FAILED(target->GetSurfaceLevel(0, &surface)))
      return false;

    D3DSURFACE_DESC desc = { };
    surface->GetDesc(&desc);

    // The textures are made before the state is captured: making one
    // changes no state, but its first fill may fail
    std::vector<IDirect3DTexture9*> textures;
    textures.reserve(list.batches().size());

    for (const VrPaintBatch& batch : list.batches())
      textures.push_back(batch.image ? texture(*batch.image, batch.use) : nullptr);

    // Pre-transformed vertices, moved by half a pixel: pixel centres are
    // at whole coordinates in Direct3D 9
    m_vertices.resize(list.vertices().size());

    for (size_t i = 0; i < m_vertices.size(); i++) {
      const VrPaintVertex& vertex = list.vertices()[i];
      m_vertices[i] = { vertex.x - 0.5f, vertex.y - 0.5f, 0.0f, 1.0f, vertex.colour, vertex.u, vertex.v };
    }

    if (!captureState())
      return false;

    SavedTargets saved;
    saveTargets(saved, surface.ptr());

    setStates(desc.Width, desc.Height);

    if (clear)
      m_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);

    for (size_t i = 0; i < list.batches().size(); i++) {
      const VrPaintBatch& batch = list.batches()[i];

      if (batch.count < 3 || (batch.image && !textures[i]))
        continue;

      setBlend(batch.blend);

      DWORD op = textures[i] ? D3DTOP_MODULATE : D3DTOP_SELECTARG2;
      m_device->SetTexture(0, textures[i]);
      m_device->SetTextureStageState(0, D3DTSS_COLOROP, op);
      m_device->SetTextureStageState(0, D3DTSS_ALPHAOP, op);
      m_device->SetSamplerState(0, D3DSAMP_MIPFILTER,
        batch.use == VrPaintImageUse::Shape ? D3DTEXF_LINEAR : D3DTEXF_NONE);

      m_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, batch.count / 3, &m_vertices[batch.first], sizeof(DeviceVertex));
    }

    restoreTargets(saved);
    return true;
  }


  bool VrPainter::drawLens(const LensDrawing& lens) {
    D3DSURFACE_DESC desc = { };

    if (FAILED(lens.target->GetDesc(&desc)))
      return false;

    // Before the state is captured, as in paint
    IDirect3DTexture9* overlayTexture = lens.image && lens.overlay
      ? texture(*lens.overlay, VrPaintImageUse::PictureWithMipmaps)
      : nullptr;

    if (!captureState())
      return false;

    SavedTargets saved;
    saveTargets(saved, lens.target);

    setStates(desc.Width, desc.Height);
    setBlend(VrPaintBlend::Copy);

    m_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);

    // Tested against the world and the first-person model, as they were drawn
    m_device->SetDepthStencilSurface(lens.depth);
    m_device->SetRenderState(D3DRS_ZENABLE,             D3DZB_TRUE);
    m_device->SetRenderState(D3DRS_ZFUNC,               D3DCMP_LESSEQUAL);
    m_device->SetRenderState(D3DRS_DEPTHBIAS,           0);
    m_device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0);

    // Added to the textures' result by the device
    m_device->SetRenderState(D3DRS_SPECULARENABLE, TRUE);

    D3DMATRIX identity = { };
    identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;

    m_device->SetTransform(D3DTS_WORLD, &identity);
    m_device->SetTransform(D3DTS_VIEW, &identity);
    m_device->SetTransform(D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(lens.projection));

    m_device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1);
    m_device->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    m_device->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);

    DWORD stage = 0;

    // The image, or a dark glass
    m_device->SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(255, 0, 0, 0));
    m_device->SetTextureStageState(0, D3DTSS_RESULTARG, D3DTA_CURRENT);
    m_device->SetTextureStageState(1, D3DTSS_RESULTARG, D3DTA_CURRENT);
    m_device->SetTextureStageState(2, D3DTSS_RESULTARG, D3DTA_CURRENT);
    m_device->SetTexture(stage, lens.image);
    m_device->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    m_device->SetTextureStageState(stage, D3DTSS_COLORARG1, lens.image ? D3DTA_TEXTURE : D3DTA_TFACTOR);
    m_device->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    m_device->SetTextureStageState(stage, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    m_device->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    m_device->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER);
    m_device->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_BORDER);
    m_device->SetSamplerState(stage, D3DSAMP_BORDERCOLOR, D3DCOLOR_ARGB(255, 0, 0, 0));
    stage += 1;

    if (overlayTexture) {
      m_device->SetTexture(stage, overlayTexture);
      m_device->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_BLENDTEXTUREALPHA);
      m_device->SetTextureStageState(stage, D3DTSS_COLORARG1, D3DTA_TEXTURE);
      m_device->SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_CURRENT);
      m_device->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
      m_device->SetTextureStageState(stage, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
      m_device->SetTextureStageState(stage, D3DTSS_TEXCOORDINDEX, 0);
      m_device->SetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);

      m_device->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
      m_device->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
      m_device->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
      m_device->SetSamplerState(stage, D3DSAMP_MAXMIPLEVEL, 0);
      m_device->SetSamplerState(stage, D3DSAMP_MIPMAPLODBIAS, 0);
      m_device->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, FALSE);
      m_device->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER);
      m_device->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_BORDER);
      m_device->SetSamplerState(stage, D3DSAMP_BORDERCOLOR, D3DCOLOR_ARGB(255, 0, 0, 0));
      stage += 1;
    }

    // What the glass lets through
    m_device->SetTexture(stage, nullptr);
    m_device->SetTextureStageState(stage, D3DTSS_COLOROP, lens.amplified ? D3DTOP_MODULATE4X : D3DTOP_MODULATE);
    m_device->SetTextureStageState(stage, D3DTSS_COLORARG1, D3DTA_CURRENT);
    m_device->SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    m_device->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    m_device->SetTextureStageState(stage, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
    m_device->SetTextureStageState(stage, D3DTSS_TEXCOORDINDEX, 0);
    m_device->SetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    stage += 1;

    m_device->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_DISABLE);
    m_device->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    m_device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, lens.vertexCount, lens.triangles,
      lens.indices, D3DFMT_INDEX16, lens.vertices, lens.vertexStride);

    restoreTargets(saved);
    return true;
  }


  bool VrPainter::composite(
          IDirect3DSurface9* target,
          IDirect3DTexture9* source,
          float              left,
          float              top,
          float              right,
          float              bottom) {
    D3DSURFACE_DESC desc = { };

    if (FAILED(target->GetDesc(&desc)) || !captureState())
      return false;

    SavedTargets saved;
    saveTargets(saved, target);

    setStates(desc.Width, desc.Height);
    setBlend(VrPaintBlend::OverKeepAlpha);

    m_device->SetTexture(0, source);
    m_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    m_device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

    // Pre-transformed, moved by half a pixel as in paint
    float x0 = left   * float(desc.Width)  - 0.5f;
    float x1 = right  * float(desc.Width)  - 0.5f;
    float y0 = top    * float(desc.Height) - 0.5f;
    float y1 = bottom * float(desc.Height) - 0.5f;

    D3DCOLOR white = D3DCOLOR_ARGB(255, 255, 255, 255);

    const DeviceVertex quad[4] = {
      { x0, y0, 0.0f, 1.0f, white, left,  top    },
      { x1, y0, 0.0f, 1.0f, white, right, top    },
      { x0, y1, 0.0f, 1.0f, white, left,  bottom },
      { x1, y1, 0.0f, 1.0f, white, right, bottom },
    };

    m_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(DeviceVertex));

    restoreTargets(saved);
    return true;
  }


  bool VrPainter::captureState() {
    if (m_state == nullptr && FAILED(m_device->CreateStateBlock(D3DSBT_ALL, &m_state))) {
      Logger::err("VR: Failed to create the state block of the panels' drawing");
      return false;
    }

    m_state->Capture();
    return true;
  }


  void VrPainter::saveTargets(SavedTargets& saved, IDirect3DSurface9* target) {
    // Render targets and the depth buffer are not part of a state block
    for (DWORD i = 0; i < RenderTargetCount; i++)
      m_device->GetRenderTarget(i, &saved.colour[i]);

    m_device->GetDepthStencilSurface(&saved.depth);

    m_device->SetRenderTarget(0, target);

    for (DWORD i = 1; i < RenderTargetCount; i++)
      m_device->SetRenderTarget(i, nullptr);

    m_device->SetDepthStencilSurface(nullptr);
  }


  void VrPainter::restoreTargets(SavedTargets& saved) {
    for (DWORD i = 0; i < RenderTargetCount; i++) {
      if (i == 0 && saved.colour[i] == nullptr)
        continue;

      m_device->SetRenderTarget(i, saved.colour[i].ptr());
    }

    m_device->SetDepthStencilSurface(saved.depth.ptr());

    // Last: setting the render target also set the viewport
    m_state->Apply();
  }


  IDirect3DTexture9* VrPainter::texture(const VrImage& image, VrPaintImageUse use) {
    TextureKey key = { image.pixels.data(), image.width, image.height, use };
    auto found = m_textures.find(key);

    if (found != m_textures.end())
      return found->second.ptr();

    Com<IDirect3DTexture9>& texture = m_textures[key];

    // Shapes are drawn smaller than their images: the GPU makes their
    // mipmaps. Glyphs packed in a font's texture would bleed into each
    // other in mipmaps.
    bool mipmaps = use == VrPaintImageUse::Shape || use == VrPaintImageUse::PictureWithMipmaps;
    D3DLOCKED_RECT locked = { };

    if (!image.width || !image.height
     || FAILED(m_device->CreateTexture(image.width, image.height, mipmaps ? 0 : 1,
          mipmaps ? D3DUSAGE_AUTOGENMIPMAP : 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr))
     || FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
      Logger::err(str::format("VR: Failed to make a ", image.width, "x", image.height,
        " texture for the panels, it is not drawn"));
      texture = nullptr;
      return nullptr;
    }

    for (uint32_t y = 0; y < image.height; y++) {
      auto row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(locked.pBits) + y * locked.Pitch);

      for (uint32_t x = 0; x < image.width; x++)
        row[x] = textureTexel(image.at(x, y), use);
    }

    texture->UnlockRect(0);
    return texture.ptr();
  }


  void VrPainter::setStates(UINT width, UINT height) {
    D3DVIEWPORT9 viewport = { 0, 0, width, height, 0.0f, 1.0f };
    m_device->SetViewport(&viewport);
    m_device->SetVertexShader(nullptr);
    m_device->SetPixelShader(nullptr);
    m_device->SetFVF(VertexFormat);

    m_device->SetRenderState(D3DRS_ZENABLE,            D3DZB_FALSE);
    m_device->SetRenderState(D3DRS_ZWRITEENABLE,       FALSE);
    m_device->SetRenderState(D3DRS_STENCILENABLE,      FALSE);
    m_device->SetRenderState(D3DRS_ALPHATESTENABLE,    FALSE);
    m_device->SetRenderState(D3DRS_SCISSORTESTENABLE,  FALSE);
    m_device->SetRenderState(D3DRS_FOGENABLE,          FALSE);
    m_device->SetRenderState(D3DRS_LIGHTING,           FALSE);
    m_device->SetRenderState(D3DRS_SPECULARENABLE,     FALSE);
    m_device->SetRenderState(D3DRS_CULLMODE,           D3DCULL_NONE);
    m_device->SetRenderState(D3DRS_FILLMODE,           D3DFILL_SOLID);
    m_device->SetRenderState(D3DRS_SHADEMODE,          D3DSHADE_GOURAUD);
    m_device->SetRenderState(D3DRS_CLIPPLANEENABLE,    0);
    m_device->SetRenderState(D3DRS_SRGBWRITEENABLE,    FALSE);
    m_device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE);
    m_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    m_device->SetRenderState(D3DRS_BLENDOP,            D3DBLENDOP_ADD);
    m_device->SetRenderState(D3DRS_COLORWRITEENABLE,   AllChannels);

    m_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    m_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    m_device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    m_device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    m_device->SetTextureStageState(1, D3DTSS_COLOROP,   D3DTOP_DISABLE);
    m_device->SetTextureStageState(1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

    m_device->SetSamplerState(0, D3DSAMP_ADDRESSU,      D3DTADDRESS_CLAMP);
    m_device->SetSamplerState(0, D3DSAMP_ADDRESSV,      D3DTADDRESS_CLAMP);
    m_device->SetSamplerState(0, D3DSAMP_MINFILTER,     D3DTEXF_LINEAR);
    m_device->SetSamplerState(0, D3DSAMP_MAGFILTER,     D3DTEXF_LINEAR);
    m_device->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL,   0);
    m_device->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, 0);
    m_device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE,   FALSE);
  }


  void VrPainter::setBlend(VrPaintBlend blend) {
    DWORD source = D3DBLEND_ONE;
    DWORD dest   = D3DBLEND_INVSRCALPHA;
    DWORD write  = ColourChannels;

    switch (blend) {
      case VrPaintBlend::Over:
        write = AllChannels;
        break;

      case VrPaintBlend::OverKeepAlpha:
        break;

      case VrPaintBlend::Copy:
        source = D3DBLEND_ONE;
        dest   = D3DBLEND_ZERO;
        write  = AllChannels;
        break;

      case VrPaintBlend::Multiply:
        source = D3DBLEND_ZERO;
        dest   = D3DBLEND_SRCCOLOR;
        break;

      case VrPaintBlend::Brighten:
        source = D3DBLEND_DESTCOLOR;
        dest   = D3DBLEND_ONE;
        break;

      case VrPaintBlend::Add:
        source = D3DBLEND_ONE;
        dest   = D3DBLEND_ONE;
        break;
    }

    m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_device->SetRenderState(D3DRS_SRCBLEND,         source);
    m_device->SetRenderState(D3DRS_DESTBLEND,        dest);
    m_device->SetRenderState(D3DRS_COLORWRITEENABLE, write);
  }

}
