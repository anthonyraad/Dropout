#include "DSP/DropperEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dropper {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxFragments = 2;
// Shoulder used when the hole is long enough to hold it. A vertical cut clicks.
constexpr double kMinEdgeSec = 0.003;
// Short stutter holes keep a shorter cosine so the mute in the middle survives.
constexpr double kStitchSec = 0.0008;

struct Event {
    double start = 0.0;
    double duration = 0.0;
    double fadeIn = 0.0;
    double fadeOut = 0.0;
    float depth = 1.f; // 1 = full mute. Lower leaves a little signal in the dip.
    float curve = 1.f; // 1 = raised cosine. Other values lean the shoulder.
};

struct Effective {
    bool bypass = true;
    bool wrap = false;
    TimeUnit unit = TimeUnit::Seconds;
    double cell = 0.05;
    double patternLength = 2.0;
    int nCells = 1;
    double p = 0.0;
    int fragments = 1;
    double minDur = 0.003;
    double maxDur = 0.08;     // longest event this setting can emit
    double maxDurEff = 0.08;  // intensity-shaped ceiling
    double fade = 0.008;      // in time units
    double randomness = 0.0;
    double amount = 0.0;
    double degrade = 0.0;
    double fadeChaos = 0.0;
    float mix = 1.f;
    float maxDelta = 1.f;
    std::uint64_t seed = kDefaultSeed;
    double bpm = 120.0;
};

double clampd(double v, double lo, double hi) noexcept {
    return std::max(lo, std::min(hi, v));
}

float clampf(float v, float lo, float hi) noexcept {
    return std::max(lo, std::min(hi, v));
}

double lerp(double a, double b, double t) noexcept {
    return a + (b - a) * t;
}

double wrapPositive(double x, double length) noexcept {
    if (!(length > 0.0))
        return 0.0;
    double m = std::fmod(x, length);
    if (m < 0.0)
        m += length;
    return m;
}

double secToUnit(double seconds, const Effective& e) noexcept {
    if (e.unit == TimeUnit::Beats)
        return seconds * e.bpm / 60.0;
    return seconds;
}

void applyDegradation(Effective& e, float severity) noexcept {
    const double s = clampd(severity, 0.0, 1.0);
    if (!(s > 0.004))
        return;

    // The holes already chosen by the dropout control stay where they are.
    // Wear lengthens those holes and pulls the shoulders tighter. The shoulder
    // stays a rounded tape edge; it is never collapsed into a vertical cut.
    e.degrade = s;
    e.fadeChaos = std::pow(s, 0.85);
    e.fade *= lerp(1.0, 0.2, s);
    const double reach = std::max(e.maxDur * lerp(1.0, 1.2, s), e.maxDurEff * lerp(1.0, 2.15, s));
    e.maxDur = std::max(e.maxDur, reach);
}

Effective makeEffective(const ScheduleContext& ctx) noexcept {
    Effective e;
    const Parameters& p = ctx.params;

    e.seed = ctx.seed == 0 ? kDefaultSeed : ctx.seed;
    e.bpm = sanitizeBpm(ctx.bpm);
    e.mix = clampf(p.mix, 0.f, 1.f);
    e.unit = p.timingSync != 0 ? TimeUnit::Beats : TimeUnit::Seconds;
    e.wrap = p.patternMode != 0;

    const double beatsPerBar = ctx.beatsPerBar > 0.0 ? ctx.beatsPerBar : 4.0;
    const double bars = clampd(p.patternBars, 1.0, 4.0);
    if (e.unit == TimeUnit::Beats)
        e.patternLength = bars * beatsPerBar;
    else
        e.patternLength = bars * beatsPerBar * 60.0 / e.bpm;
    e.patternLength = std::max(e.patternLength, 1.0e-3);

    const double amount = clampd(p.dropper, 0.0, 1.0);
    const double density = clampd(p.density, 0.0, 1.0);
    e.amount = amount;

    const double sampleRate = ctx.sampleRate > 1000.0 ? ctx.sampleRate : 48000.0;
    const float degNow = p.degrade.enabled ? clampf(ctx.degradeLevel, 0.f, 1.f) : 0.f;
    const bool macroDry = amount < static_cast<double>(kCenterDetent) || density <= 0.0;
    if (macroDry) {
        e.bypass = true;
        // Entering the dry end ramps back to unity instead of clicking.
        e.maxDelta = static_cast<float>(std::min(1.0, 1.0 / std::max(1.0, 0.004 * sampleRate)));
        return e;
    }

    const double intensity = amount;
    const double densityDrive = density;
    e.amount = intensity;
    e.bypass = false;

    e.randomness = clampd(p.randomness, 0.0, 1.0);

    // A worn tape loop picks up a few more dead spots. The chance rises, and the
    // free-running gap tightens a little. Synced playback stays on the note grid.
    const double activity = intensity * densityDrive;
    const double curve = std::pow(activity, 1.05);

    const double baseSec = p.timingSync != 0
                               ? subdivisionBeats(p.subdivision) * 60.0 / e.bpm
                               : 0.190;
    double desiredSec = baseSec;
    if (p.timingSync == 0) {
        const double closerSec = lerp(0.200, 0.135, intensity);
        desiredSec = lerp(baseSec, closerSec, std::pow(intensity, 0.8));
    }
    desiredSec = clampd(desiredSec, 0.040, 2.0);

    double desiredCell = e.unit == TimeUnit::Beats ? desiredSec * e.bpm / 60.0 : desiredSec;
    desiredCell = std::max(desiredCell, 1.0e-4);

    if (e.wrap) {
        e.nCells = std::max(1, static_cast<int>(std::lround(e.patternLength / desiredCell)));
        e.cell = e.patternLength / static_cast<double>(e.nCells);
    } else {
        e.nCells = 1;
        e.cell = desiredCell;
    }

    double minSec = clampd(p.minDurationSec, 0.0005, 0.050);
    double maxSec = clampd(p.maxDurationSec, 0.001, 0.500);
    if (maxSec < minSec)
        std::swap(minSec, maxSec);

    // Dropout opens the hole. Low settings stay near the short end of Min/Max.
    // High settings reach the long end and stretch a little past Max. The cell
    // still has to hold the hole, so a longer dropout stays one event.
    const double span = std::max(0.0, maxSec - minSec);
    const double opened = std::min(0.500, maxSec + span * lerp(0.20, 1.7, intensity));
    const double cellSecNow = e.unit == TimeUnit::Beats ? e.cell * 60.0 / e.bpm : e.cell;
    const double cellSecLimit = cellSecNow * 0.86;
    const double toward = lerp(minSec + span * 0.18, opened, std::pow(intensity, 0.65));
    const double maxEffSec = std::min(std::max(minSec, toward), std::max(minSec, cellSecLimit));
    e.minDur = secToUnit(minSec, e);
    e.maxDur = secToUnit(maxSec, e);
    e.maxDurEff = secToUnit(maxEffSec, e);
    e.maxDur = std::max(e.maxDur, e.maxDurEff);

    // The Fade knob is the starting shoulder. Dropout stretches it, and adds a
    // slow floor of its own, so the tape loses and finds the signal gradually.
    double fadeSec = clampd(p.fadeSec, 0.0, 0.200);
    const double stretch = lerp(1.0, 2.6, std::pow(intensity, 0.75));
    const double floorFade = lerp(0.0, 0.055, std::pow(intensity, 0.9));
    fadeSec = std::min(0.160, std::max(fadeSec * stretch, floorFade));
    e.fade = secToUnit(fadeSec, e);

    e.fragments = 1;

    e.p = clampd(lerp(0.20, 0.74, curve), 0.0, 0.97);

    const double edgeSamples = std::max(1.0, kStitchSec * sampleRate);
    e.maxDelta = static_cast<float>(std::min(1.0, 1.35 * (kPi * 0.5) / edgeSamples));

    if (degNow > 0.004f)
        applyDegradation(e, degNow);
    return e;
}

