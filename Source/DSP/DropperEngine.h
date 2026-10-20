#pragma once

#include "SeededRng.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace dropper {

// Musical model of the Blooper Dropper: a sample-accurate gain envelope made of
// overlapping dropout events. This is an interpretation of the pedal's behavior,
// not a claim about Chase Bliss's implementation.
//
// The schedule is a pure function of (parameters, seed, musical time). The UI
// and the audio thread both call scheduleGain(), so the display is the envelope
// the processor applies.

inline constexpr std::uint64_t kDefaultSeed = 0x5EEDD20BULL;
inline constexpr int kRegenerateController = 20;
inline constexpr float kCenterDetent = 0.02f;

// Wow/flutter reads one modulated delay centered on this offset. Amount changes
// how far that single tap wanders. It does not mix a second copy against it.
// While the amount is above 0 the processor reports this delay as latency.
// At 0%, and while Generational Wear is the only active path, the live signal
// stays un-delayed and reported latency is 0. Engaging crossfades onto the delay.
inline constexpr double kStabilityDelaySec = 0.008;

// Generational Wear is a recirculating buffer, not a looper overdub. Each trip
// around this loop reprints the stored audio with more filtering, saturation,
// and (when noise is enabled) hiss, then blends it back with the live input.
// Reset copies the clean input history into that buffer and clears the count.
// Feedback gain stays below unity so the loop cannot run away.
inline constexpr double kGenerationLoopSec = 0.180;

enum class TimeUnit : int { Seconds = 0, Beats = 1 };

enum class DegradeCurve : int {
    SlowBurn = 0,
    Steady = 1,
    Rapid = 2,
    Unstable = 3
};

// Progressive wear is a playback-time state, not a function of the waveform.
// `manual` replaces the automatic target with `manualLevel` (scaled by the
// ceiling). Elapsed time keeps advancing, so releasing manual resumes the
// curve. Recovery slews downward when that target falls. Reset snaps to the
// initial condition immediately and does not use the recovery slew.
struct Degradation {
    bool enabled = false;
    float ceiling = 0.8f;
    float initial = 0.f;
    float timeSec = 60.f;
    float recovery = 0.35f;
    int curve = static_cast<int>(DegradeCurve::Steady);
    bool hold = false;
    bool manual = false;
    float manualLevel = 0.f;
};

// Instantaneous tape instability. Independent of the dropout envelope and of
// the Destroy timeline. `amount` 0 bypasses the live processors. Generational
// wear is the cumulative path and does not move `amount` by itself.
struct Stability {
    float amount = 0.f;
    float wow = 0.6f;
    float flutter = 0.f;
    bool noiseEnabled = true;
    float noise = 0.35f;
    float freqLoss = 0.4f;
    float freqLossMax = 1.f;
    float amplitude = 0.3f;
    bool saturation = true;
    bool generational = false;
    float wearAmount = 0.45f;
    float retention = 0.65f;
    int generations = 8; // 0 = unlimited
};

struct Parameters {
    float dropper = 0.f;            // 0..1, 0 is bypass
    float density = 1.f;            // 0..1 scales the macro
    float minDurationSec = 0.003f;  // floor of the event distribution
    float maxDurationSec = 0.080f;  // ceiling of the event distribution
    float fadeSec = 0.008f;
    float randomness = 0.5f;        // Drift. 0 = even placement, 1 = free placement
    float patternBars = 1.f;        // repeatable-mode loop length
    float mix = 1.f;                // 1 = full dropout, 0 = dry
    int timingSync = 0;             // 0 free-running, 1 host tempo
    int subdivision = 3;            // index into subdivisionBeats()
    int patternMode = 0;            // 0 variable, 1 constant looping pattern
    Degradation degrade {};
    Stability stability {};
};

