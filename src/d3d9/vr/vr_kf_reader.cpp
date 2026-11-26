#include <algorithm>
#include <cmath>
#include <cstring>

#include "vr_kf_reader.h"
#include "vr_math.h"

namespace dxvk {

  namespace {

    // Gamebryo 20.2.0.7, as the game's files are written
    constexpr uint32_t FileVersion = 0x14020007;

    // NiTransformData key types
    constexpr uint32_t KeyLinear    = 1;
    constexpr uint32_t KeyQuadratic = 2;
    constexpr uint32_t KeyTbc       = 3;
    constexpr uint32_t KeyXyz       = 4;

    // A controlled block of a NiControllerSequence: interpolator,
    // controller, priority, node name, property type, controller type,
    // controller ID, interpolator ID
    constexpr size_t ControlledBlockSize = 4 + 4 + 1 + 5 * 4;

    class Reader {

    public:

      Reader(const uint8_t* data, size_t size, size_t offset = 0)
      : m_data(data), m_size(size), m_pos(offset) { }

      template<typename T>
      T get() {
        T value = T();

        if (m_pos + sizeof(T) > m_size) {
          m_failed = true;
          m_pos    = m_size;
          return value;
        }

        std::memcpy(&value, m_data + m_pos, sizeof(T));
        m_pos += sizeof(T);
        return value;
      }

      std::string bytes(size_t count) {
        if (m_pos + count > m_size) {
          m_failed = true;
          m_pos    = m_size;
          return std::string();
        }

        std::string text(reinterpret_cast<const char*>(m_data + m_pos), count);
        m_pos += count;
        return text;
      }

      void skip(size_t count) {
        if (m_pos + count > m_size) {
          m_failed = true;
          m_pos    = m_size;
          return;
        }

        m_pos += count;
      }

      void fail() {
        m_failed = true;
        m_pos    = m_size;
      }

      size_t position() const { return m_pos; }
      bool   failed()   const { return m_failed; }

    private:

      const uint8_t* m_data;
      size_t         m_size;
      size_t         m_pos;
      bool           m_failed = false;

    };

    struct Block {
      std::string type;
      size_t      offset = 0;
      size_t      size   = 0;
    };

    VrGameRotation quaternionRotation(float w, float x, float y, float z) {
      VrQuaternion q;
      q.w = w;
      q.x = x;
      q.y = y;
      q.z = z;
      return vrGameRotationFromQuaternion(vrNormalize(q));
    }

    // Size of a float key after its time and value
    size_t floatKeyExtra(uint32_t type) {
      switch (type) {
        case KeyQuadratic: return 8;
        case KeyTbc:       return 12;
        default:           return 0;
      }
    }

    // Size of a vector key after its time and value
    size_t vectorKeyExtra(uint32_t type) {
      switch (type) {
        case KeyQuadratic: return 24;
        case KeyTbc:       return 12;
        default:           return 0;
      }
    }

    bool knownKeyType(uint32_t type) {
      return type == KeyLinear || type == KeyQuadratic || type == KeyTbc;
    }

    void readFloatKeys(Reader& reader, std::vector<VrKfTrack::FloatKey>& keys) {
      uint32_t count = reader.get<uint32_t>();

      if (!count)
        return;

      uint32_t type = reader.get<uint32_t>();

      if (!knownKeyType(type)) {
        reader.fail();
        return;
      }

      for (uint32_t i = 0; i < count && !reader.failed(); i++) {
        VrKfTrack::FloatKey key;
        key.time  = reader.get<float>();
        key.value = reader.get<float>();
        reader.skip(floatKeyExtra(type));
        keys.push_back(key);
      }
    }

    bool readTransformData(Reader& reader, VrKfTrack& track) {
      uint32_t rotationCount = reader.get<uint32_t>();

      if (rotationCount) {
        uint32_t type = reader.get<uint32_t>();

        if (type == KeyXyz) {
          track.euler = true;

          for (auto& axis : track.angles)
            readFloatKeys(reader, axis);
        } else if (knownKeyType(type)) {
          for (uint32_t i = 0; i < rotationCount && !reader.failed(); i++) {
            VrKfTrack::RotationKey key;
            key.time = reader.get<float>();

            float w = reader.get<float>();
            float x = reader.get<float>();
            float y = reader.get<float>();
            float z = reader.get<float>();
            key.value = quaternionRotation(w, x, y, z);

            if (type == KeyTbc)
              reader.skip(12);

            track.rotations.push_back(key);
          }
        } else {
          return false;
        }
      }

      uint32_t translationCount = reader.get<uint32_t>();

      if (translationCount) {
        uint32_t type = reader.get<uint32_t>();

        if (!knownKeyType(type))
          return false;

        for (uint32_t i = 0; i < translationCount && !reader.failed(); i++) {
          VrKfTrack::VectorKey key;
          key.time    = reader.get<float>();
          key.value.x = reader.get<float>();
          key.value.y = reader.get<float>();
          key.value.z = reader.get<float>();
          reader.skip(vectorKeyExtra(type));
          track.translations.push_back(key);
        }
      }

      readFloatKeys(reader, track.scales);
      return !reader.failed();
    }