int wrapCell(const Effective& e, std::int64_t cell) noexcept {
    if (!e.wrap || e.nCells <= 0)
        return static_cast<int>(cell);
    std::int64_t n = e.nCells;
    std::int64_t m = cell % n;
    if (m < 0)
        m += n;
    return static_cast<int>(m);
}

int makeEvents(std::int64_t cellIndex, const Effective& e, Event* out, int maxOut) noexcept {
    if (e.bypass || maxOut <= 0)
        return 0;

    const std::int64_t stored = e.wrap ? wrapCell(e, cellIndex) : cellIndex;
    const auto key = static_cast<std::uint64_t>(stored);
    const double cellStart = static_cast<double>(stored) * e.cell;

    int written = 0;
    const int fragments = std::max(1, std::min(e.fragments, kMaxFragments));
    for (int f = 0; f < fragments && written < maxOut; ++f) {
        const double gate = hash01(e.seed, key, static_cast<std::uint64_t>(10 + f));
        const double thresh = std::min(0.97, e.p * (f == 0 ? 1.0 : (0.55 + 0.45 * e.amount)));
        const int stride = std::max(1, static_cast<int>(std::lround(1.0 / std::max(thresh, 0.05))));
        std::int64_t phase = stored % stride;
        if (phase < 0)
            phase += stride;
        const bool onGrid = phase == 0;
        // Low randomness locks events to a steady grid. High randomness uses the hash gate.
        const double gridWeight = onGrid ? 1.0 : 0.0;
        const double probability = lerp(gridWeight, thresh, clampd(e.randomness, 0.0, 1.0));
        if (f > 0 && e.randomness < 0.2 && e.amount < 0.85)
            continue;
        if (gate >= probability)
            continue;

        const double uDur = hash01(e.seed, key, static_cast<std::uint64_t>(30 + f));
        const double uPos = hash01(e.seed, key, static_cast<std::uint64_t>(50 + f));
        const double uFi = hash01(e.seed, key, static_cast<std::uint64_t>(70 + f));
        const double uFo = hash01(e.seed, key, static_cast<std::uint64_t>(90 + f));

        const double shape = lerp(1.25, 0.58, e.amount);
        double shaped = std::pow(clampd(uDur, 0.0, 1.0), shape);
        // Low drift keeps hole lengths similar, around the longer size Dropout opens up.
        // The curve opens up through the middle of the knob so Fixed dips start to differ
        // before Drift is all the way up.
        const double drift = clampd(e.randomness, 0.0, 1.0);
        const double driftVis = std::pow(drift, 0.6);
        const double evenTarget = lerp(0.28, 0.84, e.amount);
        shaped = lerp(shaped, evenTarget, (1.0 - driftVis) * 0.85);
        double dur = e.minDur + (e.maxDurEff - e.minDur) * shaped;
        dur = clampd(dur, std::max(1.0e-5, e.minDur * 0.5), std::max(e.maxDurEff, e.minDur));
        if (e.degrade > 0.0) {
            const double grown = dur * lerp(1.0, 2.15, std::pow(e.degrade, 0.9));
            const double ceiling = std::max(dur, e.maxDur * lerp(1.0, 1.2, e.degrade));
            dur = std::min(std::max(dur, grown), ceiling);
        }
        if (e.wrap)
            dur = std::min(dur, e.patternLength * 0.92);

        // Drift adds a seeded difference to each dip. Fixed mode repeats the same
        // differences because the hash is tied to the looped cell. The spread is wide
        // enough to see: width, depth, and where the dip sits in its slot.
        const double uSize = hash01(e.seed, key, static_cast<std::uint64_t>(130 + f));
        const double uShape = hash01(e.seed, key, static_cast<std::uint64_t>(150 + f));
        const double uDepth = hash01(e.seed, key, static_cast<std::uint64_t>(170 + f));
        dur *= lerp(1.0, lerp(0.55, 1.4, uSize), driftVis);
        const double baseCap = std::max(e.maxDurEff, e.minDur);
        double cap = baseCap * lerp(1.0, 1.28, driftVis);
        if (e.wrap)
            cap = std::min(cap, e.cell * lerp(0.86, 0.58, driftVis));
        cap = std::min(cap, std::max(e.patternLength, e.minDur) * 0.92);
        dur = clampd(dur, std::max(1.0e-5, e.minDur * 0.35), std::max(cap, e.minDur));
        if (e.wrap)
            dur = std::min(dur, e.patternLength * 0.92);

        // Worn tape doesn't stretch every dropout the same way. A seeded share of
        // the holes that are already there grow a little further as Destroy rises.
        // Fixed mode repeats the same ones because the hash is tied to the cell.
        if (e.degrade > 0.02) {
            const double wear = clampd(e.degrade, 0.0, 1.0);
            const double pick = hash01(e.seed, key, static_cast<std::uint64_t>(190 + f));
            const double chance = lerp(0.2, 0.55, std::pow(wear, 0.85));
            if (pick < chance) {
                const double before = dur;
                const double stretch = lerp(1.0, lerp(1.1, 1.36, pick), std::pow(wear, 0.8));
                dur *= stretch;
                double room = e.patternLength * 0.94;
                if (e.wrap)
                    room = std::min(room, e.cell * lerp(0.74, 0.96, wear));
                dur = std::min(std::max(dur, before), std::max(room, before));
            }
        }

        const double span = std::max(0.0, e.cell * 0.92);
        double start = cellStart + driftVis * uPos * span * 0.55;
        if (f > 0)
            start = cellStart + e.cell * lerp(0.46, 0.15 + 0.7 * uPos, e.randomness);

        const double inSpread = lerp(1.0, lerp(0.32, 2.15, uFi), driftVis);
        const double outSpread = lerp(1.0, lerp(0.32, 2.15, uFo), driftVis);
        double fadeIn = e.fade * inSpread;
        double fadeOut = e.fade * outSpread;
        if (fadeIn < 0.0)
            fadeIn = 0.0;
        if (fadeOut < 0.0)
            fadeOut = 0.0;
        const double tapeEdge = secToUnit(kMinEdgeSec, e);
        const double stitch = secToUnit(kStitchSec, e);
        // Long holes take the slow tape shoulder. A short stutter only needs a
        // cosine long enough to avoid a step. Heavy wear never keeps that short
        // cosine: the hole grows until a full shoulder fits inside it.
        double shoulder = dur >= tapeEdge * 4.0 ? tapeEdge : stitch;
        if (e.degrade > 0.35) {
            const double need = tapeEdge * 2.0;
            if (dur < need)
                dur = std::min(need, std::max(dur, e.maxDur));
            if (e.wrap)
                dur = std::min(dur, e.patternLength * 0.92);
            shoulder = tapeEdge;
        }
        if (e.fadeChaos > 0.15) {
            const double cut = hash01(e.seed, key, static_cast<std::uint64_t>(110 + f));
            const double tighten = cut * e.fadeChaos;
            fadeIn = lerp(fadeIn, shoulder, tighten);
            fadeOut = lerp(fadeOut, shoulder, tighten);
        }
        fadeIn = std::max(fadeIn, shoulder);
        fadeOut = std::max(fadeOut, shoulder);
        // Keep a held mute in the middle. The fades can grow with the hole,
        // and the silent part grows with them.
        if (dur > 0.0) {
            const double budget = std::max(shoulder * 2.0, dur * 0.58);
            if (fadeIn + fadeOut > budget) {
                const double scale = budget / (fadeIn + fadeOut);
                fadeIn *= scale;
                fadeOut *= scale;
            }
        }

        Event ev;
        ev.start = start;
        ev.duration = dur;
        ev.fadeIn = fadeIn;
        ev.fadeOut = fadeOut;
        ev.depth = static_cast<float>(lerp(1.0, lerp(0.62, 1.0, uDepth), driftVis));
        ev.curve = static_cast<float>(lerp(1.0, lerp(0.5, 1.7, uShape), driftVis));
        out[written++] = ev;
    }
    return written;
}

