#include <cstring>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_game_nodes.h"
#include "vr_scene_model.h"

namespace dxvk {

  namespace {

    using LoadFileFn    = void* (__thiscall*)(void* loader, const char* path, uint32_t lodFade,
      uint32_t assignShaders, uint32_t unknown, uint32_t keepUv, uint32_t noUseCount);
    using CloneFn       = void* (__thiscall*)(void* object, void* process);
    using CloningInitFn = void  (__thiscall*)(void* process, float scale);
    using CloningFreeFn = void  (__thiscall*)(void* process);
    using UpdateFn      = void  (__thiscall*)(void* object, void* data);
    using AttachChildFn = void  (__thiscall*)(void* node, void* child, uint32_t firstFree);
    using DetachChildFn = void  (__thiscall*)(void* node, void* child);
    using SetRangeFn    = void  (__thiscall*)(void* node, float nearDistance, float farDistance);
    using MethodFn      = void  (__thiscall*)(void* object);
    using SceneObjectFn = void  (__thiscall*)(void* shadowSceneNode, void* object);
    using AssignFn      = void* (__thiscall*)(void* pointer, void* object);

    // The models are a few levels deep
    constexpr uint32_t MaxDepth = 8;

    // As FORenderedTerminal::OnMenuOpen sets its monitor's fade node
    constexpr float FadeDistance = 100000.0f;

    template<typename T>
    T& field(void* object, uintptr_t offset) {
      return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
    }

    template<typename Fn>
    Fn method(void* object, uintptr_t slot) {
      return *reinterpret_cast<Fn*>(field<uint8_t*>(object, 0) + slot);
    }

    template<typename Fn>
    Fn game(uintptr_t address) {
      return reinterpret_cast<Fn>(address);
    }

    void* menuShadowSceneNode() {
      return reinterpret_cast<void* const*>(VrGame::ShadowSceneNodes)[VrGame::MenuShadowSceneNode];
    }

    bool supported() {
      struct Bytes {
        uintptr_t      address;
        const uint8_t* bytes;
        size_t         size;
      };

      const Bytes checks[] = {
        { VrGame::ModelLoaderLoadFile,         VrGame::ModelLoaderLoadFilePrologue,         sizeof(VrGame::ModelLoaderLoadFilePrologue) },
        { VrGame::ModelLoaderSingletonUseSite, VrGame::ModelLoaderSingletonUse,             sizeof(VrGame::ModelLoaderSingletonUse) },
        { VrGame::ObjectClone,                 VrGame::ObjectClonePrologue,                 sizeof(VrGame::ObjectClonePrologue) },
        { VrGame::CloningProcessConstruct,     VrGame::CloningProcessConstructPrologue,     sizeof(VrGame::CloningProcessConstructPrologue) },
        { VrGame::CloningProcessDestruct,      VrGame::CloningProcessDestructPrologue,      sizeof(VrGame::CloningProcessDestructPrologue) },
        { VrGame::FadeNodeSetRange,            VrGame::FadeNodeSetRangePrologue,            sizeof(VrGame::FadeNodeSetRangePrologue) },
        { VrGame::FadeNodeTurnOff,             VrGame::FadeNodeTurnOffPrologue,             sizeof(VrGame::FadeNodeTurnOffPrologue) },
        { VrGame::ShadowSceneNodeAddObject,    VrGame::ShadowSceneNodeAddObjectPrologue,    sizeof(VrGame::ShadowSceneNodeAddObjectPrologue) },
        { VrGame::ShadowSceneNodeRemoveObject, VrGame::ShadowSceneNodeRemoveObjectPrologue, sizeof(VrGame::ShadowSceneNodeRemoveObjectPrologue) },
      };

      for (const auto& check : checks) {
        if (!VrGameMemory::matches(check.address, check.bytes, check.size))
          return false;
      }

      return VrGameMemory::readable(VrGame::ModelLoaderSingleton, sizeof(uintptr_t))
          && VrGameMemory::readable(VrGame::FadeNodeVtable, sizeof(uintptr_t))
          && VrGameMemory::readable(VrGame::ShadowSceneNodes, (VrGame::MenuShadowSceneNode + 1) * sizeof(uintptr_t));
    }

  }


  std::unique_ptr<VrSceneModel> VrSceneModel::create(const char* path) {
    if (!supported()) {
      Logger::warn(str::format("VR: The game's model functions were not found, ", path, " is not shown"));
      return nullptr;
    }

    return std::unique_ptr<VrSceneModel>(new VrSceneModel(path));
  }


