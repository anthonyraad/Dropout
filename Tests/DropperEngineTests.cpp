#include "DSP/DropperEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        std::printf("FAIL %s:%d  %s\n", file, line, expression);
        ++gFailures;
    }
}

#define CHECK(cond) check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

dropper::Parameters paramsAt(float dropperAmount) {
    dropper::Parameters p;
    p.dropper = dropperAmount;
    p.density = 1.f;
    p.minDurationSec = 0.002f;
    p.maxDurationSec = 0.090f;
    p.fadeSec = 0.010f;
    p.randomness = 0.55f;
    p.patternBars = 1.f;
    p.mix = 1.f;
    p.timingSync = 0;
    p.subdivision = 3;
    p.patternMode = 0;
    return p;
}

dropper::Transport clockAt(double sampleRate, double bpm = 120.0) {
    dropper::Transport t;
    t.sampleRate = sampleRate;
    t.bpm = bpm;
    t.isPlaying = true;
    t.hasHostPosition = false;
    t.timeSigNumerator = 4;
    t.timeSigDenominator = 4;
    return t;
}

void prepare(dropper::Engine& engine, std::uint64_t seed, double sampleRate) {
    engine.setSeed(seed);
    engine.prepare(sampleRate, 512);
}

bool finiteInUnitInterval(const std::vector<float>& gain) {
    for (float g : gain) {
        if (!std::isfinite(g) || g < -1.0e-6f || g > 1.0f + 1.0e-6f)
            return false;
    }
    return true;
}

float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float m = 0.f;
    const auto n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
        m = std::max(m, std::abs(a[i] - b[i]));
    return m;
}

std::vector<float> renderGain(dropper::Parameters p, dropper::Transport t, std::uint64_t seed,
                              int samples, bool protect, int blockSize) {
    dropper::Engine engine;
    prepare(engine, seed, t.sampleRate);
    std::vector<float> gain(static_cast<size_t>(samples), -1.f);
    std::vector<float> audio(static_cast<size_t>(samples), 0.25f);
    int rendered = 0;
    while (rendered < samples) {
        const int count = std::min(blockSize, samples - rendered);
        float* channel = audio.data() + rendered;
        engine.process(&channel, 1, count, p, t, gain.data() + rendered, protect);
        if (p.timingSync != 0 && t.hasHostPosition && t.isPlaying) {
            const double beats = (dropper::sanitizeBpm(t.bpm) / 60.0) * static_cast<double>(count) / t.sampleRate;
            t.ppqPosition += beats;
        }
        rendered += count;
    }
    return gain;
}

void testBypass() {
    dropper::Engine engine;
    prepare(engine, 1, 48000.0);
    auto p = paramsAt(0.f);
    auto t = clockAt(48000.0);
    std::vector<float> input { 0.f, -1.f, 1.f, 1.0e-20f, -0.333f, 0.999f, -0.0001f, 0.25f };
    auto output = input;
    float* channel = output.data();
    engine.process(&channel, 1, static_cast<int>(output.size()), p, t, nullptr, true);
    CHECK(output == input);

    p.dropper = 0.015f; // inside the center detent
    output = input;
    channel = output.data();
    engine.process(&channel, 1, static_cast<int>(output.size()), p, t, nullptr, true);
    CHECK(output == input);

    p.dropper = 1.f;
    p.density = 0.f;
    output = input;
    channel = output.data();
    engine.process(&channel, 1, static_cast<int>(output.size()), p, t, nullptr, true);
    CHECK(output == input);

    engine.process(nullptr, 0, 0, p, t, nullptr, true);
}

void testScheduleMatchAndDeterminism() {
    auto p = paramsAt(0.72f);
    auto t = clockAt(48000.0);
    const auto a = renderGain(p, t, 42, 24000, false, 512);
    const auto b = renderGain(p, t, 42, 24000, false, 127);
    const auto c = renderGain(p, t, 42, 24000, false, 1);
    const auto d = renderGain(p, t, 99, 24000, false, 512);
    CHECK(a == b);
    CHECK(a == c);
    CHECK(a != d);
    CHECK(finiteInUnitInterval(a));

    dropper::Engine engine;
    prepare(engine, 42, 48000.0);
    const auto ctx = dropper::contextFor(p, t, 42);
    float worst = 0.f;
    for (int i = 0; i < static_cast<int>(a.size()); ++i) {
        const float expected = dropper::scheduleGain(ctx, static_cast<double>(i) / 48000.0);
        worst = std::max(worst, std::abs(a[static_cast<size_t>(i)] - expected));
    }
    std::printf("schedule match worst error %.3g\n", worst);
    CHECK(worst < 1.0e-5f);

    const auto protectedA = renderGain(p, t, 42, 8000, true, 64);
    const auto protectedB = renderGain(p, t, 42, 8000, true, 700);
    CHECK(protectedA == protectedB);
}

void testStereoAndMix() {
    auto p = paramsAt(0.8f);
    p.patternMode = 1;
    auto t = clockAt(44100.0);
    dropper::Engine engine;
    prepare(engine, 7, 44100.0);

    constexpr int n = 4000;
    std::vector<float> left(n), right(n), gain(n);
    for (int i = 0; i < n; ++i) {
        left[static_cast<size_t>(i)] = 0.2f * std::sin(0.03f * static_cast<float>(i));
        right[static_cast<size_t>(i)] = -0.55f + 0.001f * static_cast<float>(i % 17);
    }
    auto leftDry = left;
    auto rightDry = right;
    float* channels[2] = { left.data(), right.data() };
    engine.process(channels, 2, n, p, t, gain.data(), false);

    for (int i = 0; i < n; ++i) {
        const float g = gain[static_cast<size_t>(i)];
        CHECK(std::abs(left[static_cast<size_t>(i)] - leftDry[static_cast<size_t>(i)] * g) < 1.0e-5f);
        CHECK(std::abs(right[static_cast<size_t>(i)] - rightDry[static_cast<size_t>(i)] * g) < 1.0e-5f);
    }

    p.mix = 0.f;
    dropper::Engine dryEngine;
    prepare(dryEngine, 7, 44100.0);
    auto dry = leftDry;
    float* dryCh = dry.data();
    dryEngine.process(&dryCh, 1, n, p, t, nullptr, false);
    CHECK(dry == leftDry);

    p.mix = 0.5f;
    dropper::Engine mixEngine;
    prepare(mixEngine, 7, 44100.0);
    auto mixed = leftDry;
    float* mixCh = mixed.data();
    std::vector<float> mixGain(n);
    mixEngine.process(&mixCh, 1, n, p, t, mixGain.data(), false);
    float mixError = 0.f;
    for (int i = 0; i < n; ++i) {
        const float g = mixGain[static_cast<size_t>(i)];
        const float expected = leftDry[static_cast<size_t>(i)] * (1.f - 0.5f * (1.f - g));
        mixError = std::max(mixError, std::abs(mixed[static_cast<size_t>(i)] - expected));
    }
    CHECK(mixError < 1.0e-5f);
}