float eventGain(double time, const Event& ev, const Effective& e) noexcept {
    if (!(ev.duration > 0.0))
        return 1.f;

    double rel;
    if (e.wrap)
        rel = wrapPositive(time - ev.start, e.patternLength);
    else
        rel = time - ev.start;

    if (!(rel >= 0.0 && rel < ev.duration))
        return 1.f;

    const double depth = clampd(ev.depth, 0.0, 1.0);
    const double curve = std::max(0.35, static_cast<double>(ev.curve));
    const double floor = 1.0 - depth;
    auto shoulder = [&](double x) {
        return std::pow(clampd(x, 0.0, 1.0), curve);
    };

    if (ev.fadeIn > 1.0e-12 && rel < ev.fadeIn) {
        const double x = shoulder(rel / ev.fadeIn);
        const double cosine = 0.5 * (1.0 + std::cos(kPi * x));
        return static_cast<float>(floor + depth * cosine);
    }

    const double tail = ev.duration - ev.fadeOut;
    if (ev.fadeOut > 1.0e-12 && rel > tail) {
        const double x = shoulder((rel - tail) / ev.fadeOut);
        const double rise = 0.5 * (1.0 - std::cos(kPi * x));
        return static_cast<float>(floor + depth * rise);
    }

    return static_cast<float>(floor);
}

std::int64_t cellOf(const Effective& e, double time) noexcept {
    if (!(e.cell > 0.0))
        return 0;
    if (e.wrap) {
        const double w = wrapPositive(time, e.patternLength);
        auto c = static_cast<std::int64_t>(std::floor(w / e.cell));
        if (c >= e.nCells)
            c = e.nCells - 1;
        if (c < 0)
            c = 0;
        return c;
    }
    return static_cast<std::int64_t>(std::floor(time / e.cell));
}

float gainAt(const Effective& e, double time) noexcept {
    if (e.bypass)
        return 1.f;

    const double t = e.wrap ? wrapPositive(time, e.patternLength) : time;
    const double look = std::max(e.maxDur, e.maxDurEff) + e.cell * 2.0;
    float g = 1.f;
    Event events[kMaxFragments];

    auto consume = [&](std::int64_t cell) {
        const int n = makeEvents(cell, e, events, kMaxFragments);
        for (int k = 0; k < n; ++k)
            g = std::min(g, eventGain(t, events[k], e));
    };

    if (!e.wrap) {
        if (!(e.cell > 0.0))
            return 1.f;
        const auto c0 = static_cast<std::int64_t>(std::floor((t - look) / e.cell)) - 1;
        const auto c1 = static_cast<std::int64_t>(std::floor(t / e.cell)) + 1;
        for (std::int64_t c = c0; c <= c1; ++c) {
            consume(c);
            if (g <= 0.f)
                return 0.f;
        }
        return g;
    }

    const int extra = static_cast<int>(std::ceil(look / std::max(e.cell, 1.0e-9))) + 3;
    if (extra >= e.nCells) {
        for (int c = 0; c < e.nCells; ++c) {
            consume(c);
            if (g <= 0.f)
                return 0.f;
        }
        return g;
    }

    const std::int64_t center = cellOf(e, t);
    for (int k = 0; k <= extra; ++k) {
        consume(wrapCell(e, center - k));
        if (g <= 0.f)
            return 0.f;
    }
    return g;
}

float protectGain(float target, float& lastGain, int& slewLeft, int slewTotal, float slewAnchor, float maxDelta) noexcept {
    target = clampf(target, 0.f, 1.f);
    if (slewLeft > 0 && slewTotal > 0) {
        const float x = 1.f - static_cast<float>(slewLeft) / static_cast<float>(slewTotal);
        const float y = x * x * (3.f - 2.f * x);
        float g = slewAnchor + (target - slewAnchor) * y;
        if (--slewLeft == 0)
            g = target;
        lastGain = clampf(g, 0.f, 1.f);
        return lastGain;
    }

    const float delta = target - lastGain;
    if (delta > maxDelta)
        target = lastGain + maxDelta;
    else if (delta < -maxDelta)
        target = lastGain - maxDelta;
    lastGain = target;
    return lastGain;
}

} // namespace

float degradationTarget(const Degradation& settings, double elapsedSec, std::uint64_t seed) noexcept {
    const double ceiling = std::max(0.0, std::min(1.0, static_cast<double>(settings.ceiling)));
    const double initial = std::max(0.0, std::min(1.0, static_cast<double>(settings.initial)));
    const double timeSec = std::max(10.0, std::min(600.0, static_cast<double>(settings.timeSec)));
    if (settings.manual) {
        const double manual = std::max(0.0, std::min(1.0, static_cast<double>(settings.manualLevel))) * ceiling;
        return static_cast<float>(manual);
    }

    const double u = std::max(0.0, std::min(1.0, (std::isfinite(elapsedSec) ? elapsedSec : 0.0) / timeSec));
    double shape = u;
    switch (settings.curve) {
        case static_cast<int>(DegradeCurve::SlowBurn):
            shape = std::pow(u, 3.0);
            break;
        case static_cast<int>(DegradeCurve::Rapid):
            shape = 1.0 - std::pow(1.0 - u, 3.0);
            break;
        case static_cast<int>(DegradeCurve::Unstable):
            shape = std::pow(u, 1.15);
            break;
        default:
            shape = u;
            break;
    }

    if (settings.curve == static_cast<int>(DegradeCurve::Unstable)) {
        constexpr double kWindow = 4.0;
        const double elapsed = std::max(0.0, std::isfinite(elapsedSec) ? elapsedSec : 0.0);
        const double pos = elapsed - std::floor(elapsed / kWindow) * kWindow;
        const auto bucket = static_cast<std::uint64_t>(std::floor(elapsed / kWindow));
        const std::uint64_t useSeed = seed == 0 ? kDefaultSeed : seed;
        if (hash01(useSeed, bucket, 77) > 0.45) {
            constexpr double center = 2.48;
            constexpr double half = 0.22;
            const double dist = std::abs(pos - center);
            if (dist < half) {
                constexpr double kPiLocal = 3.14159265358979323846;
                const double env = 0.5 * (1.0 + std::cos(kPiLocal * dist / half));
                shape = std::min(1.0, shape + 0.42 * env);
            }
        }
    }

    const double mixed = initial + (1.0 - initial) * shape;
    return static_cast<float>(std::min(ceiling, mixed * ceiling));
}

