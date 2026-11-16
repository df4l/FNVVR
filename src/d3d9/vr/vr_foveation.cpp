#include <cmath>
#include <cstring>

#include "../d3d9_device.h"

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_foveation.h"

namespace dxvk {

  namespace {

    // Largest texel side tried: a smaller one follows the zones' circles
    // more closely, for an image four times larger
    constexpr uint32_t PreferredTexelSize = 16u;

    constexpr VkFormat RateFormat = VK_FORMAT_R8_UINT;

    constexpr VkImageLayout RateLayout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;

    bool sameFov(const VrFov& a, const VrFov& b) {
      constexpr float Tolerance = 1e-6f;

      return std::abs(a.angleLeft  - b.angleLeft)  <= Tolerance
          && std::abs(a.angleRight - b.angleRight) <= Tolerance
          && std::abs(a.angleUp    - b.angleUp)    <= Tolerance
          && std::abs(a.angleDown  - b.angleDown)  <= Tolerance;
    }

  }


  VrFoveation::VrFoveation(D3D9DeviceEx* device, uint32_t texelSize, const VrFoveationZones& zones)
  : m_device(device), m_texelSize(texelSize), m_zones(zones) {
    const Rc<DxvkDevice>& dxvk = m_device->GetDXVKDevice();

    if (dxvk->features().core.features.pipelineStatisticsQuery) {
      for (auto& query : m_invocations)
        query = dxvk->createGpuQuery(VK_QUERY_TYPE_PIPELINE_STATISTICS, 0u, 0u);
    }
  }


  VrFoveation::~VrFoveation() {
    endWorld();
  }


  std::unique_ptr<VrFoveation> VrFoveation::create(
          IDirect3DDevice9*   device,
    const VrFoveationZones&   zones) {
    auto* d3d9 = static_cast<D3D9DeviceEx*>(device);
    const Rc<DxvkDevice>& dxvk = d3d9->GetDXVKDevice();

    if (!dxvk->canUseShadingRateAttachment()) {
      Logger::info("VR: The GPU or its driver has no shading rate attachments, eye-tracked foveation is not available");
      return nullptr;
    }

    uint32_t texelSize = pickTexelSize(*dxvk);

    if (!texelSize || !(dxvk->getFormatFeatures(RateFormat).optimal
        & VK_FORMAT_FEATURE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR)) {
      Logger::info("VR: The GPU's shading rate attachments do not fit, eye-tracked foveation is not available");
      return nullptr;
    }

    Logger::info(str::format("VR: Eye-tracked foveation can be used: shading rate texels of ", texelSize, "x", texelSize,
      " pixels, every pixel shaded within ", zones.fullDegrees, " degrees of the gaze, one shade per 2x2 pixels within ",
      zones.halfDegrees, " degrees, per 4x4 beyond"));

    return std::unique_ptr<VrFoveation>(new VrFoveation(d3d9, texelSize, zones));
  }


  void VrFoveation::prepareFrame(
    const std::array<VrEyeView, VrEyeCount>&  views,
    const std::array<VrGaze, VrEyeCount>*     gaze,
          VrExtent                            target) {
    bool resized = target.width != m_target.width || target.height != m_target.height;
    bool foveated = false;

    m_target = target;

    for (uint32_t i = 0; i < VrEyeCount; i++) {
      Eye& eye = m_eyes[i];
      eye.foveated = false;

      // An eye whose image could not be made is not tried again until
      // the view or the target changes
      if (resized || !eye.map || !sameFov(eye.fov, views[i].fov))
        createEye(eye, views[i].fov, target);

      VrGazeTexel texel;

      if (!eye.view || !gaze || !vrLocateGazeTexel(eye.fov, (*gaze)[i], target, m_texelSize, texel))
        continue;

      size_t offset = eye.map->windowOffset(texel);
      eye.foveated = true;
      foveated = true;

      // The image keeps the window copied last until the gaze moves
      if (offset == eye.window)
        continue;

      eye.window = offset;

      VrExtent size = eye.map->imageSize();
      VkDeviceSize rowLength = eye.map->rowLength();

      m_device->EmitVrCommand([
        cImage    = eye.image,
        cRates    = eye.rates,
        cOffset   = VkDeviceSize(offset),
        cExtent   = VkExtent3D { size.width, size.height, 1u },
        cRowBytes = rowLength
      ] (DxvkContext* ctx) {
        VkImageSubresourceLayers layers = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };

        ctx->copyBufferToImage(cImage, layers, VkOffset3D { 0, 0, 0 }, cExtent,
          cRates, cOffset, cRowBytes, 0u, RateFormat);
      });
    }

    pickSample();

    m_frames += 1;
    m_foveatedFrames += foveated ? 1u : 0u;