void testRepeatableLoopAndRegenerate() {
    auto p = paramsAt(0.66f);
    p.patternMode = 1;
    p.timingSync = 1;
    p.patternBars = 1.f;
    p.randomness = 0.2f;
    auto t = clockAt(48000.0, 120.0);
    t.hasHostPosition = true;
    t.isPlaying = true;
    t.ppqPosition = 0.0;

    const int cycle = 96000; // 4 beats at 120 bpm
    auto first = renderGain(p, t, 11, cycle, false, 512);
    t.ppqPosition = 4.0;
    dropper::Engine engine;
    prepare(engine, 11, 48000.0);
    // Render the first cycle so the cursor lands on the loop point, then the second.
    std::vector<float> audio(static_cast<size_t>(cycle), 0.2f);
    std::vector<float> gain(static_cast<size_t>(cycle));
    float* channel = audio.data();
    t.ppqPosition = 0.0;
    engine.process(&channel, 1, cycle, p, t, gain.data(), false);
    CHECK(maxAbsDiff(gain, first) < 1.0e-5f);

    t.ppqPosition = 4.0;
    std::vector<float> second(static_cast<size_t>(cycle));
    channel = audio.data();
    engine.process(&channel, 1, cycle, p, t, second.data(), false);
    const float loopError = maxAbsDiff(first, second);
    std::printf("repeatable loop error %.3g\n", loopError);
    CHECK(loopError < 1.0e-4f);

    dropper::Engine regen;
    prepare(regen, 11, 48000.0);
    regen.requestRegenerate();
    std::vector<float> changed(static_cast<size_t>(cycle));
    t.ppqPosition = 0.0;
    channel = audio.data();
    regen.process(&channel, 1, cycle, p, t, changed.data(), false);
    CHECK(regen.getSeed() == dropper::mixSeed(11));
    CHECK(maxAbsDiff(changed, first) > 0.05f);
    const auto seedAfter = regen.getSeed();
    t.ppqPosition = 4.0;
    regen.process(&channel, 1, 128, p, t, nullptr, false);
    CHECK(regen.getSeed() == seedAfter);
}

void testClicksAndHardCuts() {
    auto p = paramsAt(0.62f);
    p.fadeSec = 0.030f;
    auto t = clockAt(48000.0);
    const auto gain = renderGain(p, t, 5, 48000, true, 256);
    CHECK(finiteInUnitInterval(gain));

    float maxStep = 0.f;
    for (size_t i = 1; i < gain.size(); ++i)
        maxStep = std::max(maxStep, std::abs(gain[i] - gain[i - 1]));
    std::printf("smoothed max step %.4f\n", maxStep);
    CHECK(maxStep < 0.02f);

    p.fadeSec = 0.f;
    p.dropper = 0.06f;
    const auto hard = renderGain(p, t, 5, 48000, false, 256);
    CHECK(finiteInUnitInterval(hard));
    float hardStep = 0.f;
    for (size_t i = 1; i < hard.size(); ++i)
        hardStep = std::max(hardStep, std::abs(hard[i] - hard[i - 1]));
    std::printf("fastest edge max step %.4f\n", hardStep);
    CHECK(hardStep < 0.05f);

    p.dropper = 0.95f;
    p.degrade.enabled = true;
    p.degrade.ceiling = 1.f;
    p.degrade.initial = 1.f;
    p.degrade.timeSec = 30.f;
    p.degrade.recovery = 1.f;
    const auto worn = renderGain(p, t, 5, 48000, false, 256);
    CHECK(finiteInUnitInterval(worn));
    float wornStep = 0.f;
    for (size_t i = 1; i < worn.size(); ++i)
        wornStep = std::max(wornStep, std::abs(worn[i] - worn[i - 1]));
    std::printf("worn edge max step %.4f\n", wornStep);
    CHECK(wornStep > 0.001f);
    CHECK(wornStep < 0.02f);
}

void testTransport() {
    auto p = paramsAt(0.7f);
    p.timingSync = 1;
    p.patternMode = 1;
    p.fadeSec = 0.012f;
    auto t = clockAt(48000.0, 120.0);
    t.hasHostPosition = true;
    t.isPlaying = true;
    t.ppqPosition = 0.25;

    dropper::Engine engine;
    prepare(engine, 21, 48000.0);
    std::vector<float> audio(4096, 0.4f);
    std::vector<float> gain(4096);
    float* channel = audio.data();
    engine.process(&channel, 1, 2048, p, t, gain.data(), true);
    const float before = gain[2047];

    t.ppqPosition = 18.0; // seek
    engine.process(&channel, 1, 1024, p, t, gain.data(), true);
    CHECK(finiteInUnitInterval(std::vector<float>(gain.begin(), gain.begin() + 1024)));
    CHECK(std::abs(gain[0] - before) < 0.05f);

    const double playedBeats = (120.0 / 60.0) * 1024.0 / 48000.0;
    t.isPlaying = false;
    t.ppqPosition = 18.0 + playedBeats;
    std::vector<float> held(512);
    engine.process(&channel, 1, 512, p, t, held.data(), true);
    for (size_t i = 1; i < held.size(); ++i)
        CHECK(held[i] == held[0]);

    t.isLooping = true;
    t.ppqLoopStart = 0.0;
    t.ppqLoopEnd = 4.0;
    t.isPlaying = true;
    t.ppqPosition = 3.95;
    engine.process(&channel, 1, 256, p, t, nullptr, true);
    t.ppqPosition = 0.02;
    std::vector<float> looped(256);
    engine.process(&channel, 1, 256, p, t, looped.data(), true);
    CHECK(finiteInUnitInterval(looped));

    t.bpm = 0.0;
    t.ppqPosition = -3.5;
    t.timeSigNumerator = 7;
    t.timeSigDenominator = 8;
    std::vector<float> wild(128);
    engine.process(&channel, 1, 128, p, t, wild.data(), true);
    CHECK(finiteInUnitInterval(wild));
}