double subdivisionBeats(int index) noexcept {
    static constexpr double kTable[] = {
        4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
        2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0,
        0.75, 0.375
    };
    constexpr int kCount = static_cast<int>(sizeof(kTable) / sizeof(kTable[0]));
    if (index < 0 || index >= kCount)
        index = 3;
    return kTable[index];
}

double beatsPerBarFromSignature(int numerator, int denominator) noexcept {
    if (numerator < 1 || numerator > 32)
        numerator = 4;
    if (denominator != 1 && denominator != 2 && denominator != 4 && denominator != 8 && denominator != 16)
        denominator = 4;
    return static_cast<double>(numerator) * 4.0 / static_cast<double>(denominator);
}

double sanitizeBpm(double bpm) noexcept {
    if (!(bpm >= 20.0 && bpm <= 999.0))
        return 120.0;
    return bpm;
}

ScheduleContext contextFor(const Parameters& params, const Transport& transport, std::uint64_t seed) noexcept {
    ScheduleContext ctx;
    ctx.params = params;
    ctx.seed = seed == 0 ? kDefaultSeed : seed;
    ctx.bpm = sanitizeBpm(transport.bpm);
    ctx.beatsPerBar = beatsPerBarFromSignature(transport.timeSigNumerator, transport.timeSigDenominator);
    ctx.sampleRate = transport.sampleRate > 1000.0 ? transport.sampleRate : 48000.0;
    ctx.params.dropper = clampf(params.dropper, 0.f, 1.f);
    ctx.params.density = clampf(params.density, 0.f, 1.f);
    ctx.params.randomness = clampf(params.randomness, 0.f, 1.f);
    ctx.params.mix = clampf(params.mix, 0.f, 1.f);
    ctx.params.patternBars = clampf(params.patternBars, 1.f, 4.f);
    ctx.params.minDurationSec = clampf(params.minDurationSec, 0.0005f, 0.050f);
    ctx.params.maxDurationSec = clampf(params.maxDurationSec, 0.001f, 0.500f);
    ctx.params.fadeSec = clampf(params.fadeSec, 0.f, 0.200f);
    if (ctx.params.maxDurationSec < ctx.params.minDurationSec)
        std::swap(ctx.params.minDurationSec, ctx.params.maxDurationSec);
    auto& d = ctx.params.degrade;
    d.ceiling = clampf(d.ceiling, 0.f, 1.f);
    d.initial = clampf(d.initial, 0.f, 1.f);
    d.timeSec = static_cast<float>(clampd(d.timeSec, 10.0, 600.0));
    d.recovery = clampf(d.recovery, 0.f, 1.f);
    d.manualLevel = clampf(d.manualLevel, 0.f, 1.f);
    if (d.curve < 0 || d.curve > 3)
        d.curve = static_cast<int>(DegradeCurve::Steady);
    auto& s = ctx.params.stability;
    s.amount = clampf(s.amount, 0.f, 1.f);
    s.wow = clampf(s.wow, 0.f, 1.f);
    s.flutter = clampf(s.flutter, 0.f, 1.f);
    s.noise = clampf(s.noise, 0.f, 1.f);
    s.freqLoss = clampf(s.freqLoss, 0.f, 1.f);
    s.freqLossMax = clampf(s.freqLossMax, 0.f, 1.f);
    s.amplitude = clampf(s.amplitude, 0.f, 1.f);
    s.wearAmount = clampf(s.wearAmount, 0.f, 1.f);
    s.retention = clampf(s.retention, 0.f, 1.f);
    if (s.generations < 0)
        s.generations = 0;
    if (s.generations > 24)
        s.generations = 24;
    ctx.degradeLevel = clampf(ctx.degradeLevel, 0.f, 1.f);
    return ctx;
}

ScheduleGeometry geometryFor(const ScheduleContext& ctx) noexcept {
    const Effective e = makeEffective(ctx);
    ScheduleGeometry g;
    g.patternLength = e.patternLength;
    g.unit = e.unit;
    g.bypass = e.bypass;
    g.repeatable = e.wrap;
    return g;
}

float scheduleGain(const ScheduleContext& ctx, double time) noexcept {
    if (!std::isfinite(time))
        return 1.f;
    const Effective e = makeEffective(ctx);
    const float g = gainAt(e, time);
    if (!std::isfinite(g))
        return 1.f;
    return clampf(g, 0.f, 1.f);
}

Engine::Engine() = default;

void Engine::prepare(double sampleRate, int) {
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    freeSamples_ = 0.0;
    freeBeats_ = 0.0;
    expectedCursor_ = 0.0;
    haveCursor_ = false;
    lastSync_ = -1;
    lastGain_ = 1.f;
    slewLeft_ = 0;
    slewTotal_ = 1;
    slewAnchor_ = 1.f;
    pendingRegens_.store(0, std::memory_order_relaxed);
    discontinuity_.store(false, std::memory_order_relaxed);
    clearStability();
}

void Engine::reset() {
    prepare(sampleRate_, 0);
}

void Engine::setSeed(std::uint64_t seed) {
    if (seed == 0)
        seed = 1;
    seed_.store(seed, std::memory_order_relaxed);
    noiseState_ = seed;
    discontinuity_.store(true, std::memory_order_release);
}

std::uint64_t Engine::getSeed() const noexcept {
    return seed_.load(std::memory_order_relaxed);
}

void Engine::regenerate() {
    const std::uint64_t next = mixSeed(seed_.load(std::memory_order_relaxed));
    seed_.store(next, std::memory_order_relaxed);
    noiseState_ = next;
    discontinuity_.store(true, std::memory_order_release);
}

void Engine::requestRegenerate() {
    pendingRegens_.fetch_add(1, std::memory_order_release);
}

void Engine::requestDegradeReset() {
    pendingDegradeResets_.fetch_add(1, std::memory_order_release);
}

void Engine::requestGenerationReset() {
    pendingGenResets_.fetch_add(1, std::memory_order_release);
}

DegradationState Engine::degradationState() const {
    DegradationState state;
    state.elapsed = degradeElapsedPub_.load(std::memory_order_relaxed);
    state.level = degradeLevelPub_.load(std::memory_order_relaxed);
    return state;
}

void Engine::restoreDegradationState(DegradationState state) {
    if (!std::isfinite(state.elapsed) || state.elapsed < 0.0)
        state.elapsed = 0.0;
    if (!std::isfinite(state.level))
        state.level = 0.f;
    state.level = std::max(0.f, std::min(1.f, state.level));
    degradeElapsedPub_.store(state.elapsed, std::memory_order_relaxed);
    degradeLevelPub_.store(state.level, std::memory_order_relaxed);
    degradeRestore_.store(true, std::memory_order_release);
}