    if (m_frames >= WindowFrames)
      logWindow();
  }


  void VrFoveation::beginWorld(uint32_t eye) {
    if (eye >= VrEyeCount)
      return;

    if (eye == 0u && m_sample != Sample::None && !m_queryActive) {
      m_device->EmitVrCommand([
        cQuery = m_invocations[m_sample == Sample::Rates ? 0u : 1u]
      ] (DxvkContext* ctx) {
        ctx->beginQuery(cQuery);
      });

      m_queryActive = true;
    }

    if (!m_eyes[eye].foveated)
      return;

    m_device->EmitVrCommand([
      cView      = m_eyes[eye].view,
      cTexelSize = VkExtent2D { m_texelSize, m_texelSize }
    ] (DxvkContext* ctx) {
      ctx->setShadingRateAttachment(cView, cTexelSize);
    });

    m_attached = true;
  }


  void VrFoveation::endWorld() {
    if (m_queryActive) {
      m_device->EmitVrCommand([
        cQuery = m_invocations[m_sample == Sample::Rates ? 0u : 1u]
      ] (DxvkContext* ctx) {
        ctx->endQuery(cQuery);
      });

      m_ratesSampled |= m_sample == Sample::Rates;
      m_fullSampled  |= m_sample == Sample::Full;
      m_queryActive = false;
      m_sample = Sample::None;
    }

    if (!m_attached)
      return;

    m_device->EmitVrCommand([] (DxvkContext* ctx) {
      ctx->setShadingRateAttachment(nullptr, VkExtent2D { });
    });

    m_attached = false;
  }


  void VrFoveation::stop() {
    endWorld();

    for (Eye& eye : m_eyes)
      eye.foveated = false;

    m_frames         = 0;
    m_foveatedFrames = 0;
    m_sample         = Sample::None;
    m_ratesSampled   = false;
    m_fullSampled    = false;
  }


  void VrFoveation::pickSample() {
    m_sample = Sample::None;

    if (!m_invocations[0] || !m_eyes[0].foveated)
      return;

    // The first frame of a window is measured with the rates, the second
    // at full rate: the scene barely changes between them
    if (m_frames == 0u) {
      m_sample = Sample::Rates;
      m_ratesSampled = false;
      m_fullSampled  = false;
    } else if (m_frames == 1u && m_ratesSampled) {
      m_sample = Sample::Full;
      m_eyes[0].foveated = false;
    }
  }


  void VrFoveation::createEye(Eye& eye, const VrFov& fov, VrExtent target) {
    const Rc<DxvkDevice>& dxvk = m_device->GetDXVKDevice();

    eye = Eye();
    eye.fov = fov;

    if (!target.width || !target.height)
      return;

    eye.map = std::make_unique<VrFoveationMap>(fov, target, m_texelSize, m_zones);
    const std::vector<uint8_t>& texels = eye.map->texels();
    VrExtent size = eye.map->imageSize();

    DxvkBufferCreateInfo bufferInfo;
    bufferInfo.size      = texels.size();
    bufferInfo.usage     = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.stages    = VK_PIPELINE_STAGE_TRANSFER_BIT;
    bufferInfo.access    = VK_ACCESS_TRANSFER_READ_BIT;
    bufferInfo.debugName = "VR shading rate map";

    Rc<DxvkBuffer> rates = dxvk->createBuffer(bufferInfo,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (!rates->mapPtr(0)) {
      Logger::warn("VR: The shading rate map could not be written, the eye is shaded at full rate");
      return;
    }

    std::memcpy(rates->mapPtr(0), texels.data(), texels.size());

    DxvkImageCreateInfo imageInfo;
    imageInfo.type        = VK_IMAGE_TYPE_2D;
    imageInfo.format      = RateFormat;
    imageInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.extent      = VkExtent3D { size.width, size.height, 1u };
    imageInfo.numLayers   = 1u;
    imageInfo.mipLevels   = 1u;
    imageInfo.usage       = VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR
                          | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.stages      = VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT;
    imageInfo.access      = VK_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR
                          | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    imageInfo.tiling      = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.layout      = RateLayout;
    imageInfo.debugName   = "VR shading rate image";

    Rc<DxvkImage> image = dxvk->createImage(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    DxvkImageViewKey viewKey;
    viewKey.viewType   = VK_IMAGE_VIEW_TYPE_2D;
    viewKey.usage      = VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
    viewKey.format     = RateFormat;
    viewKey.layout     = RateLayout;
    viewKey.aspects    = VK_IMAGE_ASPECT_COLOR_BIT;
    viewKey.mipIndex   = 0u;
    viewKey.mipCount   = 1u;
    viewKey.layerIndex = 0u;
    viewKey.layerCount = 1u;

    DxvkBufferCreateInfo readbackInfo;
    readbackInfo.size      = VkDeviceSize(size.width) * size.height;
    readbackInfo.usage     = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    readbackInfo.stages    = VK_PIPELINE_STAGE_TRANSFER_BIT;
    readbackInfo.access    = VK_ACCESS_TRANSFER_WRITE_BIT;
    readbackInfo.debugName = "VR shading rate readback";

    eye.readback = dxvk->createBuffer(readbackInfo,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

    eye.rates  = std::move(rates);
    eye.image  = std::move(image);
    eye.view   = eye.image->createView(viewKey);

    if (!eye.view->handle()) {
      Logger::warn("VR: The shading rate image has no view, the eye is shaded at full rate");
      eye.view = nullptr;
      return;
    }

    Logger::info(str::format("VR: Shading rate image of ", size.width, "x", size.height,
      " texels for a ", target.width, "x", target.height, " eye"));
  }


  void VrFoveation::logWindow() {
    Logger::info(str::format("VR: Eye-tracked foveation over ", m_frames, " frames: ",
      m_foveatedFrames, " drawn around the gaze, ", m_frames - m_foveatedFrames, " at full rate without a gaze"));

    for (uint32_t i = 0; i < VrEyeCount; i++)
      logImageContent(m_eyes[i], i);

    logInvocations();

    // The render passes are begun on the CS thread: their counts are logged there
    m_device->EmitVrCommand([] (DxvkContext* ctx) {
      DxvkShadingRateStats stats = ctx->takeShadingRateStats();

      Logger::info(str::format("VR: Eye-tracked foveation render passes: ", stats.passesTaken,
        " with the shading rate image, ", stats.passesSkipped, " of another size at full rate",
        stats.passesSkipped ? str::format(" (last ", stats.lastSkippedSize.width, "x",
          stats.lastSkippedSize.height, ")") : std::string()));
    });

    m_frames         = 0;
    m_foveatedFrames = 0;
  }


  void VrFoveation::logImageContent(Eye& eye, uint32_t index) {
    if (!eye.image || !eye.readback || !eye.readback->mapPtr(0))
      return;

    VrExtent size = eye.map->imageSize();

    // Read the copy made at the previous window, long finished by the GPU
    if (eye.readbackWritten) {
      const auto* texels = static_cast<const uint8_t*>(eye.readback->mapPtr(0));
      std::array<uint32_t, 4> counts = { };

      for (size_t i = 0; i < size_t(size.width) * size.height; i++) {
        switch (VrShadingRate(texels[i])) {
          case VrShadingRate::Full:    counts[0] += 1u; break;
          case VrShadingRate::Half:    counts[1] += 1u; break;
          case VrShadingRate::Quarter: counts[2] += 1u; break;
          default:                     counts[3] += 1u; break;
        }
      }

      size_t centre = size_t(size.height / 2u) * size.width + size.width / 2u;

      Logger::info(str::format("VR: ", index ? "Right" : "Left", " eye's shading rate image read back from the GPU: ",
        counts[0], " texels at full rate, ", counts[1], " at 2x2, ", counts[2], " at 4x4, ", counts[3],
        " other; centre ", uint32_t(texels[centre]), ", corner ", uint32_t(texels[0])));
    }

    m_device->EmitVrCommand([
      cBuffer = eye.readback,
      cImage  = eye.image,
      cExtent = VkExtent3D { size.width, size.height, 1u }
    ] (DxvkContext* ctx) {
      VkImageSubresourceLayers layers = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };

      ctx->copyImageToBuffer(cBuffer, 0u, 0u, 0u, RateFormat,
        cImage, layers, VkOffset3D { 0, 0, 0 }, cExtent);
    });

    eye.readbackWritten = true;
  }


  void VrFoveation::logInvocations() {
    if (!m_ratesSampled || !m_fullSampled)
      return;

    DxvkQueryData rates = { };
    DxvkQueryData full = { };

    // Measured at the start of the window: long finished by the GPU
    if (m_invocations[0]->getData(rates) != DxvkGpuQueryStatus::Available
     || m_invocations[1]->getData(full) != DxvkGpuQueryStatus::Available)
      return;

    double pixels = double(m_target.width) * double(m_target.height);
    uint64_t withRates = rates.statistic.fsInvocations;
    uint64_t atFull = full.statistic.fsInvocations;

    Logger::info(str::format("VR: Left eye's world, fragment shader invocations per pixel: ",
      std::round(100.0 * double(withRates) / pixels) / 100.0, " with the shading rate image, ",
      std::round(100.0 * double(atFull) / pixels) / 100.0, " at full rate on the next frame (",
      atFull ? uint32_t(std::round(100.0 * double(withRates) / double(atFull))) : 0u, "%)"));
  }


  uint32_t VrFoveation::pickTexelSize(const DxvkDevice& device) {
    const auto& properties = device.properties().khrFragmentShadingRate;
    VkExtent2D minSize = properties.minFragmentShadingRateAttachmentTexelSize;
    VkExtent2D maxSize = properties.maxFragmentShadingRateAttachmentTexelSize;

    uint32_t size = PreferredTexelSize;

    while (size > 1u && (size > maxSize.width || size > maxSize.height))
      size /= 2u;

    if (size < minSize.width || size < minSize.height
     || size > maxSize.width || size > maxSize.height)
      return 0u;

    return size;
  }

}