void testBoundariesAndCharacter() {
    auto t = clockAt(192000.0, 20.0);
    auto p = paramsAt(1.f);
    p.minDurationSec = 0.005f;
    p.maxDurationSec = 0.001f; // reversed on purpose
    p.fadeSec = 0.2f;
    p.randomness = 2.f;
    p.patternBars = 9.f;
    p.density = 3.f;
    p.dropper = 4.f;
    p.mix = -1.f;
    p.patternMode = 1;
    p.timingSync = 1;
    p.subdivision = 99;
    t.hasHostPosition = true;
    const auto extreme = renderGain(p, t, 1, 4096, true, 17);
    CHECK(finiteInUnitInterval(extreme));

    auto lowP = paramsAt(0.28f);
    auto highP = paramsAt(1.0f);
    lowP.fadeSec = 0.004f;
    highP.fadeSec = 0.004f;
    auto rate = clockAt(48000.0);
    const auto low = renderGain(lowP, rate, 77, 96000, false, 480);
    const auto high = renderGain(highP, rate, 77, 96000, false, 480);

    auto duty = [](const std::vector<float>& g, float threshold) {
        int n = 0;
        for (float v : g)
            if (v < threshold)
                ++n;
        return static_cast<double>(n) / static_cast<double>(g.size());
    };
    auto onsets = [](const std::vector<float>& g) {
        int n = 0;
        for (size_t i = 1; i < g.size(); ++i)
            if (g[i - 1] > 0.5f && g[i] <= 0.5f)
                ++n;
        return n;
    };
    auto medianDropout = [](const std::vector<float>& g) {
        std::vector<int> runs;
        int run = 0;
        for (float v : g) {
            if (v < 0.5f)
                ++run;
            else if (run > 0) {
                runs.push_back(run);
                run = 0;
            }
        }
        if (run > 0)
            runs.push_back(run);
        if (runs.empty())
            return 1000000;
        std::sort(runs.begin(), runs.end());
        return runs[runs.size() / 2];
    };
    const double lowDuty = duty(low, 0.2f);
    const double highDuty = duty(high, 0.2f);
    const int lowOnsets = onsets(low);
    const int highOnsets = onsets(high);
    const int lowMedian = medianDropout(low);
    const int highMedian = medianDropout(high);
    std::printf("duty low %.3f high %.3f | onsets low %d high %d | median dropout %d -> %d\n",
                lowDuty, highDuty, lowOnsets, highOnsets, lowMedian, highMedian);
    CHECK(highOnsets > lowOnsets);
    CHECK(highOnsets < 40); // a few more holes, not a buzz
    CHECK(highMedian > lowMedian);
    CHECK(highDuty > lowDuty);
    CHECK(duty(high, 0.85f) > 0.04); // the loop is still audible between holes
    CHECK(highDuty > 0.08);

    auto gaps = [](const std::vector<float>& g) {
        std::vector<double> intervals;
        int previous = -1;
        for (int i = 1; i < static_cast<int>(g.size()); ++i) {
            if (g[static_cast<size_t>(i - 1)] > 0.5f && g[static_cast<size_t>(i)] <= 0.5f) {
                if (previous >= 0)
                    intervals.push_back(static_cast<double>(i - previous));
                previous = i;
            }
        }
        return intervals;
    };
    auto coefficient = [](const std::vector<double>& values) {
        if (values.size() < 4)
            return 10.0;
        double mean = 0.0;
        for (double v : values)
            mean += v;
        mean /= static_cast<double>(values.size());
        double var = 0.0;
        for (double v : values) {
            const double d = v - mean;
            var += d * d;
        }
        var /= static_cast<double>(values.size());
        return std::sqrt(var) / std::max(1.0, mean);
    };

    auto clocked = clockAt(48000.0, 120.0);
    clocked.hasHostPosition = true;
    clocked.isPlaying = true;
    auto steady = paramsAt(1.f);
    steady.timingSync = 1;
    steady.patternMode = 1;
    steady.randomness = 0.f;
    steady.fadeSec = 0.002f;
    steady.subdivision = 2;
    auto scattered = steady;
    scattered.randomness = 1.f;
    const auto steadyGain = renderGain(steady, clocked, 15, 384000, false, 512);
    clocked.ppqPosition = 0.0;
    const auto scatteredGain = renderGain(scattered, clocked, 15, 384000, false, 512);
    const double steadyCv = coefficient(gaps(steadyGain));
    const double scatteredCv = coefficient(gaps(scatteredGain));
    std::printf("interval cv steady %.3f scattered %.3f\n", steadyCv, scatteredCv);
    CHECK(steadyCv + 0.05 < scatteredCv);
}

void writeWav(const std::filesystem::path& path, const std::vector<float>& interleaved, int channels, int sampleRate) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    const auto frames = static_cast<std::uint32_t>(interleaved.size() / static_cast<size_t>(channels));
    const std::uint32_t dataBytes = frames * static_cast<std::uint32_t>(channels) * 2u;
    const std::uint32_t chunk = 36u + dataBytes;
    const auto write = [&out](auto value) {
        out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    out.write("RIFF", 4);
    write(chunk);
    out.write("WAVEfmt ", 8);
    write(std::uint32_t { 16 });
    write(std::uint16_t { 1 });
    write(static_cast<std::uint16_t>(channels));
    write(static_cast<std::uint32_t>(sampleRate));
    write(static_cast<std::uint32_t>(sampleRate * channels * 2));
    write(static_cast<std::uint16_t>(channels * 2));
    write(std::uint16_t { 16 });
    out.write("data", 4);
    write(dataBytes);
    for (float sample : interleaved) {
        const float clamped = std::max(-1.f, std::min(1.f, sample));
        const auto pcm = static_cast<std::int16_t>(std::lrint(clamped * 32767.f));
        write(pcm);
    }
}

float musicalSample(int channel, int index, double sampleRate) {
    const double t = static_cast<double>(index) / sampleRate;
    const double env = std::min(1.0, t * 6.0) * (0.65 + 0.35 * std::sin(2.0 * 3.141592653589793 * 1.5 * t));
    const double detune = channel == 0 ? 1.0 : 1.004;
    const double tone = 0.34 * std::sin(2.0 * 3.141592653589793 * 220.0 * detune * t)
                      + 0.22 * std::sin(2.0 * 3.141592653589793 * 277.18 * detune * t)
                      + 0.16 * std::sin(2.0 * 3.141592653589793 * 329.63 * detune * t);
    const double phase = std::fmod(t, 0.5);
    const double click = phase < 0.004 ? (1.0 - phase / 0.004) * 0.25 : 0.0;
    return static_cast<float>(env * tone + (channel == 0 ? click : click * 0.7));
}