void Engine::clearStability() {
    modWrite_ = 0;
    wowPhase_ = 0.f;
    flutterPhase_ = 0.f;
    drift_ = 0.f;
    driftTarget_ = 0.f;
    driftHold_ = 0;
    ampPhase_ = 0.f;
    amountSmoothed_ = 0.f;
    engage_ = 0.f;
    toneLp_[0] = toneLp_[1] = 0.f;
    noiseHpIn_ = 0.f;
    noiseHpOut_ = 0.f;
    noiseLp_ = 0.f;
    crackleEnv_ = 0.f;
    burstEnv_ = 0.f;
    burstLeft_ = 0;
    wanderRate_ = 0.4f;
    wanderRateTarget_ = 0.4f;
    wanderHold_ = 0;
    wanderPhase2_ = 0.f;
    lossWander_ = 0.f;
    lossWanderTarget_ = 0.f;
    lossHold_ = 0;
    hissBreath_ = 0.8f;
    hissBreathTarget_ = 0.8f;
    hissHold_ = 0;
    crackleWait_ = 1;
    noiseState_ = seed_.load(std::memory_order_relaxed);
    if (noiseState_ == 0)
        noiseState_ = 1;
    stabilitySamples_ = 0;
    genPos_ = 0;
    genCount_ = 0;
    genLp_[0] = genLp_[1] = 0.f;
    genDcX_[0] = genDcX_[1] = 0.f;
    genDcY_[0] = genDcY_[1] = 0.f;
    std::memset(modDelay_, 0, sizeof(modDelay_));

    const int length = std::max(64, static_cast<int>(std::lround(sampleRate_ * kGenerationLoopSec)));
    genCapacity_ = length;
    genLength_ = length;
    genLoop_.assign(static_cast<size_t>(2 * genCapacity_), 0.f);
    genClean_.assign(static_cast<size_t>(2 * genCapacity_), 0.f);
}

float Engine::nextNoise() noexcept {
    std::uint64_t x = noiseState_ == 0 ? 1 : noiseState_;
    x ^= x << 7;
    x ^= x >> 9;
    x ^= x << 8;
    if (x == 0)
        x = 0x9E3779B97F4A7C15ULL;
    noiseState_ = x;
    const auto bits = static_cast<int>(x >> 40);
    return static_cast<float>(bits - 8388608) * (1.f / 8388608.f);
}

int Engine::stabilityLatencySamples(float amount) const noexcept {
    if (!(amount > 0.001f))
        return 0;
    return std::max(0, static_cast<int>(std::lround(kStabilityDelaySec * sampleRate_)));
}

StabilityState Engine::stabilityState() const {
    StabilityState state;
    state.wowPhase = wowPhase_;
    state.flutterPhase = flutterPhase_;
    state.drift = drift_;
    state.driftTarget = driftTarget_;
    state.ampPhase = ampPhase_;
    state.amountSmoothed = amountSmoothed_;
    state.engage = engage_;
    state.toneLp[0] = toneLp_[0];
    state.toneLp[1] = toneLp_[1];
    state.noiseHpIn = noiseHpIn_;
    state.noiseHpOut = noiseHpOut_;
    state.noiseLp = noiseLp_;
    state.crackleEnv = crackleEnv_;
    state.burstEnv = burstEnv_;
    state.wanderRate = wanderRate_;
    state.wanderRateTarget = wanderRateTarget_;
    state.wanderPhase2 = wanderPhase2_;
    state.lossWander = lossWander_;
    state.lossWanderTarget = lossWanderTarget_;
    state.hissBreath = hissBreath_;
    state.hissBreathTarget = hissBreathTarget_;
    state.genLp[0] = genLp_[0];
    state.genLp[1] = genLp_[1];
    state.genDcX[0] = genDcX_[0];
    state.genDcX[1] = genDcX_[1];
    state.genDcY[0] = genDcY_[0];
    state.genDcY[1] = genDcY_[1];
    state.noiseState = noiseState_;
    state.sampleCount = stabilitySamples_;
    state.driftHold = driftHold_;
    state.burstLeft = burstLeft_;
    state.wanderHold = wanderHold_;
    state.lossHold = lossHold_;
    state.hissHold = hissHold_;
    state.crackleWait = crackleWait_;
    state.genPos = genPos_;
    state.genCount = genCount_;
    state.genLength = genLength_;
    state.modWrite = modWrite_;
    state.sampleRate = sampleRate_;
    state.loop = genLoop_;
    state.clean = genClean_;
    state.modDelay.assign(&modDelay_[0][0], &modDelay_[0][0] + 2 * kModDelay);
    return state;
}

void Engine::restoreStabilityState(const StabilityState& state) {
    wowPhase_ = state.wowPhase;
    flutterPhase_ = state.flutterPhase;
    drift_ = state.drift;
    driftTarget_ = state.driftTarget;
    ampPhase_ = state.ampPhase;
    amountSmoothed_ = state.amountSmoothed;
    engage_ = state.engage;
    toneLp_[0] = state.toneLp[0];
    toneLp_[1] = state.toneLp[1];
    noiseHpIn_ = state.noiseHpIn;
    noiseHpOut_ = state.noiseHpOut;
    noiseLp_ = state.noiseLp;
    crackleEnv_ = state.crackleEnv;
    burstEnv_ = state.burstEnv;
    wanderRate_ = state.wanderRate;
    wanderRateTarget_ = state.wanderRateTarget;
    wanderPhase2_ = state.wanderPhase2;
    lossWander_ = state.lossWander;
    lossWanderTarget_ = state.lossWanderTarget;
    hissBreath_ = state.hissBreath;
    hissBreathTarget_ = state.hissBreathTarget;
    genLp_[0] = state.genLp[0];
    genLp_[1] = state.genLp[1];
    genDcX_[0] = state.genDcX[0];
    genDcX_[1] = state.genDcX[1];
    genDcY_[0] = state.genDcY[0];
    genDcY_[1] = state.genDcY[1];
    noiseState_ = state.noiseState == 0 ? 1 : state.noiseState;
    stabilitySamples_ = state.sampleCount;
    driftHold_ = state.driftHold;
    burstLeft_ = state.burstLeft;
    wanderHold_ = state.wanderHold;
    lossHold_ = state.lossHold;
    hissHold_ = state.hissHold;
    crackleWait_ = std::max(0, state.crackleWait);
    if (state.sampleRate == sampleRate_ && state.genLength == genLength_
        && state.loop.size() == genLoop_.size() && state.clean.size() == genClean_.size()) {
        genLoop_ = state.loop;
        genClean_ = state.clean;
        genPos_ = std::max(0, std::min(state.genPos, std::max(0, genLength_ - 1)));
        genCount_ = std::max(0, state.genCount);
    }
    if (state.modDelay.size() == static_cast<size_t>(2 * kModDelay)) {
        std::copy(state.modDelay.begin(), state.modDelay.end(), &modDelay_[0][0]);
        modWrite_ = state.modWrite & (kModDelay - 1);
    }
}