    // Index of the last key at or before a time, and the weight of the next
    template<typename Key>
    bool bracket(const std::vector<Key>& keys, float time, size_t& index, float& weight) {
      if (keys.empty())
        return false;

      index  = 0;
      weight = 0.0f;

      if (time <= keys.front().time)
        return true;

      if (time >= keys.back().time) {
        index = keys.size() - 1;
        return true;
      }

      while (index + 1 < keys.size() && keys[index + 1].time <= time)
        index++;

      float span = keys[index + 1].time - keys[index].time;
      weight = span > 0.0f ? (time - keys[index].time) / span : 0.0f;
      return true;
    }

    float sampleFloat(const std::vector<VrKfTrack::FloatKey>& keys, float time, float rest) {
      size_t index;
      float weight;

      if (!bracket(keys, time, index, weight))
        return rest;

      if (weight <= 0.0f)
        return keys[index].value;

      return keys[index].value + (keys[index + 1].value - keys[index].value) * weight;
    }

  }


  VrGameRotation vrKfEulerRotation(float x, float y, float z) {
    float cx = std::cos(x), sx = std::sin(x);
    float cy = std::cos(y), sy = std::sin(y);
    float cz = std::cos(z), sz = std::sin(z);

    VrGameRotation rx;
    rx.m[1][1] =  cx; rx.m[1][2] = sx;
    rx.m[2][1] = -sx; rx.m[2][2] = cx;

    VrGameRotation ry;
    ry.m[0][0] = cy; ry.m[0][2] = -sy;
    ry.m[2][0] = sy; ry.m[2][2] =  cy;

    VrGameRotation rz;
    rz.m[0][0] =  cz; rz.m[0][1] = sz;
    rz.m[1][0] = -sz; rz.m[1][1] = cz;

    return rx * (ry * rz);
  }