double rmsOf(const std::vector<float>& interleaved) {
    double sum = 0.0;
    for (float s : interleaved)
        sum += static_cast<double>(s) * static_cast<double>(s);
    return std::sqrt(sum / static_cast<double>(interleaved.size()));
}

void testRealAudio(const std::filesystem::path& directory) {
    constexpr double sr = 48000.0;
    constexpr int frames = 48000 * 4;
    std::vector<float> dry(static_cast<size_t>(frames * 2));
    for (int i = 0; i < frames; ++i) {
        dry[static_cast<size_t>(i * 2)] = musicalSample(0, i, sr);
        dry[static_cast<size_t>(i * 2 + 1)] = musicalSample(1, i, sr);
    }

    auto render = [&](dropper::Parameters p, std::uint64_t seed) {
        dropper::Engine engine;
        prepare(engine, seed, sr);
        std::vector<float> left(frames), right(frames);
        for (int i = 0; i < frames; ++i) {
            left[static_cast<size_t>(i)] = dry[static_cast<size_t>(i * 2)];
            right[static_cast<size_t>(i)] = dry[static_cast<size_t>(i * 2 + 1)];
        }
        float* planar[2] = { left.data(), right.data() };
        std::vector<float> gain(static_cast<size_t>(frames));
        engine.process(planar, 2, frames, p, clockAt(sr), gain.data(), true);
        std::vector<float> interleaved(static_cast<size_t>(frames * 2));
        double peakIn = 0.0;
        double peakOut = 0.0;
        int mismatches = 0;
        for (int i = 0; i < frames; ++i) {
            interleaved[static_cast<size_t>(i * 2)] = left[static_cast<size_t>(i)];
            interleaved[static_cast<size_t>(i * 2 + 1)] = right[static_cast<size_t>(i)];
            peakIn = std::max(peakIn, static_cast<double>(std::abs(dry[static_cast<size_t>(i * 2)])));
            peakOut = std::max(peakOut, static_cast<double>(std::abs(left[static_cast<size_t>(i)])));
            const float inL = dry[static_cast<size_t>(i * 2)];
            const float inR = dry[static_cast<size_t>(i * 2 + 1)];
            if (std::abs(inL) > 1.0e-4f && std::abs(inR) > 1.0e-4f) {
                const float gL = left[static_cast<size_t>(i)] / inL;
                const float gR = right[static_cast<size_t>(i)] / inR;
                if (std::abs(gL - gR) > 2.0e-4f)
                    ++mismatches;
            }
            if (!std::isfinite(left[static_cast<size_t>(i)]) || !std::isfinite(right[static_cast<size_t>(i)]))
                ++mismatches;
        }
        CHECK(mismatches == 0);
        CHECK(peakOut <= peakIn + 1.0e-4);
        return interleaved;
    };

    auto bypassP = paramsAt(0.f);
    auto randomP = paramsAt(1.f);
    randomP.fadeSec = 0.006f;
    auto repeatP = paramsAt(1.f);
    repeatP.patternMode = 1;
    repeatP.timingSync = 0;
    repeatP.fadeSec = 0.004f;
    repeatP.randomness = 0.15f;

    const auto bypass = render(bypassP, 3);
    const auto random = render(randomP, 3);
    const auto repeat = render(repeatP, 3);
    const auto repeatAgain = render(repeatP, 3);

    CHECK(maxAbsDiff(bypass, dry) < 1.0e-6f);
    CHECK(repeat == repeatAgain);

    const double dryRms = rmsOf(dry);
    const double randomRms = rmsOf(random);
    const double repeatRms = rmsOf(repeat);
    std::printf("real audio rms dry %.4f random %.4f repeatable %.4f\n", dryRms, randomRms, repeatRms);
    CHECK(randomRms < dryRms * 0.85);
    CHECK(repeatRms < dryRms * 0.85);
    CHECK(randomRms > dryRms * 0.04);

    writeWav(directory / "dry.wav", dry, 2, 48000);
    writeWav(directory / "bypass.wav", bypass, 2, 48000);
    writeWav(directory / "random-full.wav", random, 2, 48000);
    writeWav(directory / "repeatable-full.wav", repeat, 2, 48000);
    std::printf("wrote verification audio to %s\n", directory.string().c_str());
}

dropper::Parameters degradeAt(int curve, float ceiling, float initial, float timeSec) {
    auto p = paramsAt(0.f);
    p.degrade.enabled = true;
    p.degrade.curve = curve;
    p.degrade.ceiling = ceiling;
    p.degrade.initial = initial;
    p.degrade.timeSec = timeSec;
    p.degrade.recovery = 1.f;
    return p;
}

dropper::Snapshot runEngine(dropper::Engine& engine, dropper::Parameters p, dropper::Transport t,
                            double seconds, int blockSize) {
    const int samples = std::max(1, static_cast<int>(std::lround(seconds * t.sampleRate)));
    std::vector<float> audio(static_cast<size_t>(std::max(blockSize, 1)), 0.2f);
    int done = 0;
    while (done < samples) {
        const int count = std::min(blockSize, samples - done);
        float* channel = audio.data();
        engine.process(&channel, 1, count, p, t, nullptr, false);
        done += count;
    }
    return engine.snapshot();
}

float averageGain(const std::vector<float>& gain) {
    double sum = 0.0;
    for (float g : gain)
        sum += g;
    return static_cast<float>(sum / static_cast<double>(gain.size()));
}

