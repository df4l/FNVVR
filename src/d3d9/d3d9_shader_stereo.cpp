#include "d3d9_shader_stereo.h"

#include "d3d9_shader.h"

#include <sm3/sm3_parser.h>

#include <algorithm>
#include <array>
#include <unordered_set>
#include <vector>

namespace dxvk {

  namespace {

    /// Rows of the stereo matrix
    constexpr uint32_t StereoRows     = 4u;

    /// Constants of a projecting shader's projection
    constexpr uint32_t ProjectionRows = 4u;

    dxbc_spv::ir::SsaDef declareStereoCbv(
            dxbc_spv::ir::Builder&    builder,
            dxbc_spv::ir::SsaDef      entryPoint) {
      using namespace dxbc_spv;

      // Both passes may run on the same shader
      for (auto iter = builder.getDeclarations().first;
                iter != builder.getDeclarations().second; iter++) {
        if (iter->getOpCode() == ir::OpCode::eDclCbv
         && uint32_t(iter->getOperand(iter->getFirstLiteralOperandIndex() + 1u)) == D3D9ShaderResourceMapping::VSStereoTransform)
          return iter->getDef();
      }

      auto cbvType = ir::Type(ir::ScalarType::eF32, 4u).addArrayDimension(StereoRows);

      auto cbv = builder.add(ir::Op::DclCbv(cbvType, entryPoint, 0u,
        D3D9ShaderResourceMapping::VSStereoTransform, 1u).setFlags(ir::OpFlag::eInBounds));
      builder.add(ir::Op::DebugName(cbv, "stereo"));
      return cbv;
    }

    /// Sum of the four components times the stereo row, at the cursor
    dxbc_spv::ir::SsaDef emitRowDot(
            dxbc_spv::ir::Builder&    builder,
            dxbc_spv::ir::SsaDef      descriptor,
            uint32_t                  row,
      const std::array<dxbc_spv::ir::SsaDef, 4u>& values) {
      using namespace dxbc_spv;

      auto rowType = ir::Type(ir::ScalarType::eF32, 4u);
      auto rowDef = builder.add(ir::Op::BufferLoad(rowType, descriptor,
        builder.makeConstant(row), 16u).setFlags(ir::OpFlag::eInBounds));

      ir::SsaDef sum = { };

      for (uint32_t c = 0u; c < values.size(); c++) {
        auto factor = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eF32, rowDef, builder.makeConstant(c)));
        auto product = builder.add(ir::Op::FMul(ir::ScalarType::eF32, factor, values[c]).setFlags(ir::OpFlag::ePrecise));

        sum = sum
          ? builder.add(ir::Op::FAdd(ir::ScalarType::eF32, sum, product).setFlags(ir::OpFlag::ePrecise))
          : product;
      }