  bool VrKfAnimation::read(const uint8_t* data, size_t size) {
    m_tracks.clear();
    m_textKeys.clear();
    m_length = 0.0f;

    const void* line = std::memchr(data, '\n', std::min<size_t>(size, 64));

    if (!line)
      return false;

    Reader header(data, size, static_cast<const uint8_t*>(line) - data + 1);

    if (header.get<uint32_t>() != FileVersion || header.get<uint8_t>() != 1)
      return false;

    header.get<uint32_t>();                   // user version
    uint32_t blockCount = header.get<uint32_t>();
    header.get<uint32_t>();                   // Bethesda stream version

    for (uint32_t i = 0; i < 3; i++)          // author, process, export
      header.skip(header.get<uint8_t>());

    uint16_t typeCount = header.get<uint16_t>();
    std::vector<std::string> types;

    for (uint16_t i = 0; i < typeCount && !header.failed(); i++)
      types.push_back(header.bytes(header.get<uint32_t>()));

    std::vector<uint16_t> blockTypes(blockCount);

    for (auto& type : blockTypes)
      type = header.get<uint16_t>();

    std::vector<uint32_t> blockSizes(blockCount);

    for (auto& blockSize : blockSizes)
      blockSize = header.get<uint32_t>();

    uint32_t stringCount = header.get<uint32_t>();
    header.get<uint32_t>();                   // longest string

    std::vector<std::string> strings;

    for (uint32_t i = 0; i < stringCount && !header.failed(); i++)
      strings.push_back(header.bytes(header.get<uint32_t>()));

    header.skip(size_t(header.get<uint32_t>()) * 4);

    if (header.failed())
      return false;

    std::vector<Block> blocks(blockCount);
    size_t offset = header.position();

    for (uint32_t i = 0; i < blockCount; i++) {
      if (blockTypes[i] >= types.size() || offset + blockSizes[i] > size)
        return false;

      blocks[i] = { types[blockTypes[i]], offset, blockSizes[i] };
      offset += blockSizes[i];
    }

    auto string = [&strings] (int32_t index) {
      return index >= 0 && uint32_t(index) < strings.size() ? strings[index] : std::string();
    };

    auto block = [&blocks] (int32_t index, const char* type) -> const Block* {
      return index >= 0 && uint32_t(index) < blocks.size() && blocks[index].type == type
        ? &blocks[index] : nullptr;
    };

    for (const Block& sequence : blocks) {
      if (sequence.type != "NiControllerSequence" && sequence.type != "BSAnimGroupSequence")
        continue;

      Reader reader(data, sequence.offset + sequence.size, sequence.offset);
      reader.get<int32_t>();                  // name
      uint32_t controlled = reader.get<uint32_t>();
      reader.get<uint32_t>();                 // array grow by

      for (uint32_t i = 0; i < controlled && !reader.failed(); i++) {
        size_t start = reader.position();
        int32_t interpolator = reader.get<int32_t>();
        reader.get<int32_t>();                // controller
        reader.get<uint8_t>();                // priority
        std::string name = string(reader.get<int32_t>());
        reader.skip(start + ControlledBlockSize - reader.position());

        const Block* source = block(interpolator, "NiTransformInterpolator");

        if (!source || name.empty())
          continue;

        Reader values(data, source->offset + source->size, source->offset);
        VrKfTrack track;
        track.name = name;

        VrVector3 translate;
        translate.x = values.get<float>();
        translate.y = values.get<float>();
        translate.z = values.get<float>();

        float w = values.get<float>();
        float x = values.get<float>();
        float y = values.get<float>();
        float z = values.get<float>();
        float scale = values.get<float>();
        int32_t keys = values.get<int32_t>();

        // Values the interpolator does not have are written as huge floats
        constexpr float Invalid = 3.0e38f;
        track.hasTranslation = std::fabs(translate.x) < Invalid;
        track.hasRotation    = std::fabs(w) < Invalid;
        track.hasScale       = std::fabs(scale) < Invalid;

        track.rest.translate = track.hasTranslation ? translate : VrVector3();
        track.rest.rotate    = track.hasRotation ? quaternionRotation(w, x, y, z) : VrGameRotation();
        track.rest.scale     = track.hasScale ? scale : 1.0f;

        if (const Block* transformData = block(keys, "NiTransformData")) {
          Reader keyReader(data, transformData->offset + transformData->size, transformData->offset);

          if (!readTransformData(keyReader, track))
            continue;

          track.hasRotation    |= !track.rotations.empty() || track.euler;
          track.hasTranslation |= !track.translations.empty();
          track.hasScale       |= !track.scales.empty();
        }

        m_tracks.push_back(std::move(track));
      }

      reader.get<float>();                    // weight
      int32_t textKeys = reader.get<int32_t>();
      reader.get<uint32_t>();                 // cycle type
      reader.get<float>();                    // frequency
      float start = reader.get<float>();
      float stop  = reader.get<float>();
      m_length = !reader.failed() && stop > start ? stop - start : 0.0f;

      if (const Block* text = block(textKeys, "NiTextKeyExtraData")) {
        Reader keyReader(data, text->offset + text->size, text->offset);
        keyReader.get<int32_t>();             // name
        uint32_t count = keyReader.get<uint32_t>();

        for (uint32_t i = 0; i < count && !keyReader.failed(); i++) {
          VrKfTextKey key;
          key.time = keyReader.get<float>();
          key.text = string(keyReader.get<int32_t>());

          if (!keyReader.failed())
            m_textKeys.push_back(std::move(key));
        }
      }

      break;
    }

    // Without a length, the last text key ends the animation
    if (m_length <= 0.0f) {
      for (const VrKfTextKey& key : m_textKeys)
        m_length = std::max(m_length, key.time);
    }

    return !m_tracks.empty();
  }


  const VrKfTrack* VrKfAnimation::track(const std::string& name) const {
    for (const VrKfTrack& t : m_tracks) {
      if (t.name == name)
        return &t;
    }

    return nullptr;
  }


  VrSequencePose VrKfAnimation::sample(const VrKfTrack& track, float time) {
    VrSequencePose pose = track.rest;
    size_t index;
    float weight;

    if (track.euler) {
      pose.rotate = vrKfEulerRotation(
        sampleFloat(track.angles[0], time, 0.0f),
        sampleFloat(track.angles[1], time, 0.0f),
        sampleFloat(track.angles[2], time, 0.0f));
    } else if (bracket(track.rotations, time, index, weight)) {
      if (weight <= 0.0f) {
        pose.rotate = track.rotations[index].value;
      } else {
        VrQuaternion a = vrQuaternionFromGameRotation(track.rotations[index].value);
        VrQuaternion b = vrQuaternionFromGameRotation(track.rotations[index + 1].value);
        pose.rotate = vrGameRotationFromQuaternion(vrSlerp(a, b, weight));
      }
    }

    if (bracket(track.translations, time, index, weight)) {
      pose.translate = track.translations[index].value;

      if (weight > 0.0f) {
        const VrVector3& next = track.translations[index + 1].value;
        pose.translate.x += (next.x - pose.translate.x) * weight;
        pose.translate.y += (next.y - pose.translate.y) * weight;
        pose.translate.z += (next.z - pose.translate.z) * weight;
      }
    }

    pose.scale = sampleFloat(track.scales, time, track.rest.scale);
    return pose;
  }

}