struct Transport {
    double sampleRate = 48000.0;
    double bpm = 120.0;
    double ppqPosition = 0.0;
    double ppqLoopStart = 0.0;
    double ppqLoopEnd = 0.0;
    bool hasHostPosition = false;
    bool isPlaying = false;
    bool isLooping = false;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
};

struct ScheduleContext {
    Parameters params {};
    std::uint64_t seed = kDefaultSeed;
    double bpm = 120.0;
    double beatsPerBar = 4.0;
    double sampleRate = 48000.0;
    // Applied severity, 0..1, already limited by the ceiling. 0 leaves the
    // original Dropper schedule unchanged.
    float degradeLevel = 0.f;
};

struct Snapshot {
    double playhead = 0.0;
    double bpm = 120.0;
    double beatsPerBar = 4.0;
    double sampleRate = 48000.0;
    double patternLength = 2.0;
    float outputGain = 1.f;
    std::uint64_t seed = kDefaultSeed;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    TimeUnit unit = TimeUnit::Seconds;
    bool repeatable = false;
    bool bypassed = true;
    bool valid = false;
    float degradation = 0.f;
    float degradationCeiling = 0.f;
    double degradationElapsed = 0.0;
    double degradationRemaining = 0.0;
    bool degradationEnabled = false;
    bool degradationHeld = false;
    int generation = 0;
    bool generational = false;
};

// Modulation, noise generator, and generational buffers. Restored as a unit so
// a block boundary or a UI repaint does not rewind the tape.
struct StabilityState {
    float wowPhase = 0.f;
    float flutterPhase = 0.f;
    float drift = 0.f;
    float driftTarget = 0.f;
    float ampPhase = 0.f;
    float amountSmoothed = 0.f;
    float engage = 0.f;
    float toneLp[2] {};
    float noiseHpIn = 0.f;
    float noiseHpOut = 0.f;
    float noiseLp = 0.f;
    float crackleEnv = 0.f;
    float burstEnv = 0.f;
    float wanderRate = 0.4f;
    float wanderRateTarget = 0.4f;
    float wanderPhase2 = 0.f;
    float lossWander = 0.f;
    float lossWanderTarget = 0.f;
    float hissBreath = 0.8f;
    float hissBreathTarget = 0.8f;
    float genLp[2] {};
    float genDcX[2] {};
    float genDcY[2] {};
    std::uint64_t noiseState = 1;
    std::uint64_t sampleCount = 0;
    int driftHold = 0;
    int burstLeft = 0;
    int wanderHold = 0;
    int lossHold = 0;
    int hissHold = 0;
    int crackleWait = 1;
    int genPos = 0;
    int genCount = 0;
    int genLength = 0;
    int modWrite = 0;
    double sampleRate = 0.0;
    std::vector<float> loop;
    std::vector<float> clean;
    std::vector<float> modDelay;
};

struct DegradationState {
    double elapsed = 0.0;
    float level = 0.f;
};

// Automatic target in 0..ceiling for this elapsed playback time. Deterministic
// in (settings, elapsed, seed). Does not include the recovery slew.
float degradationTarget(const Degradation& settings, double elapsedSec, std::uint64_t seed) noexcept;

double subdivisionBeats(int index) noexcept;
double beatsPerBarFromSignature(int numerator, int denominator) noexcept;
double sanitizeBpm(double bpm) noexcept;

ScheduleContext contextFor(const Parameters& params, const Transport& transport, std::uint64_t seed) noexcept;

struct ScheduleGeometry {
    double patternLength = 1.0;
    TimeUnit unit = TimeUnit::Seconds;
    bool bypass = true;
    bool repeatable = false;
};

ScheduleGeometry geometryFor(const ScheduleContext& ctx) noexcept;

// Gain at `time` in seconds (free-running) or quarter notes (tempo sync).
// 1 = signal present, 0 = full dropout. Overlapping events combine by minimum.
float scheduleGain(const ScheduleContext& ctx, double time) noexcept;

