#pragma once

namespace dxvk {

  /**
   * \brief Lines the water reflections up with the headset's frustum
   *
   * The game mirrors the eye's camera about the water plane to draw the
   * reflections, negating its up axis and keeping its frustum, and the water
   * shader reads the reflection at the pixel's own screen position, flipped
   * vertically. An eye's frustum is not symmetric vertically, so every pixel
   * read its reflection about 4.5 degrees too high: distant water showed the
   * hazy sky above the horizon instead of the shore, and reflections moved
   * with the head. The mirrored camera gets the frustum flipped vertically
   * (vrComputeReflectionFrustum), which leaves a symmetric frustum as it is.
   * See findings/water.md.
   */
  class VrWaterReflection {

  public:

    /**
     * \brief Flips the frustum of the cameras mirrored from now on
     *
     * Another plugin's hook on these calls stays in the chain. Logs if the
     * code is not the supported version, which is then left as it is.
     *
     * \returns \c true if the reflections are lined up
     */
    static bool install();

  private:

    static void __cdecl reflectCameraHook(void* camera, void* plane, void* reflected);

  };

}
