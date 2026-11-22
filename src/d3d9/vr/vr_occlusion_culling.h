#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "../d3d9_include.h"

#include "vr_occlusion_rules.h"

namespace dxvk {

  /**
   * \brief When occlusion culling runs (\c d3d9.vrOcclusionCulling)
   */
  enum class VrOcclusionUse {
    /// While the performance mode is on
    Performance,
    /// Always
    Always,
    /// Never
    Never,
  };

  /**
   * \brief Leaves out of each eye's view what the GPU proved hidden
   *
   * While an eye's scene is gathered, every large object, actor, door,
   * tree, land quadrant, terrain LOD chunk and grass group the game finds
   * in the view is a candidate. Right after the eye's opaque pass, when its
   * depth buffer holds the finished scene, the box of each candidate due a
   * test is drawn there inside an occlusion query, with colour and depth
   * writes off. The answers are read in the same eye's next frame without
   * waiting. A candidate is left out of that eye's view (its OnVisible is
   * skipped, as the culler does with what lies outside the view) only when
   * its last two answers drew no pixel, the newest is at most a few frames
   * old and the eye has not turned or moved much since. Shadows,
   * reflections and the other eye are not affected.
   *
   * Each eye keeps its own answers: the eyes see around edges differently.
   * Covered candidates are tested every frame, so they come back the frame
   * after their box shows a pixel; while the eye moves, their boxes grow by
   * how far the move slides a near edge, so they come back a frame early.
   *
   * After mrfury97's FNV Occlusion Culling (GPL-3, ideas only, no code),
   * findings/performance.md §17. The plugin itself is left to work alone
   * when it is installed.
   *
   * Only used on the game's main thread.
   */
  class VrOcclusionCulling {

  public:

    static constexpr uint32_t EyeCount = 2;

    ~VrOcclusionCulling();

    VrOcclusionCulling             (const VrOcclusionCulling&) = delete;
    VrOcclusionCulling& operator = (const VrOcclusionCulling&) = delete;

    /**
     * \brief Redirects the game's calls
     * \returns \c nullptr if the executable is not the supported version
     *    or the FNV Occlusion Culling plugin is loaded
     */
    static std::unique_ptr<VrOcclusionCulling> install(IDirect3DDevice9* device);

    /**
     * \brief Turns the culling on or off
     *
     * Off, every call goes through to the game and the queries are freed.
     */
    void setEnabled(bool enabled);

    /**
     * \brief Starts an eye's world passes
     * \param [in] eye 0 for the left eye, 1 for the right eye
     */
    void beginWorld(uint32_t eye);

    /**
     * \brief Ends the world passes started by beginWorld
     */
    void endWorld();

    /**
     * \brief Ends a frame
     */
    void endFrame();

  private:

    enum class Kind : uint8_t {
      Object,
      Tree,
      Actor,
      Activator,
      Land,
    };

    struct EyeState {
      IDirect3DQuery9* query        = nullptr;
      bool             pending      = false;
      bool             answered     = false;
      uint32_t         tested       = 0;
      uint32_t         answerFrame  = 0;
      uint8_t          zeroStreak   = 0;
      // The pending query's box was grown for the eye's move; a grown box
      // that shows pixels is tested again the next frame and one answer
      // without pixels gives back the streak it had
      bool             testedGrown  = false;
      bool             afterGrown   = false;
      uint8_t          streakBefore = 0;
      VrOcclusionBox   testedBox;
      VrOcclusionBox   answeredBox;
      // Blinking guard
      uint32_t         hiddenFrame  = 0;
      uint32_t         hideStart    = 0;
      uint32_t         flapFrame    = 0;
      uint8_t          flaps        = 0;
      bool             sawPixels    = false;
    };

    struct Record {
      Kind           kind       = Kind::Object;
      uint32_t       formId     = 0;
      float          key[3]     = { };
      float          scale      = 0.0f;
      VrOcclusionBox box;
      uint16_t       shapes     = 0;
      uint32_t       lastSeen   = 0;
      uint32_t       boxFrame   = 0;
      bool           unstable   = false;
      EyeState       eyes[EyeCount];
    };

    struct Candidate {
      Record* record;
      bool    hidden;
    };

    struct CameraRecord {
      uint32_t frame = 0;
      float    position[3] = { };
      float    rotation[9] = { };
    };

    struct Stats {
      uint32_t frames    = 0;
      uint64_t candidates = 0;
      uint64_t hidden     = 0;
      uint64_t hiddenShapes = 0;
      uint64_t tested     = 0;
      uint64_t covered    = 0;
      uint64_t untestable = 0;
      uint64_t notReady   = 0;
      uint64_t unstable   = 0;
      uint64_t guarded    = 0;
      uint64_t kinds[5]   = { };
      double   costUs     = 0.0;
    };

    static constexpr uint32_t CameraRing = 16;

    explicit VrOcclusionCulling(IDirect3DDevice9* device);

    IDirect3DDevice9* m_device;

    bool      m_enabled     = false;
    bool      m_supported   = true;
    bool      m_checked     = false;
    uint32_t  m_frame       = 1;
    uint32_t  m_eye         = ~0u;
    uint32_t  m_thread      = 0;
    bool      m_gathering   = false;
    void*     m_culler      = nullptr;
    void*     m_camera      = nullptr;
    uint32_t  m_depth       = 0;
    float     m_eyePosition[3] = { };

    std::unordered_map<void*, Record> m_records;
    std::vector<Record*>              m_pending[EyeCount];
    std::vector<Candidate>            m_candidates;
    std::vector<IDirect3DQuery9*>     m_freeQueries;
    std::vector<VrOcclusionTurn>      m_turns;
    CameraRecord                      m_cameras[EyeCount][CameraRing];
    bool                              m_guard[CameraRing] = { };

    Stats     m_stats;
    double    m_statsMs  = 0.0;
    uint64_t  m_lastFrameTicks = 0;

    bool active() const;

    void gatherBegin(void* camera, void* culler);

    void gatherEnd();

    void readAnswers(uint32_t eye);

    void testCandidates(void* camera);

    bool readView(void* renderer, void* camera, VrOcclusionView& view) const;

    bool collect(void* node, Kind hint);

    bool collectMultiBound(void* node, void* culler);

    Record& record(void* node);

    bool decide(Record& record, bool moving, const VrOcclusionBox* current);

    bool notBlinking(Record& record, EyeState& state, bool hide);

    void resetEye(EyeState& state);

    void releaseAll();

    IDirect3DQuery9* acquireQuery();

    void logStats();

    static void __cdecl accumulateHook(void* camera, void* scene, void* culler);

    static void __cdecl opaqueHook(void* camera, void* accumulator, uint32_t flag);

    static void __fastcall fadeOnVisibleHook(void* node, void* unused, void* culler);

    static void __fastcall treeOnVisibleHook(void* node, void* unused, void* culler);

    static void __fastcall multiBoundOnVisibleHook(void* node, void* unused, void* culler);

  };

}