class Engine {
public:
    Engine();

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void setSeed(std::uint64_t seed);
    std::uint64_t getSeed() const noexcept;
    void regenerate();
    void requestRegenerate();
    void requestDegradeReset();
    void requestGenerationReset();
    DegradationState degradationState() const;
    void restoreDegradationState(DegradationState state);
    StabilityState stabilityState() const;
    void restoreStabilityState(const StabilityState& state);
    int generationCount() const noexcept { return genCount_; }
    int stabilityLatencySamples(float amount) const noexcept;

    // gainOut, when non-null, receives the dropout gain before the wet/dry mix.
    // clickProtection slews transport jumps. Dropout edges are rounded cosines;
    // fade == 0 is the fastest tape shoulder, not a hard cut.
    void process(float* const* channels, int numChannels, int numSamples,
                 const Parameters& params, const Transport& transport,
                 float* gainOut = nullptr, bool clickProtection = true);

    Snapshot snapshot() const;

private:
    struct Published {
        Snapshot snap {};
    };

    bool tryLock() const noexcept;
    void unlock() const noexcept;

    double sampleRate_ = 48000.0;
    double freeSamples_ = 0.0;
    double freeBeats_ = 0.0;
    double expectedCursor_ = 0.0;
    bool haveCursor_ = false;
    int lastSync_ = -1;

    float lastGain_ = 1.f;
    int slewLeft_ = 0;
    int slewTotal_ = 1;
    float slewAnchor_ = 1.f;

    std::atomic<std::uint64_t> seed_ { kDefaultSeed };
    std::atomic<int> pendingRegens_ { 0 };
    std::atomic<int> pendingDegradeResets_ { 0 };
    std::atomic<bool> discontinuity_ { false };
    std::atomic<double> degradeElapsedPub_ { 0.0 };
    std::atomic<float> degradeLevelPub_ { 0.f };
    std::atomic<bool> degradeRestore_ { false };

    double degradeElapsed_ = 0.0;
    float degradeLevel_ = 0.f;

    static constexpr int kModDelay = 8192;
    float modDelay_[2][kModDelay] {};
    int modWrite_ = 0;
    float wowPhase_ = 0.f;
    float flutterPhase_ = 0.f;
    float drift_ = 0.f;
    float driftTarget_ = 0.f;
    int driftHold_ = 0;
    float ampPhase_ = 0.f;
    float amountSmoothed_ = 0.f;
    float engage_ = 0.f;
    float toneLp_[2] {};
    float noiseHpIn_ = 0.f;
    float noiseHpOut_ = 0.f;
    float noiseLp_ = 0.f;
    float crackleEnv_ = 0.f;
    float burstEnv_ = 0.f;
    int burstLeft_ = 0;
    float wanderRate_ = 0.4f;
    float wanderRateTarget_ = 0.4f;
    int wanderHold_ = 0;
    float wanderPhase2_ = 0.f;
    float lossWander_ = 0.f;
    float lossWanderTarget_ = 0.f;
    int lossHold_ = 0;
    float hissBreath_ = 0.8f;
    float hissBreathTarget_ = 0.8f;
    int hissHold_ = 0;
    int crackleWait_ = 1;
    std::uint64_t noiseState_ = 1;
    std::uint64_t stabilitySamples_ = 0;

    std::vector<float> genLoop_;
    std::vector<float> genClean_;
    int genCapacity_ = 0;
    int genLength_ = 0;
    int genPos_ = 0;
    int genCount_ = 0;
    float genLp_[2] {};
    float genDcX_[2] {};
    float genDcY_[2] {};
    std::atomic<int> pendingGenResets_ { 0 };

    Published published_ {};
    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;

    void clearStability();
    float nextNoise() noexcept;
    void applyStability(float* const* channels, int numChannels, int index, const Stability& stability,
                        std::uint64_t seed, double sampleRate, float dropout) noexcept;
};

} // namespace dropper
