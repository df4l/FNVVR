#include <cstring>

#include "vr_game_addresses.h"
#include "vr_game_nodes.h"

namespace dxvk {

  namespace {

    using GetAsNodeFn = uintptr_t (__fastcall*)(uintptr_t object, void* unused);

    template<typename T>
    T readField(uintptr_t base, uintptr_t offset) {
      return *reinterpret_cast<const T*>(base + offset);
    }

  }


  uintptr_t vrGameAsNode(uintptr_t object) {
    auto vtable = readField<uintptr_t>(object, 0);
    return reinterpret_cast<GetAsNodeFn>(readField<uintptr_t>(vtable, VrGame::ObjectGetAsNodeSlot))(object, nullptr);
  }


  uintptr_t vrFindGameObject(uintptr_t root, const char* name, uint32_t maxDepth) {
    if (!root)
      return 0;

    auto objectName = readField<const char*>(root, VrGame::ObjectName);

    if (objectName && !std::strcmp(objectName, name))
      return root;

    uintptr_t node = vrGameAsNode(root);

    if (!node || !maxDepth)
      return 0;

    auto children = readField<const uintptr_t*>(node, VrGame::NodeChildren);
    auto count    = readField<uint16_t>(node, VrGame::NodeChildCount);

    for (uint32_t i = 0; children && i < count; i++) {
      if (uintptr_t found = vrFindGameObject(children[i], name, maxDepth - 1))
        return found;
    }

    return 0;
  }

}