  VrSceneModel::VrSceneModel(const char* path)
  : m_path(path) {

  }


  VrSceneModel::~VrSceneModel() {
    // The model is left to the game, which may be shutting down
  }


  bool VrSceneModel::show(void* sibling, const VrVector3& translate, float scale) {
    void* parent = sibling ? field<void*>(sibling, VrGame::ObjectParent) : nullptr;

    if (m_failed || !parent)
      return false;

    hide();

    if (!m_root && !load()) {
      Logger::err(str::format("VR: Failed to read ", m_path));
      m_failed = true;
      return false;
    }

    // The sibling's place, then the model's in its space
    float rotation[9];
    std::memcpy(rotation, static_cast<uint8_t*>(sibling) + VrGame::ObjectLocalRotation, sizeof(rotation));
    VrVector3 position = field<VrVector3>(sibling, VrGame::ObjectLocalTranslation);
    float     size     = field<float>(sibling, VrGame::ObjectLocalScale);

    VrVector3 offset = translate * size;
    position.x += rotation[0] * offset.x + rotation[1] * offset.y + rotation[2] * offset.z;
    position.y += rotation[3] * offset.x + rotation[4] * offset.y + rotation[5] * offset.z;
    position.z += rotation[6] * offset.x + rotation[7] * offset.y + rotation[8] * offset.z;

    std::memcpy(static_cast<uint8_t*>(m_root) + VrGame::ObjectLocalRotation, rotation, sizeof(rotation));
    field<VrVector3>(m_root, VrGame::ObjectLocalTranslation) = position;
    field<float>(m_root, VrGame::ObjectLocalScale) = size * scale;

    m_parent = parent;
    method<AttachChildFn>(parent, VrGame::NodeAttachChildSlot)(parent, m_root, 1);

    uint8_t data[VrGame::UpdateDataSize] = { };
    game<UpdateFn>(VrGame::ObjectUpdate)(m_root, data);

    if (void* scene = menuShadowSceneNode())
      game<SceneObjectFn>(VrGame::ShadowSceneNodeAddObject)(scene, m_root);

    return true;
  }


  void VrSceneModel::hide() {
    if (!m_parent)
      return;

    if (void* scene = menuShadowSceneNode())
      game<SceneObjectFn>(VrGame::ShadowSceneNodeRemoveObject)(scene, m_root);

    method<DetachChildFn>(m_parent, VrGame::NodeDetachChildSlot)(m_parent, m_root);
    m_parent = nullptr;
  }


  bool VrSceneModel::load() {
    void* loader = *reinterpret_cast<void* const*>(VrGame::ModelLoaderSingleton);
    void* cached = loader ? game<LoadFileFn>(VrGame::ModelLoaderLoadFile)(loader, m_path, 0, 1, 0, 0, 0) : nullptr;

    if (!cached)
      return false;

    // A copy, since the world may draw the cached model
    alignas(4) uint8_t process[VrGame::CloningProcessSize];
    game<CloningInitFn>(VrGame::CloningProcessConstruct)(process, 1.0f);
    void* copy = game<CloneFn>(VrGame::ObjectClone)(cached, process);
    game<CloningFreeFn>(VrGame::CloningProcessDestruct)(process);

    if (!copy)
      return false;

    game<AssignFn>(VrGame::NiPointerAssign)(&m_root, copy);

    // The copy's collision body never joins a physics world
    vrVisitGameObjects(reinterpret_cast<uintptr_t>(m_root), MaxDepth, [] (uintptr_t object, uint32_t) {
      void** collision = &field<void*>(reinterpret_cast<void*>(object), VrGame::ObjectCollision);

      if (*collision)
        game<AssignFn>(VrGame::NiPointerAssign)(collision, nullptr);
    });

    // Shown up close whatever the distance, as a terminal's monitor
    if (field<uintptr_t>(m_root, 0) == VrGame::FadeNodeVtable) {
      field<uint32_t>(m_root, VrGame::FadeNodeLodMultType) = VrGame::LodMultRenderedMenu;
      game<SetRangeFn>(VrGame::FadeNodeSetRange)(m_root, FadeDistance, FadeDistance);
      game<MethodFn>(VrGame::FadeNodeTurnOff)(m_root);
    }

    Logger::info(str::format("VR: ", m_path, " was read"));
    return true;
  }

}
