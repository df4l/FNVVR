#include <cstring>

#include "vr_game_addresses.h"
#include "vr_game_camera.h"

namespace dxvk {

  namespace {

    using GetWorldCameraFn = void* (__cdecl*)();

    // __thiscall passes this in ECX and nothing in EDX, which is what
    // __fastcall does with an unused second argument
    using UpdateWorldToCameraFn = void (__fastcall*)(void* camera, void* unused);

    // The frustum starts with left, right, top, bottom, followed by near and far
    constexpr uint32_t FrustumPlaneCount = 4;

  }


  bool VrGameCamera::acquire() {
    auto getWorldCamera = reinterpret_cast<GetWorldCameraFn>(VrGame::GetWorldCamera);
    m_camera = static_cast<uint8_t*>(getWorldCamera());
    return m_camera != nullptr;
  }


  VrGameCameraState VrGameCamera::save() const {
    VrGameCameraState state;
    std::memcpy(state.rotation,    m_camera + VrGame::CameraWorldRotation,    sizeof(state.rotation));
    std::memcpy(state.translation, m_camera + VrGame::CameraWorldTranslation, sizeof(state.translation));
    std::memcpy(state.frustum,     m_camera + VrGame::CameraFrustum,          sizeof(state.frustum));
    return state;
  }


  void VrGameCamera::restore(const VrGameCameraState& state) {
    std::memcpy(m_camera + VrGame::CameraWorldRotation,    state.rotation,    sizeof(state.rotation));
    std::memcpy(m_camera + VrGame::CameraWorldTranslation, state.translation, sizeof(state.translation));
    std::memcpy(m_camera + VrGame::CameraFrustum,          state.frustum,     sizeof(state.frustum));
    refresh();
  }


  VrGameCameraPose VrGameCamera::readPose() const {
    float rotation[9];
    float translation[3];
    std::memcpy(rotation,    m_camera + VrGame::CameraWorldRotation,    sizeof(rotation));
    std::memcpy(translation, m_camera + VrGame::CameraWorldTranslation, sizeof(translation));

    // Row-major 3x3, the axes are its columns
    VrGameCameraPose pose;
    pose.forward  = { rotation[0], rotation[3], rotation[6] };
    pose.up       = { rotation[1], rotation[4], rotation[7] };
    pose.right    = { rotation[2], rotation[5], rotation[8] };
    pose.position = { translation[0], translation[1], translation[2] };
    return pose;
  }


  void VrGameCamera::apply(const VrGameCameraPose& pose, const VrGameFrustum& frustum) {
    const float rotation[9] = {
      pose.forward.x, pose.up.x, pose.right.x,
      pose.forward.y, pose.up.y, pose.right.y,
      pose.forward.z, pose.up.z, pose.right.z };
    const float translation[3] = { pose.position.x, pose.position.y, pose.position.z };

    std::memcpy(m_camera + VrGame::CameraWorldRotation,    rotation,    sizeof(rotation));
    std::memcpy(m_camera + VrGame::CameraWorldTranslation, translation, sizeof(translation));

    float planes[FrustumPlaneCount] = { frustum.left, frustum.right, frustum.top, frustum.bottom };
    std::memcpy(m_camera + VrGame::CameraFrustum, planes, sizeof(planes));

    refresh();
  }


  void VrGameCamera::refresh() {
    auto update = reinterpret_cast<UpdateWorldToCameraFn>(VrGame::CameraUpdateWorldToCamera);
    update(m_camera, nullptr);
  }

}