void Engine::applyStability(float* const* channels, int numChannels, int index, const Stability& stability,
                            std::uint64_t seed, double sampleRate, float dropout) noexcept {
    if (channels == nullptr || numChannels <= 0 || !(sampleRate > 1000.0))
        return;

    const float dt = static_cast<float>(1.0 / sampleRate);
    const float amount = clampf(stability.amount, 0.f, 1.f);
    const bool genOn = stability.generational && genLength_ > 8 && !genLoop_.empty();
    const float follow = 1.f - std::exp(-dt / 0.012f);
    amountSmoothed_ += (amount - amountSmoothed_) * follow;
    if (amount <= 1.0e-5f && amountSmoothed_ < 1.0e-4f)
        amountSmoothed_ = 0.f;

    const float engageTarget = (amountSmoothed_ > 1.0e-5f || genOn) ? 1.f : 0.f;
    engage_ += (engageTarget - engage_) * follow;
    if (engageTarget == 0.f && engage_ < 1.0e-4f)
        engage_ = 0.f;

    if (engage_ <= 0.f && !genOn) {
        const int nCh = std::min(numChannels, 2);
        for (int ch = 0; ch < nCh; ++ch) {
            if (channels[ch] != nullptr)
                toneLp_[ch] = channels[ch][index];
        }
        return;
    }

    if (noiseState_ == 0)
        noiseState_ = seed == 0 ? 1 : seed;

    const float a = amountSmoothed_;
    // Easier than a straight line, so the first half of Amount is already unstable.
    const float motion = std::pow(std::max(a, 0.f), 0.65f);
    if (driftHold_ <= 0) {
        driftTarget_ = nextNoise();
        driftHold_ = std::max(1, static_cast<int>(sampleRate * 0.045));
    }
    --driftHold_;
    drift_ += (driftTarget_ - drift_) * (1.f - std::exp(-dt / 0.18f));

    const float wow = clampf(stability.wow, 0.f, 1.f);
    if (wanderHold_ <= 0) {
        wanderRateTarget_ = 0.28f + 1.2f * (0.5f + 0.5f * nextNoise());
        wanderHold_ = std::max(1, static_cast<int>(sampleRate * lerp(0.28, 1.7, 0.5 + 0.5 * nextNoise())));
    }
    --wanderHold_;
    wanderRate_ += (wanderRateTarget_ - wanderRate_) * (1.f - std::exp(-dt / 0.35f));
    const float wanderHz = wanderRate_ * static_cast<float>(lerp(0.72, 1.28, wow))
                           * static_cast<float>(lerp(0.82, 1.18, 0.5 + 0.5 * drift_));
    const float secondHz = wanderHz * static_cast<float>(lerp(0.31, 0.73, 0.5 + 0.5 * drift_));
    wowPhase_ += wanderHz * dt;
    wanderPhase2_ += secondHz * dt;
    const float flutterHz = static_cast<float>(lerp(3.2, 14.0, std::pow(a, 0.65))
                                                * lerp(0.7, 1.25, stability.flutter)
                                                * lerp(0.86, 1.14, 0.5 + 0.5 * drift_));
    flutterPhase_ += flutterHz * dt;
    if (wowPhase_ >= 1.f)
        wowPhase_ -= std::floor(wowPhase_);
    if (wanderPhase2_ >= 1.f)
        wanderPhase2_ -= std::floor(wanderPhase2_);
    if (flutterPhase_ >= 1.f)
        flutterPhase_ -= std::floor(flutterPhase_);
    ampPhase_ += static_cast<float>(lerp(0.05, 0.22, 0.5f + 0.5f * drift_) * dt);
    if (ampPhase_ >= 1.f)
        ampPhase_ -= std::floor(ampPhase_);

    if (burstLeft_ > 0) {
        --burstLeft_;
        const float env = static_cast<float>(burstLeft_) / static_cast<float>(std::max(1, static_cast<int>(sampleRate * 0.07)));
        burstEnv_ = std::max(0.f, env);
    } else {
        burstEnv_ = 0.f;
        if (a > 0.72f && stability.flutter > 0.01f && nextNoise() > 0.992f)
            burstLeft_ = std::max(1, static_cast<int>(sampleRate * 0.07));
    }
    ++stabilitySamples_;

    const float wanderShape = 0.55f + 0.45f * (0.5f + 0.5f * drift_);
    const float wanderLfo = 0.68f * std::sin(6.2831853f * wowPhase_) + 0.32f * std::sin(6.2831853f * wanderPhase2_);
    // One tap. Depth is several milliseconds at the default Wander so the pitch
    // drifts slowly. Flutter stays off unless a test turns that parameter on.
    const float wowExc = motion * wow * static_cast<float>(0.014 * sampleRate) * wanderShape;
    const float flutterExc = static_cast<float>(motion * stability.flutter * 0.00115 * sampleRate
                                                 * (1.0 + 1.8 * burstEnv_));
    const float driftExc = motion * wow * static_cast<float>(0.0032 * sampleRate) * drift_;
    float delaySamples = static_cast<float>(kStabilityDelaySec * sampleRate) + wowExc * wanderLfo
                         + flutterExc * std::sin(6.2831853f * flutterPhase_) + driftExc;
    delaySamples = clampf(delaySamples, 1.f, static_cast<float>(kModDelay - 2));

    if (lossHold_ <= 0) {
        lossWanderTarget_ = nextNoise();
        lossHold_ = std::max(1, static_cast<int>(sampleRate * lerp(0.12, 0.85, 0.5 + 0.5 * nextNoise())));
    }
    --lossHold_;
    lossWander_ += (lossWanderTarget_ - lossWander_) * (1.f - std::exp(-dt / 0.16f));
    const float loss = clampf(motion * stability.freqLoss * stability.freqLossMax, 0.f, 1.f);
    const float dull = clampf(loss * static_cast<float>(lerp(0.72, 1.15, 0.5 + 0.5 * lossWander_)), 0.f, 1.f);
    const float cutoff = static_cast<float>(lerp(15000.0, 1400.0, std::pow(dull, 0.6f)));
    const float toneCoef = 1.f - std::exp(-6.2831853f * cutoff * dt);
    const float drive = stability.saturation ? motion * 0.65f : 0.f;
    const float satNorm = drive > 1.0e-4f ? 1.f / std::tanh(1.f + 2.4f * drive) : 1.f;
    const float noiseGain = (stability.noiseEnabled && a > 1.0e-5f)
                                 ? static_cast<float>(stability.noise * (0.0018f + 0.05f * motion))
                                 : 0.f;
    const float ampDepth = stability.amplitude * motion * 0.22f + motion * 0.30f;
    const float amp = 1.f - ampDepth * (0.4f + 0.6f * (0.5f + 0.5f * drift_));

    float hiss = 0.f;
    float crack = 0.f;
    if (noiseGain > 0.f) {
        if (hissHold_ <= 0) {
            hissBreathTarget_ = 0.4f + 0.6f * (0.5f + 0.5f * nextNoise());
            hissHold_ = std::max(1, static_cast<int>(sampleRate * lerp(0.18, 1.3, 0.5 + 0.5 * nextNoise())));
        }
        --hissHold_;
        hissBreath_ += (hissBreathTarget_ - hissBreath_) * (1.f - std::exp(-dt / 0.22f));
        const float white = nextNoise();
        const float hp = white - noiseHpIn_ + 0.985f * noiseHpOut_;
        noiseHpIn_ = white;
        noiseHpOut_ = hp;
        const float lpCoef = 1.f - std::exp(-6.2831853f * 1650.f * dt);
        noiseLp_ += lpCoef * (hp - noiseLp_);
        if (!std::isfinite(noiseLp_) || std::abs(noiseLp_) < 1.0e-20f)
            noiseLp_ = 0.f;
        hiss = noiseLp_ * noiseGain * hissBreath_ * dropout;
        crackleEnv_ *= 0.965f;
        if (crackleWait_ <= 0) {
            crackleEnv_ = std::max(crackleEnv_, (0.25f + 0.75f * std::abs(nextNoise())) * stability.noise * a);
            crackleWait_ = std::max(1, static_cast<int>(sampleRate * lerp(0.04, 1.6, 0.5 + 0.5 * nextNoise())));
        } else {
            --crackleWait_;
        }
        crack = noiseLp_ * crackleEnv_ * noiseGain * dropout * 4.5f;
    }

    const int mask = kModDelay - 1;
    auto readTap = [&](int ch, float delay) noexcept {
        const float read = static_cast<float>(modWrite_) - delay;
        float wrapped = std::fmod(read, static_cast<float>(kModDelay));
        if (wrapped < 0.f)
            wrapped += static_cast<float>(kModDelay);
        const int i0 = static_cast<int>(wrapped) & mask;
        const int i1 = (i0 + 1) & mask;
        const float frac = wrapped - std::floor(wrapped);
        return modDelay_[ch][i0] + (modDelay_[ch][i1] - modDelay_[ch][i0]) * frac;
    };
    const int nCh = std::min(numChannels, 2);
    float live[2] = { 0.f, 0.f };
    float clean[2] = { 0.f, 0.f };
    for (int ch = 0; ch < nCh; ++ch) {
        if (channels[ch] == nullptr)
            continue;
        const float x = channels[ch][index];
        clean[ch] = x;
        modDelay_[ch][modWrite_] = x;
        float y = readTap(ch, delaySamples);
        if (dull > 1.0e-4f) {
            y = toneLp_[ch] + toneCoef * (y - toneLp_[ch]);
            if (!std::isfinite(y) || std::abs(y) < 1.0e-20f)
                y = 0.f;
        }
        toneLp_[ch] = y;
        if (drive > 1.0e-4f)
            y = std::tanh(y * (1.f + 2.4f * drive)) * satNorm;
        y = (y + hiss + crack) * amp;
        if (!std::isfinite(y))
            y = 0.f;
        live[ch] = y;
    }
    modWrite_ = (modWrite_ + 1) & mask;

    if (genOn) {
        const int pos = genPos_;
        const bool imprint = stability.generations <= 0 || genCount_ < stability.generations;
        const float retention = clampf(stability.retention, 0.f, 1.f);
        const float wear = clampf(stability.wearAmount, 0.f, 1.f);
        const float genCutoff = static_cast<float>(lerp(14000.0, 2400.0, std::pow(std::max(wear, 0.f), 0.75)));
        const float genCoef = 1.f - std::exp(-6.2831853f * genCutoff * dt);
        const float genDrive = 1.f + 2.2f * wear;
        const bool firstPass = genCount_ == 0;
        for (int ch = 0; ch < nCh; ++ch) {
            if (channels[ch] == nullptr)
                continue;
            const size_t slot = static_cast<size_t>(ch * genCapacity_ + pos);
            const float prev = genLoop_[slot];
            float recirc = firstPass ? live[ch] : static_cast<float>(lerp(live[ch], prev, retention));
            if (imprint) {
                recirc = genLp_[ch] + genCoef * (recirc - genLp_[ch]);
                if (!std::isfinite(recirc))
                    recirc = 0.f;
                genLp_[ch] = recirc;
                recirc = std::tanh(recirc * genDrive) / genDrive;
                if (stability.noiseEnabled && noiseGain > 0.f)
                    recirc += hiss * wear * 0.65f;
            }
            const float dc = recirc - genDcX_[ch] + 0.995f * genDcY_[ch];
            genDcX_[ch] = recirc;
            genDcY_[ch] = dc;
            genLoop_[slot] = clampf(dc, -1.2f, 1.2f);
            genClean_[slot] = clampf(clean[ch], -2.f, 2.f);
            if (!firstPass)
                live[ch] = static_cast<float>(lerp(live[ch], prev, wear * 0.85f));
        }
        ++genPos_;
        if (genPos_ >= genLength_) {
            genPos_ = 0;
            if (genCount_ < 1000000)
                ++genCount_;
        }
    }

    for (int ch = 0; ch < nCh; ++ch) {
        if (channels[ch] == nullptr)
            continue;
        const float dry = clean[ch];
        float y = static_cast<float>(lerp(dry, live[ch], engage_));
        if (!std::isfinite(y))
            y = dry;
        if (std::abs(y) < 1.0e-20f)
            y = 0.f;
        channels[ch][index] = y;
    }
}