void testDegradation() {
    auto base = paramsAt(0.55f);
    auto timed = clockAt(48000.0);
    const auto plain = renderGain(base, timed, 11, 16000, false, 128);
    base.degrade.ceiling = 1.f;
    base.degrade.initial = 1.f;
    base.degrade.timeSec = 10.f;
    base.degrade.curve = static_cast<int>(dropper::DegradeCurve::Rapid);
    base.degrade.enabled = false;
    const auto disabled = renderGain(base, timed, 11, 16000, false, 128);
    CHECK(plain == disabled);

    const auto steadyEarly = dropper::degradationTarget(
        degradeAt(static_cast<int>(dropper::DegradeCurve::Steady), 1.f, 0.f, 10.f).degrade, 3.0, 5);
    const auto slowEarly = dropper::degradationTarget(
        degradeAt(static_cast<int>(dropper::DegradeCurve::SlowBurn), 1.f, 0.f, 10.f).degrade, 3.0, 5);
    const auto rapidEarly = dropper::degradationTarget(
        degradeAt(static_cast<int>(dropper::DegradeCurve::Rapid), 1.f, 0.f, 10.f).degrade, 3.0, 5);
    CHECK(rapidEarly > steadyEarly + 0.2f);
    CHECK(steadyEarly > slowEarly + 0.15f);
    CHECK(dropper::degradationTarget(degradeAt(1, 0.4f, 0.f, 10.f).degrade, 100.0, 5) <= 0.4f + 1.0e-5f);

    bool foundBurst = false;
    std::uint64_t burstSeed = 1;
    auto unstable = degradeAt(static_cast<int>(dropper::DegradeCurve::Unstable), 1.f, 0.f, 10.f).degrade;
    for (std::uint64_t seed = 1; seed < 48; ++seed) {
        const float mid = dropper::degradationTarget(unstable, 2.48, seed);
        const float before = dropper::degradationTarget(unstable, 2.0, seed);
        const float after = dropper::degradationTarget(unstable, 2.9, seed);
        if (mid > before + 0.2f && mid > after + 0.15f) {
            foundBurst = true;
            burstSeed = seed;
            break;
        }
    }
    CHECK(foundBurst);

    auto played = [&](dropper::Parameters p, std::uint64_t seed, double seconds) {
        dropper::Engine engine;
        prepare(engine, seed, 48000.0);
        return runEngine(engine, p, clockAt(48000.0), seconds, 256).degradation;
    };
    auto steady = degradeAt(static_cast<int>(dropper::DegradeCurve::Steady), 1.f, 0.f, 10.f);
    auto slow = degradeAt(static_cast<int>(dropper::DegradeCurve::SlowBurn), 1.f, 0.f, 10.f);
    auto rapid = degradeAt(static_cast<int>(dropper::DegradeCurve::Rapid), 1.f, 0.f, 10.f);
    const float steadyLevel = played(steady, 5, 3.0);
    const float slowLevel = played(slow, 5, 3.0);
    const float rapidLevel = played(rapid, 5, 3.0);
    std::printf("degrade curves slow %.3f steady %.3f rapid %.3f\n", slowLevel, steadyLevel, rapidLevel);
    CHECK(rapidLevel > steadyLevel + 0.15f);
    CHECK(steadyLevel > slowLevel + 0.08f);

    float previous = -1.f;
    for (int step = 1; step <= 8; ++step) {
        const float level = played(steady, 5, static_cast<double>(step));
        CHECK(level + 0.004f >= previous);
        previous = level;
    }
    CHECK(previous > 0.55f);

    auto unstableParams = degradeAt(static_cast<int>(dropper::DegradeCurve::Unstable), 1.f, 0.f, 10.f);
    const float burst = played(unstableParams, burstSeed, 2.48);
    const float afterBurst = played(unstableParams, burstSeed, 2.9);
    const float early = played(unstableParams, burstSeed, 1.0);
    CHECK(burst > afterBurst + 0.04f);
    CHECK(afterBurst > early);

    dropper::Engine held;
    prepare(held, 9, 48000.0);
    auto holdParams = degradeAt(1, 1.f, 0.f, 10.f);
    holdParams.dropper = 0.8f;
    runEngine(held, holdParams, clockAt(48000.0), 2.0, 256);
    const auto frozen = held.snapshot();
    holdParams.degrade.hold = true;
    std::vector<float> heldGain(48000, 0.f);
    std::vector<float> heldAudio(48000, 0.35f);
    float* heldChannel = heldAudio.data();
    held.process(&heldChannel, 1, 48000, holdParams, clockAt(48000.0), heldGain.data(), false);
    const auto afterHold = held.snapshot();
    CHECK(std::abs(afterHold.degradation - frozen.degradation) < 1.0e-4f);
    CHECK(std::abs(afterHold.degradationElapsed - frozen.degradationElapsed) < 1.0e-4);
    CHECK(maxAbsDiff(heldGain, std::vector<float>(heldGain.size(), heldGain.front())) > 0.2f);

    dropper::Engine resetEngine;
    prepare(resetEngine, 4, 48000.0);
    auto resetParams = degradeAt(1, 1.f, 0.15f, 10.f);
    runEngine(resetEngine, resetParams, clockAt(48000.0), 4.0, 256);
    CHECK(resetEngine.snapshot().degradation > 0.35f);
    resetEngine.requestDegradeReset();
    runEngine(resetEngine, resetParams, clockAt(48000.0), 64.0 / 48000.0, 64);
    const auto resetSnap = resetEngine.snapshot();
    std::printf("degrade reset level %.3f elapsed %.4f\n", resetSnap.degradation, resetSnap.degradationElapsed);
    CHECK(resetSnap.degradationElapsed < 0.02);
    CHECK(std::abs(resetSnap.degradation - 0.15f) < 0.06f);

    auto capped = degradeAt(1, 0.3f, 0.f, 10.f);
    CHECK(played(capped, 6, 16.0) <= 0.301f);
    CHECK(played(capped, 6, 16.0) >= 0.25f);

    dropper::Transport host = clockAt(48000.0);
    host.hasHostPosition = true;
    host.isPlaying = true;
    dropper::Engine pauseEngine;
    prepare(pauseEngine, 8, 48000.0);
    auto pauseParams = degradeAt(1, 1.f, 0.f, 10.f);
    runEngine(pauseEngine, pauseParams, host, 2.0, 256);
    const float running = pauseEngine.snapshot().degradation;
    host.isPlaying = false;
    runEngine(pauseEngine, pauseParams, host, 2.0, 256);
    const float paused = pauseEngine.snapshot().degradation;
    CHECK(std::abs(paused - running) < 1.0e-3f);
    host.isPlaying = true;
    host.ppqPosition = 40.0;
    runEngine(pauseEngine, pauseParams, host, 2.0, 256);
    CHECK(pauseEngine.snapshot().degradation > paused + 0.08f);

    dropper::Engine resumeEngine;
    prepare(resumeEngine, 8, 48000.0);
    host.isPlaying = true;
    host.ppqPosition = 0.0;
    auto resumeParams = pauseParams;
    runEngine(resumeEngine, resumeParams, host, 3.0, 256);
    const float remembered = resumeEngine.snapshot().degradation;
    host.isPlaying = false;
    runEngine(resumeEngine, resumeParams, host, 0.2, 128);
    host.isPlaying = true;
    host.ppqPosition = 0.0;
    runEngine(resumeEngine, resumeParams, host, 0.2, 128);
    CHECK(resumeEngine.snapshot().degradation > remembered - 0.02f);

    dropper::Engine seekA;
    dropper::Engine seekB;
    prepare(seekA, 3, 48000.0);
    prepare(seekB, 3, 48000.0);
    host.isPlaying = true;
    host.ppqPosition = 0.0;
    auto seekParams = pauseParams;
    runEngine(seekA, seekParams, host, 2.0, 200);
    runEngine(seekB, seekParams, host, 1.0, 200);
    host.ppqPosition = 120.0;
    runEngine(seekB, seekParams, host, 1.0, 200);
    CHECK(std::abs(seekA.snapshot().degradation - seekB.snapshot().degradation) < 0.02f);

    dropper::Engine source;
    prepare(source, 21, 48000.0);
    auto savedParams = degradeAt(1, 0.9f, 0.1f, 12.f);
    runEngine(source, savedParams, clockAt(48000.0), 3.0, 300);
    const auto saved = source.degradationState();
    const auto savedSeed = source.getSeed();
    dropper::Engine restored;
    prepare(restored, 1, 48000.0);
    restored.setSeed(savedSeed);
    restored.restoreDegradationState(saved);
    CHECK(std::abs(restored.degradationState().elapsed - saved.elapsed) < 1.0e-6);
    CHECK(std::abs(restored.degradationState().level - saved.level) < 1.0e-6f);
    runEngine(source, savedParams, clockAt(48000.0), 0.5, 128);
    runEngine(restored, savedParams, clockAt(48000.0), 0.5, 128);
    CHECK(std::abs(source.snapshot().degradation - restored.snapshot().degradation) < 0.03f);
    CHECK(std::abs(source.snapshot().degradationElapsed - restored.snapshot().degradationElapsed) < 0.02);

    auto dry = degradeAt(1, 1.f, 1.f, 10.f);
    dry.dropper = 0.f;
    const auto dryGain = renderGain(dry, clockAt(48000.0), 18, 24000, false, 256);
    CHECK(averageGain(dryGain) > 0.98f);

    auto fresh = paramsAt(0.5f);
    fresh.fadeSec = 0.004f;
    auto aged = fresh;
    aged.degrade.enabled = true;
    aged.degrade.ceiling = 1.f;
    aged.degrade.initial = 1.f;
    aged.degrade.timeSec = 10.f;
    aged.degrade.curve = static_cast<int>(dropper::DegradeCurve::Steady);
    aged.degrade.recovery = 1.f;
    const auto freshGain = renderGain(fresh, clockAt(48000.0), 18, 96000, false, 256);
    const auto agedGain = renderGain(aged, clockAt(48000.0), 18, 96000, false, 256);
    auto onsets = [](const std::vector<float>& g) {
        int n = 0;
        for (size_t i = 1; i < g.size(); ++i)
            if (g[i - 1] > 0.5f && g[i] <= 0.5f)
                ++n;
        return n;
    };
    auto medianDropout = [](const std::vector<float>& g) {
        std::vector<int> runs;
        int run = 0;
        for (float v : g) {
            if (v < 0.5f)
                ++run;
            else if (run > 0) {
                runs.push_back(run);
                run = 0;
            }
        }
        if (run > 0)
            runs.push_back(run);
        if (runs.empty())
            return 0;
        std::sort(runs.begin(), runs.end());
        return runs[runs.size() / 2];
    };
    const int freshOnsets = onsets(freshGain);
    const int agedOnsets = onsets(agedGain);
    const int freshMedian = medianDropout(freshGain);
    const int agedMedian = medianDropout(agedGain);
    const float freshMean = averageGain(freshGain);
    const float agedMean = averageGain(agedGain);
    std::printf("degrade onsets %d -> %d | median %d -> %d | mean %.3f -> %.3f\n",
                freshOnsets, agedOnsets, freshMedian, agedMedian, freshMean, agedMean);
    CHECK(freshOnsets > 2);
    CHECK(agedOnsets <= freshOnsets + freshOnsets / 5 + 2);
    CHECK(agedMedian > freshMedian);
    CHECK(agedMean < freshMean - 0.04f);

    auto manual = degradeAt(1, 1.f, 0.f, 60.f);
    manual.degrade.manual = true;
    manual.degrade.manualLevel = 1.f;
    manual.degrade.recovery = 0.f;
    dropper::Engine manualEngine;
    prepare(manualEngine, 2, 48000.0);
    runEngine(manualEngine, manual, clockAt(48000.0), 0.25, 256);
    CHECK(manualEngine.snapshot().degradation > 0.8f);
    manual.degrade.manual = false;
    runEngine(manualEngine, manual, clockAt(48000.0), 0.3, 256);
    CHECK(manualEngine.snapshot().degradation > 0.55f);
    manual.degrade.recovery = 1.f;
    runEngine(manualEngine, manual, clockAt(48000.0), 1.0, 256);
    CHECK(manualEngine.snapshot().degradation < 0.2f);

    auto varied = degradeAt(1, 1.f, 0.2f, 10.f);
    varied.dropper = 0.4f;
    const auto blocksA = renderGain(varied, clockAt(48000.0), 27, 6000, false, 1);
    const auto blocksB = renderGain(varied, clockAt(48000.0), 27, 6000, false, 512);
    CHECK(maxAbsDiff(blocksA, blocksB) < 1.0e-4f);
    CHECK(finiteInUnitInterval(blocksA));

    for (double sr : { 44100.0, 96000.0 }) {
        auto t = clockAt(sr);
        auto p = degradeAt(1, 1.f, 0.5f, 10.f);
        p.dropper = 0.4f;
        p.stability.wow = 0.f;
        p.stability.noise = 0.f;
        p.stability.freqLoss = 0.f;
        p.stability.saturation = false;
        p.stability.amplitude = 0.f;
        const auto gain = renderGain(p, t, 13, static_cast<int>(sr * 0.25), true, 127);
        CHECK(finiteInUnitInterval(gain));
        dropper::Engine stereo;
        prepare(stereo, 13, sr);
        const int n = 1500;
        std::vector<float> left(n, 0.4f), right(n, -0.25f), gainOut(n);
        float* channels[2] = { left.data(), right.data() };
        t.sampleRate = sr;
        stereo.process(channels, 2, n, p, t, gainOut.data(), true);
        float mismatch = 0.f;
        for (int i = 0; i < n; ++i) {
            const float leftGain = left[static_cast<size_t>(i)] / 0.4f;
            const float rightGain = right[static_cast<size_t>(i)] / -0.25f;
            mismatch = std::max(mismatch, std::abs(leftGain - rightGain));
        }
        CHECK(mismatch < 1.0e-4f);
    }

}

} // namespace

