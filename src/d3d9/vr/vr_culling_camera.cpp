#include "../../util/log/log.h"

#include "vr_culling_camera.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"

namespace dxvk {

  namespace {

    // Both are __thiscall with the camera as their only stack argument
    using VisibilityFn = void (__fastcall*)(void* object, void* unused, void* camera);

    VrCullingCamera* g_cullingCamera = nullptr;

  }


  VrCullingCamera::VrCullingCamera(ViewFn view)
  : m_view(std::move(view)) { }


  VrCullingCamera::~VrCullingCamera() {
    // The patched calls stay in place and fall through to the game's functions
    g_cullingCamera = nullptr;
  }


  std::unique_ptr<VrCullingCamera> VrCullingCamera::install(ViewFn view) {
    if (g_cullingCamera)
      return nullptr;

    // Both calls are checked first, so that the pass never culls the
    // occlusion planes and the multibounds for different views
    bool found = VrGameMemory::callsTo(VrGame::OcclusionPlaneVisibilityCallSite, VrGame::OcclusionPlaneVisibility)
      && VrGameMemory::callsTo(VrGame::MultiBoundVisibilityCallSite, VrGame::MultiBoundVisibility)
      && VrGameMemory::redirectCall(VrGame::OcclusionPlaneVisibilityCallSite, VrGame::OcclusionPlaneVisibility,
           reinterpret_cast<const void*>(&VrCullingCamera::occlusionPlaneHook))
      && VrGameMemory::redirectCall(VrGame::MultiBoundVisibilityCallSite, VrGame::MultiBoundVisibility,
           reinterpret_cast<const void*>(&VrCullingCamera::multiBoundHook));

    if (!found) {
      Logger::warn("VR: The game's visibility pass was not found, objects outside of the game's view may be missing in the headset");
      return nullptr;
    }

    std::unique_ptr<VrCullingCamera> cullingCamera(new VrCullingCamera(std::move(view)));
    g_cullingCamera = cullingCamera.get();
    return cullingCamera;
  }


  void VrCullingCamera::cullAgain() {
    using ResetBoundsFn = void (__fastcall*)(void* tes, void* unused, uint32_t landscape, uint32_t rooms);
    using WorldCameraFn = void* (__cdecl*)();

    void* camera = reinterpret_cast<WorldCameraFn>(VrGame::GetWorldCamera)();
    void* tes    = *reinterpret_cast<void**>(VrGame::TesSingleton);
    void* scene  = *reinterpret_cast<void**>(VrGame::ShadowSceneNodes);

    reinterpret_cast<ResetBoundsFn>(VrGame::ResetMultiBounds)(tes, nullptr, 1, 1);
    occlusionPlaneHook(scene, nullptr, camera);
    multiBoundHook(tes, nullptr, camera);
  }


  void __fastcall VrCullingCamera::occlusionPlaneHook(
          void*                 sceneNode,
          void*                 unused,
          void*                 camera) {
    auto original = reinterpret_cast<VisibilityFn>(VrGame::OcclusionPlaneVisibility);
    VrGameCameraState saved;

    if (!g_cullingCamera || !g_cullingCamera->begin(camera, saved)) {
      original(sceneNode, nullptr, camera);
      return;
    }

    original(sceneNode, nullptr, camera);
    g_cullingCamera->m_camera.restore(saved);
  }


  void __fastcall VrCullingCamera::multiBoundHook(
          void*                 tes,
          void*                 unused,
          void*                 camera) {
    auto original = reinterpret_cast<VisibilityFn>(VrGame::MultiBoundVisibility);
    VrGameCameraState saved;

    if (!g_cullingCamera || !g_cullingCamera->begin(camera, saved)) {
      original(tes, nullptr, camera);
      return;
    }

    original(tes, nullptr, camera);
    g_cullingCamera->m_camera.restore(saved);
  }


  bool VrCullingCamera::begin(void* camera, VrGameCameraState& saved) {
    // The game passes its world camera, unless a debug camera is frozen
    if (!m_camera.acquire() || !m_camera.isCamera(camera))
      return false;

    VrGameCameraPose pose = m_camera.readPose();
    VrGameFrustum frustum;

    if (!m_view(pose, frustum))
      return false;

    saved = m_camera.save();
    m_camera.apply(pose, frustum);

    if (!m_logged) {
      Logger::info("VR: The scene is culled for the head and the field of view of both eyes");
      m_logged = true;
    }

    return true;
  }

}