bool Engine::tryLock() const noexcept {
    return !lock_.test_and_set(std::memory_order_acquire);
}

void Engine::unlock() const noexcept {
    lock_.clear(std::memory_order_release);
}

Snapshot Engine::snapshot() const {
    Snapshot copy;
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (!tryLock())
            continue;
        copy = published_.snap;
        unlock();
        return copy;
    }
    return copy;
}

void Engine::process(float* const* channels, int numChannels, int numSamples,
                     const Parameters& params, const Transport& transport,
                     float* gainOut, bool clickProtection) {
    if (numSamples <= 0)
        return;

    const int pending = pendingRegens_.exchange(0, std::memory_order_acq_rel);
    for (int i = 0; i < pending; ++i)
        regenerate();

    const double sampleRate = transport.sampleRate > 1000.0 ? transport.sampleRate : sampleRate_;
    sampleRate_ = sampleRate;

    Transport timed = transport;
    timed.sampleRate = sampleRate;
    timed.bpm = sanitizeBpm(transport.bpm);

    const bool sync = params.timingSync != 0;
    const bool host = sync && transport.hasHostPosition;
    const bool hold = host && !transport.isPlaying;

    if (sync != (lastSync_ == 1) && lastSync_ != -1)
        discontinuity_.store(true, std::memory_order_relaxed);
    lastSync_ = sync ? 1 : 0;

    double start;
    if (host)
        start = std::isfinite(transport.ppqPosition) ? transport.ppqPosition : 0.0;
    else if (sync)
        start = freeBeats_;
    else
        start = freeSamples_ / sampleRate;

    if (host) {
        if (haveCursor_ && std::abs(start - expectedCursor_) > 0.01)
            discontinuity_.store(true, std::memory_order_relaxed);
    }
    haveCursor_ = true;

    const double dt = hold ? 0.0
                            : (sync ? (timed.bpm / 60.0) / sampleRate : 1.0 / sampleRate);

    if (degradeRestore_.exchange(false, std::memory_order_acq_rel)) {
        degradeElapsed_ = degradeElapsedPub_.load(std::memory_order_relaxed);
        degradeLevel_ = degradeLevelPub_.load(std::memory_order_relaxed);
    }

    std::uint64_t activeSeed = seed_.load(std::memory_order_relaxed);
    ScheduleContext ctx = contextFor(params, timed, activeSeed);
    const bool degradeOn = ctx.params.degrade.enabled;
    const bool transportKnown = transport.hasHostPosition;
    const bool playing = transportKnown ? transport.isPlaying : true;
    const float ceiling = ctx.params.degrade.ceiling;

    const int genResets = pendingGenResets_.exchange(0, std::memory_order_acq_rel);
    if (genResets > 0 && genLength_ > 0 && genLoop_.size() == genClean_.size()) {
        std::copy(genClean_.begin(), genClean_.end(), genLoop_.begin());
        genCount_ = 0;
        genLp_[0] = genLp_[1] = 0.f;
        genDcX_[0] = genDcX_[1] = 0.f;
        genDcY_[0] = genDcY_[1] = 0.f;
    }

    const int degradeResets = pendingDegradeResets_.exchange(0, std::memory_order_acq_rel);
    if (degradeResets > 0) {
        degradeElapsed_ = 0.0;
        degradeLevel_ = ctx.params.degrade.initial * ceiling;
        regenerate();
        activeSeed = seed_.load(std::memory_order_relaxed);
        ctx = contextFor(params, timed, activeSeed);
        discontinuity_.store(true, std::memory_order_relaxed);
    }

    if (degradeOn && degradeElapsed_ <= 0.0 && !ctx.params.degrade.hold)
        degradeLevel_ = degradationTarget(ctx.params.degrade, 0.0, activeSeed);
    if (degradeOn) {
        if (degradeLevel_ > ceiling)
            degradeLevel_ = ceiling;
        if (degradeLevel_ < 0.f)
            degradeLevel_ = 0.f;
    }

    ctx.degradeLevel = degradeOn ? degradeLevel_ : 0.f;
    Effective eff = makeEffective(ctx);

    const int slewSamples = std::max(1, static_cast<int>(std::lround(sampleRate * 0.0025)));
    const double degradeStep = 1.0 / sampleRate;
    const bool advanceDegrade = degradeOn && playing && !ctx.params.degrade.hold;
    // Amount is the gate for Wander, Noise, and Loss. Destroy can push that
    // amount further, but it does not turn those effects on by itself.
    const bool stabilityOn = channels != nullptr
                             && (ctx.params.stability.amount > 0.0008f
                                 || ctx.params.stability.generational
                                 || engage_ > 0.0008f || amountSmoothed_ > 0.0008f);

    for (int i = 0; i < numSamples; ++i) {
        if (discontinuity_.exchange(false, std::memory_order_acquire)) {
            if (clickProtection) {
                slewAnchor_ = lastGain_;
                slewTotal_ = slewSamples;
                slewLeft_ = slewTotal_;
            }
            activeSeed = seed_.load(std::memory_order_relaxed);
            ctx = contextFor(params, timed, activeSeed);
            ctx.degradeLevel = degradeOn ? degradeLevel_ : 0.f;
            eff = makeEffective(ctx);
        }

        // Hold freezes the wear clock. Manual still moves the level, so the
        // knob is not stuck while Hold is on.
        if (advanceDegrade || (degradeOn && ctx.params.degrade.manual)) {
            if (advanceDegrade)
                degradeElapsed_ += degradeStep;
            const float targetLevel = degradationTarget(ctx.params.degrade, degradeElapsed_, activeSeed);
            const float tau = targetLevel < degradeLevel_
                                  ? (6.f - (6.f - 0.12f) * ctx.params.degrade.recovery)
                                  : 0.035f;
            const float coef = 1.f - std::exp(-static_cast<float>(degradeStep) / std::max(0.001f, tau));
            degradeLevel_ += (targetLevel - degradeLevel_) * coef;
        }
        if (degradeOn) {
            if (degradeLevel_ > ceiling)
                degradeLevel_ = ceiling;
            if (degradeLevel_ < 0.f)
                degradeLevel_ = 0.f;
        }
        if (degradeOn && ctx.degradeLevel != degradeLevel_) {
            ctx.degradeLevel = degradeLevel_;
            eff = makeEffective(ctx);
        }

        const double time = start + static_cast<double>(i) * dt;
        float target = gainAt(eff, time);
        if (!std::isfinite(target))
            target = 1.f;

        float gain = clickProtection
                         ? protectGain(target, lastGain_, slewLeft_, slewTotal_, slewAnchor_, eff.maxDelta)
                         : clampf(target, 0.f, 1.f);
        if (!clickProtection)
            lastGain_ = gain;

        if (gainOut != nullptr)
            gainOut[i] = gain;

        const float mul = 1.f - eff.mix * (1.f - gain);
        if (mul != 1.f && channels != nullptr) {
            const int nCh = std::max(0, numChannels);
            for (int ch = 0; ch < nCh; ++ch) {
                if (channels[ch] != nullptr)
                    channels[ch][i] *= mul;
            }
        }
        if (stabilityOn) {
            Stability heard = ctx.params.stability;
            if (degradeOn && heard.amount > 0.0008f) {
                const float wear = clampf(degradeLevel_, 0.f, 1.f);
                heard.amount = clampf(heard.amount + (1.f - heard.amount) * wear, 0.f, 1.f);
            }
            applyStability(channels, numChannels, i, heard, activeSeed, sampleRate, mul);
        }
    }

    if (host)
        expectedCursor_ = hold ? start : start + dt * static_cast<double>(numSamples);
    if (!host && sync)
        freeBeats_ = start + dt * static_cast<double>(numSamples);
    if (!sync)
        freeSamples_ += static_cast<double>(numSamples);

    degradeElapsedPub_.store(degradeElapsed_, std::memory_order_relaxed);
    degradeLevelPub_.store(degradeLevel_, std::memory_order_relaxed);

    Snapshot snap;
    snap.playhead = hold ? start : start + dt * static_cast<double>(numSamples);
    snap.bpm = ctx.bpm;
    snap.beatsPerBar = ctx.beatsPerBar;
    snap.sampleRate = sampleRate;
    snap.patternLength = eff.patternLength > 0.0 ? eff.patternLength : ctx.beatsPerBar;
    snap.outputGain = lastGain_;
    snap.timeSigNumerator = transport.timeSigNumerator;
    snap.timeSigDenominator = transport.timeSigDenominator;
    snap.seed = seed_.load(std::memory_order_relaxed);
    snap.unit = eff.unit;
    snap.repeatable = eff.wrap;
    snap.bypassed = eff.bypass;
    snap.valid = true;
    snap.degradation = degradeLevel_;
    snap.degradationCeiling = ceiling;
    snap.degradationElapsed = degradeElapsed_;
    snap.degradationRemaining = degradeOn ? std::max(0.0, static_cast<double>(ctx.params.degrade.timeSec) - degradeElapsed_) : 0.0;
    snap.degradationEnabled = degradeOn;
    snap.degradationHeld = ctx.params.degrade.hold || (transportKnown && !playing);
    snap.generation = genCount_;
    snap.generational = ctx.params.stability.generational;

    if (tryLock()) {
        published_.snap = snap;
        unlock();
    }
}

} // namespace dropper