float rmsOf(const std::vector<float>& audio, int start, int count) {
    double sum = 0.0;
    const int end = std::min(static_cast<int>(audio.size()), start + count);
    const int n = std::max(0, end - start);
    if (n <= 0)
        return 0.f;
    for (int i = start; i < end; ++i)
        sum += static_cast<double>(audio[static_cast<size_t>(i)]) * audio[static_cast<size_t>(i)];
    return static_cast<float>(std::sqrt(sum / n));
}

std::vector<float> sine(int samples, double sr, double hz, float amplitude) {
    std::vector<float> audio(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i)
        audio[static_cast<size_t>(i)] = amplitude * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * hz * i / sr));
    return audio;
}

std::vector<float> processAudio(dropper::Parameters p, std::uint64_t seed, std::vector<float> audio, int block) {
    dropper::Engine engine;
    prepare(engine, seed, 48000.0);
    auto transport = clockAt(48000.0);
    int done = 0;
    while (done < static_cast<int>(audio.size())) {
        const int count = std::min(block, static_cast<int>(audio.size()) - done);
        float* channel = audio.data() + done;
        engine.process(&channel, 1, count, p, transport, nullptr, false);
        done += count;
    }
    return audio;
}

void testStability() {
    auto dry = paramsAt(0.f);
    std::vector<float> input { 0.f, -0.4f, 0.25f, 0.9f, -0.05f, 0.333f, 1.0e-8f, -1.f };
    dropper::Engine bypass;
    prepare(bypass, 3, 48000.0);
    auto output = input;
    float* channel = output.data();
    bypass.process(&channel, 1, static_cast<int>(output.size()), dry, clockAt(48000.0), nullptr, false);
    CHECK(output == input);

    auto moving = dry;
    moving.stability.amount = 0.75f;
    moving.stability.wow = 0.8f;
    moving.stability.flutter = 0.7f;
    moving.stability.noiseEnabled = true;
    moving.stability.noise = 0.5f;
    moving.stability.freqLoss = 0.4f;
    moving.stability.amplitude = 0.4f;
    moving.stability.saturation = true;
    const auto tone = sine(8000, 48000.0, 440.0, 0.4f);
    const auto whole = processAudio(moving, 9, tone, 8000);
    const auto split = processAudio(moving, 9, tone, 128);
    std::printf("stability block mismatch %.8f\n", maxAbsDiff(whole, split));
    CHECK(maxAbsDiff(whole, split) < 1.0e-5f);

    auto quiet = dry;
    quiet.stability.amount = 0.8f;
    quiet.stability.noiseEnabled = false;
    quiet.stability.noise = 1.f;
    quiet.stability.saturation = false;
    quiet.stability.amplitude = 0.f;
    const auto silenced = processAudio(quiet, 4, std::vector<float>(12000, 0.f), 256);
    CHECK(rmsOf(silenced, 2000, 8000) < 1.0e-5f);
    quiet.stability.amount = 0.f;
    quiet.stability.noiseEnabled = true;
    const auto bypassedNoise = processAudio(quiet, 4, std::vector<float>(4000, 0.f), 256);
    CHECK(rmsOf(bypassedNoise, 0, 4000) == 0.f);
    quiet.stability.amount = 0.85f;
    quiet.stability.noiseEnabled = true;
    quiet.stability.noise = 1.f;
    const auto hiss = processAudio(quiet, 4, std::vector<float>(16000, 0.f), 256);
    const float hissRms = rmsOf(hiss, 4000, 8000);
    std::printf("stability hiss rms %.6f\n", hissRms);
    CHECK(hissRms > 0.0003f);
    auto gated = quiet;
    gated.dropper = 1.f;
    gated.fadeSec = 0.f;
    gated.minDurationSec = 0.020f;
    gated.maxDurationSec = 0.080f;
    const auto gatedHiss = processAudio(gated, 4, std::vector<float>(16000, 0.f), 256);
    const float gatedRms = rmsOf(gatedHiss, 4000, 8000);
    std::printf("stability dropped hiss rms %.6f\n", gatedRms);
    CHECK(gatedRms < hissRms * 0.75f);

    auto dull = dry;
    dull.stability.amount = 1.f;
    dull.stability.wow = 0.f;
    dull.stability.flutter = 0.f;
    dull.stability.noiseEnabled = false;
    dull.stability.amplitude = 0.f;
    dull.stability.saturation = false;
    dull.stability.freqLossMax = 1.f;
    const auto brightIn = sine(20000, 48000.0, 8000.0, 0.45f);
    dull.stability.freqLoss = 0.f;
    const auto openTone = processAudio(dull, 6, brightIn, 256);
    dull.stability.freqLoss = 1.f;
    const auto darkTone = processAudio(dull, 6, brightIn, 256);
    const float openRms = rmsOf(openTone, 4000, 12000);
    const float darkRms = rmsOf(darkTone, 4000, 12000);
    std::printf("stability tone rms open %.4f dark %.4f\n", openRms, darkRms);
    CHECK(darkRms < openRms * 0.55f);
    const auto lowIn = sine(20000, 48000.0, 180.0, 0.45f);
    dull.stability.freqLoss = 0.f;
    const auto lowOpen = processAudio(dull, 6, lowIn, 256);
    dull.stability.freqLoss = 1.f;
    const auto lowDark = processAudio(dull, 6, lowIn, 256);
    CHECK(rmsOf(lowDark, 4000, 12000) > rmsOf(lowOpen, 4000, 12000) * 0.8f);

    auto aged = dry;
    aged.stability.amount = 0.f;
    aged.stability.noiseEnabled = false;
    aged.stability.generational = true;
    aged.stability.wearAmount = 1.f;
    aged.stability.retention = 0.92f;
    aged.stability.generations = 0;
    aged.stability.saturation = true;
    const int loop = static_cast<int>(std::lround(48000.0 * dropper::kGenerationLoopSec));
    std::vector<float> passage(static_cast<size_t>(loop * 6), 0.f);
    const auto head = sine(loop, 48000.0, 6000.0, 0.4f);
    std::copy(head.begin(), head.end(), passage.begin());
    aged.stability.retention = 1.f;
    const auto worn = processAudio(aged, 8, passage, 256);
    const float earlyGen = rmsOf(worn, loop, loop);
    const float lateGen = rmsOf(worn, loop * 5, loop);
    std::printf("stability generations rms %.4f -> %.4f count path\n", earlyGen, lateGen);
    CHECK(lateGen < earlyGen * 0.55f);

    dropper::Engine resetEngine;
    prepare(resetEngine, 8, 48000.0);
    auto transport = clockAt(48000.0);
    std::vector<float> resetAudio = sine(loop * 4, 48000.0, 6000.0, 0.4f);
    int done = 0;
    while (done < loop * 3) {
        const int count = std::min(256, loop * 3 - done);
        float* ch = resetAudio.data() + done;
        resetEngine.process(&ch, 1, count, aged, transport, nullptr, false);
        done += count;
    }
    CHECK(resetEngine.generationCount() >= 2);
    const float beforeReset = rmsOf(resetAudio, loop * 2, loop);
    resetEngine.requestGenerationReset();
    while (done < loop * 4) {
        const int count = std::min(256, loop * 4 - done);
        float* ch = resetAudio.data() + done;
        resetEngine.process(&ch, 1, count, aged, transport, nullptr, false);
        done += count;
    }
    const float afterReset = rmsOf(resetAudio, loop * 3, loop);
    std::printf("stability reset rms %.4f -> %.4f generations %d\n",
                beforeReset, afterReset, resetEngine.generationCount());
    CHECK(afterReset > beforeReset * 1.15f);
    CHECK(resetEngine.generationCount() < 3);

    auto runaway = moving;
    runaway.stability.amount = 1.f;
    runaway.stability.wow = 1.f;
    runaway.stability.flutter = 1.f;
    runaway.stability.noise = 1.f;
    runaway.stability.noiseEnabled = true;
    runaway.stability.freqLoss = 1.f;
    runaway.stability.amplitude = 1.f;
    runaway.stability.saturation = true;
    runaway.stability.generational = true;
    runaway.stability.wearAmount = 1.f;
    runaway.stability.retention = 1.f;
    runaway.stability.generations = 0;
    const auto slammed = processAudio(runaway, 12, std::vector<float>(48000, 1.f), 512);
    float peak = 0.f;
    bool finite = true;
    for (float s : slammed) {
        peak = std::max(peak, std::abs(s));
        finite = finite && std::isfinite(s);
    }
    std::printf("stability peak %.4f\n", peak);
    CHECK(finite);
    CHECK(peak < 1.5f);

    auto dropping = paramsAt(1.f);
    dropping.stability.amount = 0.f;
    const auto dropped = processAudio(dropping, 15, std::vector<float>(48000, 0.5f), 256);
    CHECK(rmsOf(dropped, 0, 48000) < 0.42f);

    dropper::Engine remembered;
    prepare(remembered, 21, 48000.0);
    auto rememberedAudio = sine(6000, 48000.0, 330.0, 0.3f);
    float* rememberedCh = rememberedAudio.data();
    auto rememberedParams = moving;
    rememberedParams.stability.generational = true;
    rememberedParams.stability.wearAmount = 0.7f;
    remembered.process(&rememberedCh, 1, 6000, rememberedParams, clockAt(48000.0), nullptr, false);
    const auto saved = remembered.stabilityState();
    dropper::Engine restored;
    prepare(restored, 1, 48000.0);
    restored.restoreStabilityState(saved);
    std::vector<float> nextA = sine(1500, 48000.0, 330.0, 0.3f);
    std::vector<float> nextB = nextA;
    float* aCh = nextA.data();
    float* bCh = nextB.data();
    remembered.process(&aCh, 1, 1500, rememberedParams, clockAt(48000.0), nullptr, false);
    restored.process(&bCh, 1, 1500, rememberedParams, clockAt(48000.0), nullptr, false);
    CHECK(maxAbsDiff(nextA, nextB) < 1.0e-5f);

    dropper::Engine stereo;
    prepare(stereo, 9, 48000.0);
    std::vector<float> left = sine(3000, 48000.0, 500.0, 0.35f);
    std::vector<float> right = left;
    float* channels[2] = { left.data(), right.data() };
    stereo.process(channels, 2, 3000, moving, clockAt(48000.0), nullptr, false);
    CHECK(maxAbsDiff(left, right) < 1.0e-5f);
}

