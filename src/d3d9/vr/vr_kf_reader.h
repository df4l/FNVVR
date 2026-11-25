#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vr_game_sequence.h"

namespace dxvk {

  /**
   * \brief A text key of an animation file
   */
  struct VrKfTextKey {
    float       time = 0.0f;
    std::string text;
  };

  /**
   * \brief The transform keys of one node in an animation file
   *
   * Rotations are kept as quaternion keys, or as one list of angle keys
   * per axis (XYZ rotation keys). A track without keys of a kind holds the
   * interpolator's own value of that kind.
   */
  struct VrKfTrack {
    struct RotationKey {
      float          time = 0.0f;
      VrGameRotation value;
    };

    struct VectorKey {
      float     time = 0.0f;
      VrVector3 value;
    };

    struct FloatKey {
      float time  = 0.0f;
      float value = 0.0f;
    };

    std::string               name;
    VrSequencePose            rest;
    bool                      hasRotation    = false;
    bool                      hasTranslation = false;
    bool                      hasScale       = false;
    std::vector<RotationKey>  rotations;
    std::vector<FloatKey>     angles[3];
    bool                      euler          = false;
    std::vector<VectorKey>    translations;
    std::vector<FloatKey>     scales;
  };

  /**
   * \brief An animation file (.kf) read without the game
   *
   * Reads the first controller sequence of a Gamebryo 20.2.0.7 file as the
   * game's animations are written: transform interpolators with their
   * keys, and the text keys. Other interpolators are left out. Keys are
   * interpolated linearly (quaternions spherically), the tangents of
   * quadratic and TBC keys are not used. See findings/consumables.md §3.
   */
  class VrKfAnimation {

  public:

    /**
     * \brief Reads an animation file
     *
     * \param [in] data The file's content
     * \param [in] size Its size in bytes
     * \returns \c false if the file is not an animation this reads
     */
    bool read(const uint8_t* data, size_t size);

    /**
     * \brief The track of a node, \c nullptr if the animation has none
     */
    const VrKfTrack* track(const std::string& name) const;

    /**
     * \brief A node's transform in its parent at a time
     *
     * \param [in] track The node's track
     * \param [in] time Seconds from the animation's start
     */
    static VrSequencePose sample(const VrKfTrack& track, float time);

    const std::vector<VrKfTrack>& tracks() const {
      return m_tracks;
    }

    const std::vector<VrKfTextKey>& textKeys() const {
      return m_textKeys;
    }

    /// The animation's length in seconds
    float length() const {
      return m_length;
    }

  private:

    std::vector<VrKfTrack>   m_tracks;
    std::vector<VrKfTextKey> m_textKeys;
    float                    m_length = 0.0f;

  };

  /**
   * \brief The rotation of XYZ angle keys, as the game builds it
   *
   * NiMatrix3::FromEulerAnglesXYZ (0x00A59540): the X, then Y, then Z
   * rotations, in the game's layout.
   */
  VrGameRotation vrKfEulerRotation(float x, float y, float z);

}