      return sum;
    }

    dxbc_spv::ir::SsaDef findBuiltIn(
            dxbc_spv::ir::Builder&    builder,
            dxbc_spv::ir::OpCode      opCode,
            dxbc_spv::ir::BuiltIn     builtIn) {
      using namespace dxbc_spv;

      for (auto iter = builder.getDeclarations().first;
                iter != builder.getDeclarations().second; iter++) {
        if (iter->getOpCode() == opCode
         && ir::BuiltIn(iter->getOperand(iter->getFirstLiteralOperandIndex())) == builtIn)
          return iter->getDef();
      }

      return { };
    }

    /// Loads of the given constants that the position is computed from
    std::unordered_set<dxbc_spv::ir::SsaDef> findPositionLoads(
            dxbc_spv::ir::Builder&    builder,
            dxbc_spv::ir::SsaDef      constants,
            bool&                     found) {
      using namespace dxbc_spv;

      std::unordered_set<ir::SsaDef> loads;
      found = false;

      auto position = findBuiltIn(builder, ir::OpCode::eDclOutputBuiltIn, ir::BuiltIn::ePosition);

      if (!position)
        return loads;

      // The front-end keeps the position in temporaries until its last stores
      std::vector<ir::SsaDef> pending;

      dxvk::small_vector<ir::SsaDef, 16u> uses;
      builder.getUses(position, uses);

      for (auto use : uses) {
        const auto& useOp = builder.getOp(use);

        if (useOp.getOpCode() != ir::OpCode::eOutputStore)
          continue;

        const auto& valueOp = builder.getOpForOperand(useOp, 2u);

        if (valueOp.getOpCode() != ir::OpCode::eTmpLoad)
          return loads;

        auto temp = ir::SsaDef(valueOp.getOperand(0u));

        dxvk::small_vector<ir::SsaDef, 16u> tempUses;
        builder.getUses(temp, tempUses);

        for (auto tempUse : tempUses) {
          const auto& tempOp = builder.getOp(tempUse);

          if (tempOp.getOpCode() == ir::OpCode::eTmpStore)
            pending.push_back(ir::SsaDef(tempOp.getOperand(1u)));
        }
      }

      if (pending.empty())
        return loads;

      // What the stored values are computed from, up to other registers
      std::unordered_set<ir::SsaDef> visited;

      while (!pending.empty()) {
        auto def = pending.back();
        pending.pop_back();

        if (!def || !visited.insert(def).second)
          continue;

        const auto& op = builder.getOp(def);

        if (op.isConstant() || op.isDeclarative())
          continue;

        if (op.getOpCode() == ir::OpCode::eInputLoad) {
          if (ir::SsaDef(op.getOperand(0u)) == constants)
            loads.insert(def);
          continue;
        }

        if (op.getOpCode() == ir::OpCode::eTmpLoad
         || op.getOpCode() == ir::OpCode::eParamLoad)
          continue;

        uint32_t first = op.getOpCode() == ir::OpCode::eFunctionCall ? 1u : 0u;

        for (uint32_t i = first; i < op.getFirstLiteralOperandIndex(); i++)
          pending.push_back(ir::SsaDef(op.getOperand(i)));
      }

      found = true;
      return loads;
    }

  }


  bool D3D9ProjectsWithFirstConstants(
    const void*                     bytecode,
          size_t                    size) {
    using namespace dxbc_spv::sm3;

    Parser parser(dxbc_spv::util::ByteReader(bytecode, size));

    if (!parser)
      return false;

    ShaderInfo info = parser.getShaderInfo();

    if (info.getType() != ShaderType::eVertex)
      return false;

    // Shader model 3 declares which output holds the position
    bool sm3 = info.getVersion().first >= 3u;
    RegisterType positionType = sm3 ? RegisterType::eOutput : RegisterType::eRasterizerOut;
    uint32_t positionIndex = sm3 ? ~0u : uint32_t(RasterizerOutIndex::eRasterOutPosition);

    uint8_t written = 0u;

    while (parser) {
      Instruction ins = parser.parseInstruction();

      if (!ins)
        return false;

      OpCode opCode = ins.getOpCode();

      if (opCode == OpCode::eComment || opCode == OpCode::eEnd)
        continue;

      if (ins.hasDcl()) {
        const Operand& dst = ins.getDst();

        if (sm3 && dst.getRegisterType() == RegisterType::eOutput
         && ins.getDcl().getSemanticUsage() == SemanticUsage::ePosition
         && !ins.getDcl().getSemanticIndex())
          positionIndex = dst.getIndex();
        continue;
      }

      if (opCode == OpCode::eDef) {
        const Operand& dst = ins.getDst();

        if (dst.getRegisterType() == RegisterType::eConst && dst.getIndex() < ProjectionRows)
          return false;
        continue;
      }

      // A relative read could reach the projection, which is moved per load
      for (uint32_t i = 0u; i < ins.getSrcCount(); i++) {
        const Operand& src = ins.getSrc(i);

        if (src.getRegisterType() == RegisterType::eConst && src.hasRelativeAddressing())
          return false;
      }

      if (!ins.hasDst())
        continue;

      const Operand& dst = ins.getDst();

      if (dst.getRegisterType() != positionType)
        continue;

      if (dst.hasRelativeAddressing())
        return false;

      if (dst.getIndex() != positionIndex)
        continue;

      auto mask = uint8_t(dst.getWriteMask(info));

      if (opCode != OpCode::eDp4 || ins.isPredicated() || dst.isSaturated() || dst.getShift()
       || !mask || (mask & (mask - 1u)) || (written & mask))
        return false;

      uint32_t component = 0u;

      while (!(mask & (1u << component)))
        component++;

      bool projected = false;

      for (uint32_t i = 0u; i < ins.getSrcCount(); i++) {
        const Operand& src = ins.getSrc(i);

        projected |= src.getRegisterType() == RegisterType::eConst
                  && src.getIndex() == component
                  && src.getModifier() == OperandModifier::eNone
                  && src.getSwizzle(info) == dxbc_spv::util::Swizzle::identity();
      }

      if (!projected)
        return false;

      written |= mask;
    }

    return written == 0xfu;
  }


  bool D3D9ApplyStereoProjection(
          dxbc_spv::ir::Builder&    builder,
          dxbc_spv::ir::SsaDef      entryPoint) {
    using namespace dxbc_spv;

    auto constants = findBuiltIn(builder, ir::OpCode::eDclInputBuiltIn, ir::BuiltIn::eLegacyConstFloat);

    if (!constants)
      return false;

    // The position keeps its own loads and gets the stereo transform at its
    // stores like any other shader's, so that passes drawn with different
    // shaders on the same geometry give the same depth
    bool found = false;
    auto positionLoads = findPositionLoads(builder, constants, found);

    if (!found)
      return false;

    std::vector<std::pair<ir::SsaDef, uint32_t>> loads;

    dxvk::small_vector<ir::SsaDef, 256u> uses;
    builder.getUses(constants, uses);

    for (auto use : uses) {
      const auto& useOp = builder.getOp(use);

      if (useOp.getOpCode() != ir::OpCode::eInputLoad)
        continue;

      const auto& addressOp = builder.getOpForOperand(useOp, 1u);

      // The front-end reads whole constants at fixed addresses
      if (!addressOp.isConstant() || addressOp.getOperandCount() != 1u)
        return false;

      auto index = uint32_t(addressOp.getOperand(0u));

      if (index < ProjectionRows && !positionLoads.count(use))
        loads.push_back({ use, index });
    }

    if (loads.empty())
      return false;

    auto vec4Type = ir::Type(ir::ScalarType::eF32, 4u);
    auto cbv = declareStereoCbv(builder, entryPoint);

    // Each other row of the projection becomes the stereo row times the four rows
    for (auto [load, index] : loads) {
      auto cursor = builder.setCursor(load);

      auto descriptor = builder.add(ir::Op::DescriptorLoad(
        ir::ScalarType::eCbv, cbv, builder.makeConstant(0u)));

      std::array<ir::SsaDef, ProjectionRows> rows = { };

      for (uint32_t r = 0u; r < rows.size(); r++)
        rows[r] = builder.add(ir::Op::InputLoad(vec4Type, constants, builder.makeConstant(r)));

      std::array<ir::SsaDef, 4u> components = { };

      for (uint32_t c = 0u; c < components.size(); c++) {
        std::array<ir::SsaDef, ProjectionRows> column = { };

        for (uint32_t r = 0u; r < rows.size(); r++)
          column[r] = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eF32, rows[r], builder.makeConstant(c)));

        components[c] = emitRowDot(builder, descriptor, index, column);
      }

      auto moved = builder.add(ir::Op::CompositeConstruct(vec4Type,
        components[0u], components[1u], components[2u], components[3u]));

      builder.setCursor(cursor == load ? moved : cursor);
      builder.rewriteDef(load, moved);
    }

    return true;
  }


  void D3D9ApplyStereoTransform(
          dxbc_spv::ir::Builder&    builder,
          dxbc_spv::ir::SsaDef      entryPoint) {
    using namespace dxbc_spv;

    auto position = findBuiltIn(builder, ir::OpCode::eDclOutputBuiltIn, ir::BuiltIn::ePosition);

    if (!position)
      return;

    std::array<ir::SsaDef, 4u> stores = { };
    std::array<ir::SsaDef, 4u> values = { };

    dxvk::small_vector<ir::SsaDef, 16u> uses;
    builder.getUses(position, uses);

    for (auto use : uses) {
      const auto& useOp = builder.getOp(use);

      if (useOp.getOpCode() != ir::OpCode::eOutputStore)
        continue;

      auto address = ir::SsaDef(useOp.getOperand(1u));

      if (!address || !builder.getOp(address).isConstant())
        return;

      auto component = uint32_t(builder.getOp(address).getOperand(0u));

      if (component >= stores.size() || stores[component])
        return;

      stores[component] = use;
      values[component] = ir::SsaDef(useOp.getOperand(2u));
    }

    for (auto store : stores) {
      if (!store)
        return;
    }

    // The new stores go after the last of the four, which must
    // follow the first one closely in the same block
    constexpr uint32_t MaxDistance = 64u;

    ir::SsaDef last = { };
    uint32_t found = 0u;
    uint32_t distance = 0u;

    for (auto def = stores[0u]; def && found < stores.size() && distance < MaxDistance; def = builder.getNext(def), distance++) {
      if (std::find(stores.begin(), stores.end(), def) != stores.end()) {
        last = def;
        found++;
      }
    }

    if (found < stores.size())
      return;

    auto cbv = declareStereoCbv(builder, entryPoint);

    auto cursor = builder.setCursor(last);

    auto descriptor = builder.add(ir::Op::DescriptorLoad(
      ir::ScalarType::eCbv, cbv, builder.makeConstant(0u)));

    std::array<ir::SsaDef, 4u> results = { };

    for (uint32_t r = 0u; r < results.size(); r++)
      results[r] = emitRowDot(builder, descriptor, r, values);

    ir::SsaDef lastStore = { };

    for (uint32_t r = 0u; r < results.size(); r++)
      lastStore = builder.add(ir::Op::OutputStore(position, builder.makeConstant(r), results[r]));

    // The position is written once, transformed
    for (auto store : stores)
      builder.remove(store);

    bool cursorRemoved = std::find(stores.begin(), stores.end(), cursor) != stores.end();
    builder.setCursor(cursorRemoved ? lastStore : cursor);
  }

}