int impulseDelay(const dropper::Parameters& params, std::uint64_t seed) {
    std::vector<float> impulse(16000, 0.f);
    impulse[1000] = 1.f;
    const auto delayed = processAudio(params, seed, impulse, 128);
    int peakAt = 0;
    float peak = 0.f;
    for (int i = 0; i < static_cast<int>(delayed.size()); ++i) {
        if (std::abs(delayed[static_cast<size_t>(i)]) > peak) {
            peak = std::abs(delayed[static_cast<size_t>(i)]);
            peakAt = i;
        }
    }
    return peakAt - 1000;
}

void testStabilityPitch() {
    dropper::Parameters toneParams;
    toneParams.stability.amount = 1.f;
    toneParams.stability.wow = 0.6f;
    toneParams.stability.flutter = 0.f;
    toneParams.stability.noiseEnabled = false;
    toneParams.stability.freqLoss = 0.f;
    toneParams.stability.amplitude = 0.f;
    toneParams.stability.saturation = false;
    const auto dry = sine(48000, 48000.0, 440.0, 0.5f);
    const auto wet = processAudio(toneParams, 3, dry, 256);
    auto crossings = [](const std::vector<float>& audio, int start, int count) {
        int n = 0;
        const int end = std::min(start + count, static_cast<int>(audio.size()));
        for (int i = start + 1; i < end; ++i)
            if (audio[static_cast<size_t>(i - 1)] <= 0.f && audio[static_cast<size_t>(i)] > 0.f)
                ++n;
        return n;
    };
    const int dryCross = crossings(dry, 8000, 32000);
    const int wetCross = crossings(wet, 8000, 32000);
    const double ratio = dryCross > 0 ? static_cast<double>(wetCross) / dryCross : 0.0;
    std::printf("stability pitch crossings dry %d wet %d ratio %.3f\n", dryCross, wetCross, ratio);
    CHECK(ratio > 0.90 && ratio < 1.10);

    const int fullDelay = impulseDelay(toneParams, 4);
    std::printf("stability impulse delay %d\n", fullDelay);
    CHECK(fullDelay > 300 && fullDelay < 520);

    toneParams.stability.amount = 0.5f;
    const int partialDelay = impulseDelay(toneParams, 5);
    std::printf("stability partial delay %d\n", partialDelay);
    CHECK(partialDelay > 250 && partialDelay < 560);
}

int main(int argc, char** argv) {
    const std::filesystem::path directory = argc > 1 ? argv[1] : "verify-audio";
    testBypass();
    testScheduleMatchAndDeterminism();
    testStereoAndMix();
    testRepeatableLoopAndRegenerate();
    testClicksAndHardCuts();
    testTransport();
    testBoundariesAndCharacter();
    testDegradation();
    testStability();
    testStabilityPitch();
    testRealAudio(directory);
    if (gFailures != 0) {
        std::printf("%d checks failed\n", gFailures);
        return 1;
    }
    std::printf("all dropper checks passed\n");
    return 0;
}
