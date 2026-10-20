#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cstring>
#include <vector>

namespace {
constexpr int kParamVersion = 1;

juce::StringArray subdivisionChoices() {
    return { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
             "1/4T", "1/8T", "1/16T", "1/8D", "1/16D" };
}

float raw(const juce::AudioProcessorValueTreeState& apvts, const char* id) {
    return apvts.getRawParameterValue(id)->load();
}
} // namespace

DropperAudioProcessor::DropperAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()) {
    setLatencySamples(0);
}

juce::AudioProcessorValueTreeState::ParameterLayout DropperAudioProcessor::createParameterLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "dropper", 2 },
        "Drops",
        NormalisableRange<float> { 0.f, 100.f, 0.1f },
        0.f,
        AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "density", kParamVersion },
        "Drop Density",
        NormalisableRange<float> { 0.f, 100.f, 0.1f },
        29.2f, // 10 o'clock on the default rotary sweep
        AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "minDuration", kParamVersion },
        "Min Duration",
        NormalisableRange<float> { 1.f, 5.f, 0.01f },
        2.f,
        AudioParameterFloatAttributes().withLabel("ms")));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "maxDuration", kParamVersion },
        "Max Duration",
        NormalisableRange<float> { 5.f, 250.f, 0.1f, 0.55f },
        90.f,
        AudioParameterFloatAttributes().withLabel("ms")));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "fade", kParamVersion },
        "Fade",
        NormalisableRange<float> { 0.f, 100.f, 0.1f, 0.5f },
        100.f,
        AudioParameterFloatAttributes().withLabel("ms")));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "randomness", kParamVersion },
        "Drift",
        NormalisableRange<float> { 0.f, 100.f, 0.1f },
        50.f,
        AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID { "patternLength", kParamVersion },
        "Pattern Length",
        1, 4, 1,
        AudioParameterIntAttributes().withLabel("bars")));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID { "sync", kParamVersion },
        "Tempo Sync",
        false));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID { "subdivision", kParamVersion },
        "Subdivision",
        subdivisionChoices(),
        3));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID { "mode", kParamVersion },
        "Pattern Mode",
        StringArray { "Variable", "Fixed" },
        0));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID { "midiRetrigger", kParamVersion },
        "MIDI Retrigger",
        false));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "mix", kParamVersion },
        "Mix",
        NormalisableRange<float> { 0.f, 100.f, 0.1f },
        100.f,
        AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID { "degrade", kParamVersion },
        "Destroy",
        false));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "degradeAmount", kParamVersion },
        "The End",
        NormalisableRange<float> { 0.f, 100.f, 0.1f },
        80.f,
        AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "degradeTime", kParamVersion },
        "Time to Degrade",
        NormalisableRange<float> { 10.f, 600.f, 0.1f, 0.35f },
        60.f,
        AudioParameterFloatAttributes().withLabel("s")));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "degradeRecovery", kParamVersion },
        "Recovery",
        NormalisableRange<float> { 0.f, 100.f, 0.1f },
        35.f,
        AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID { "degradeCurve", kParamVersion },
        "Degradation Curve",
        StringArray { "Slow Burn", "Steady Decay", "Rapid Collapse", "Unstable" },
        1));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID { "degradeHold", kParamVersion },
        "Hold",
        false));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID { "degradeReset", kParamVersion },
        "Reset Degradation",
        false));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID { "degradeManualOn", kParamVersion },
        "Manual Degradation",
        false));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "degradeManual", kParamVersion },
        "Manual Level",
        NormalisableRange<float> { 0.f, 100.f, 0.1f },
        0.f,
        AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "stability", kParamVersion },
        "Amount",
        NormalisableRange<float> { 0.f, 100.f, 0.1f },
        0.f,
        AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "wowDepth", kParamVersion },
        "Wander",
        NormalisableRange<float> { 0.f, 100.f, 0.1f },
        60.f,
        AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "noise", kParamVersion },
        "Noise Level",
        NormalisableRange<float> { 0.f, 100.f, 0.1f },
        35.f,
        AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "freqLoss", kParamVersion },
        "Frequency Loss",
        NormalisableRange<float> { 0.f, 100.f, 0.1f },
        40.f,
        AudioParameterFloatAttributes().withLabel("%")));

    return layout;
}

void DropperAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine.prepare(sampleRate, samplesPerBlock);
    regenerateCcHigh = false;
    degradeResetHigh = false;
    // Must match the latency processBlock will report. Forcing 0 here makes the
    // host re-prepare, processBlock reports the delay again, and playback restarts
    // on every block while Destabilize is up.
    const float amount = raw(apvts, "stability") / 100.f;
    setLatencySamples(engine.stabilityLatencySamples(amount));
}

void DropperAudioProcessor::releaseResources() {}

bool DropperAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

dropper::Parameters DropperAudioProcessor::readParameters() const {
    dropper::Parameters p;
    p.dropper = raw(apvts, "dropper") / 100.f;
    p.density = raw(apvts, "density") / 100.f;
    p.minDurationSec = raw(apvts, "minDuration") / 1000.f;
    p.maxDurationSec = raw(apvts, "maxDuration") / 1000.f;
    p.fadeSec = raw(apvts, "fade") / 1000.f;
    p.randomness = raw(apvts, "randomness") / 100.f;
    p.patternBars = raw(apvts, "patternLength");
    p.mix = raw(apvts, "mix") / 100.f;
    p.timingSync = raw(apvts, "sync") >= 0.5f ? 1 : 0;
    p.subdivision = static_cast<int>(raw(apvts, "subdivision"));
    p.patternMode = static_cast<int>(raw(apvts, "mode"));
    p.degrade.enabled = raw(apvts, "degrade") >= 0.5f;
    p.degrade.ceiling = raw(apvts, "degradeAmount") / 100.f;
    p.degrade.timeSec = raw(apvts, "degradeTime");
    p.degrade.initial = 0.f;
    p.degrade.recovery = raw(apvts, "degradeRecovery") / 100.f;
    p.degrade.curve = static_cast<int>(raw(apvts, "degradeCurve"));
    p.degrade.hold = raw(apvts, "degradeHold") >= 0.5f;
    p.degrade.manual = raw(apvts, "degradeManualOn") >= 0.5f;
    p.degrade.manualLevel = raw(apvts, "degradeManual") / 100.f;
    p.stability.amount = raw(apvts, "stability") / 100.f;
    p.stability.wow = raw(apvts, "wowDepth") / 100.f;
    p.stability.noiseEnabled = true;
    p.stability.noise = raw(apvts, "noise") / 100.f;
    p.stability.freqLoss = raw(apvts, "freqLoss") / 100.f;
    p.stability.freqLossMax = 1.f;
    p.stability.amplitude = 0.f;
    p.stability.saturation = true;
    p.stability.generational = false;
    return p;
}

dropper::Transport DropperAudioProcessor::readTransport() const {
    dropper::Transport t;
    t.sampleRate = getSampleRate();
    t.bpm = 120.0;
    t.isPlaying = false;
    t.hasHostPosition = false;
    t.timeSigNumerator = 4;
    t.timeSigDenominator = 4;

    if (auto* hostPlayHead = getPlayHead()) {
        if (auto pos = hostPlayHead->getPosition()) {
            t.bpm = pos->getBpm().orFallback(120.0);
            if (auto ppq = pos->getPpqPosition()) {
                t.ppqPosition = *ppq;
                t.hasHostPosition = true;
            }
            t.isPlaying = pos->getIsPlaying();
            t.isLooping = pos->getIsLooping();
            if (auto sig = pos->getTimeSignature()) {
                t.timeSigNumerator = sig->numerator;
                t.timeSigDenominator = sig->denominator;
            }
            if (auto loop = pos->getLoopPoints()) {
                t.ppqLoopStart = loop->ppqStart;
                t.ppqLoopEnd = loop->ppqEnd;
            }
        }
    }
    return t;
}

void DropperAudioProcessor::processSlice(juce::AudioBuffer<float>& buffer, int start, int count,
                                         dropper::Parameters params, dropper::Transport& transport) {
    if (count <= 0)
        return;

    transport.bpm = dropper::sanitizeBpm(transport.bpm);

    float* channels[2] = { nullptr, nullptr };
    const int nCh = std::min(buffer.getNumChannels(), 2);
    for (int ch = 0; ch < nCh; ++ch)
        channels[ch] = buffer.getWritePointer(ch, start);

    engine.process(channels, nCh, count, params, transport);

    if (transport.hasHostPosition && transport.isPlaying) {
        const double beats = (transport.bpm / 60.0) * static_cast<double>(count) / transport.sampleRate;
        transport.ppqPosition += beats;
    }
}

void DropperAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, n);

    const auto params = readParameters();
    auto transport = readTransport();
    const bool noteRetrigger = raw(apvts, "midiRetrigger") >= 0.5f;
    const bool resetHigh = raw(apvts, "degradeReset") >= 0.5f;
    if (resetHigh && !degradeResetHigh)
        engine.requestDegradeReset();
    degradeResetHigh = resetHigh;

    const int latency = engine.stabilityLatencySamples(params.stability.amount);
    if (latency != getLatencySamples())
        setLatencySamples(latency);

    int cursor = 0;
    for (const auto meta : midi) {
        const auto msg = meta.getMessage();
        const int at = juce::jlimit(0, n, meta.samplePosition);

        bool trigger = false;
        if (msg.isController() && msg.getControllerNumber() == dropper::kRegenerateController) {
            const bool high = msg.getControllerValue() >= 64;
            trigger = high && !regenerateCcHigh;
            regenerateCcHigh = high;
        }
        if (noteRetrigger && msg.isNoteOn())
            trigger = true;

        if (!trigger)
            continue;

        processSlice(buffer, cursor, at - cursor, params, transport);
        engine.regenerate();
        cursor = at;
    }

    processSlice(buffer, cursor, n - cursor, params, transport);
}

void DropperAudioProcessor::requestRegenerate() {
    engine.requestRegenerate();
}

void DropperAudioProcessor::requestDegradeReset() {
    engine.requestDegradeReset();
}

void DropperAudioProcessor::requestGenerationReset() {
    engine.requestGenerationReset();
}

juce::AudioProcessorEditor* DropperAudioProcessor::createEditor() {
    return new DropperAudioProcessorEditor(*this);
}

void DropperAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    const auto seed = engine.getSeed();
    state.setProperty("seedLo", static_cast<juce::int64>(seed & 0xffffffffu), nullptr);
    state.setProperty("seedHi", static_cast<juce::int64>(seed >> 32), nullptr);
    const auto wear = engine.degradationState();
    state.setProperty("degElapsed", wear.elapsed, nullptr);
    state.setProperty("degLevel", static_cast<double>(wear.level), nullptr);

    const auto stability = engine.stabilityState();
    state.setProperty("stabWow", static_cast<double>(stability.wowPhase), nullptr);
    state.setProperty("stabFlutter", static_cast<double>(stability.flutterPhase), nullptr);
    state.setProperty("stabDrift", static_cast<double>(stability.drift), nullptr);
    state.setProperty("stabDriftTarget", static_cast<double>(stability.driftTarget), nullptr);
    state.setProperty("stabAmp", static_cast<double>(stability.ampPhase), nullptr);
    state.setProperty("stabAmount", static_cast<double>(stability.amountSmoothed), nullptr);
    state.setProperty("stabEngage", static_cast<double>(stability.engage), nullptr);
    state.setProperty("stabNoise", static_cast<juce::int64>(stability.noiseState), nullptr);
    state.setProperty("stabCount", static_cast<juce::int64>(stability.sampleCount), nullptr);
    state.setProperty("stabGenPos", stability.genPos, nullptr);
    state.setProperty("stabGenCount", stability.genCount, nullptr);
    state.setProperty("stabGenLength", stability.genLength, nullptr);
    state.setProperty("stabModWrite", stability.modWrite, nullptr);
    state.setProperty("stabRate", stability.sampleRate, nullptr);
    state.setProperty("stabWanderRate", static_cast<double>(stability.wanderRate), nullptr);
    state.setProperty("stabWanderTarget", static_cast<double>(stability.wanderRateTarget), nullptr);
    state.setProperty("stabWander2", static_cast<double>(stability.wanderPhase2), nullptr);
    state.setProperty("stabLoss", static_cast<double>(stability.lossWander), nullptr);
    state.setProperty("stabLossTarget", static_cast<double>(stability.lossWanderTarget), nullptr);
    state.setProperty("stabHiss", static_cast<double>(stability.hissBreath), nullptr);
    state.setProperty("stabHissTarget", static_cast<double>(stability.hissBreathTarget), nullptr);
    state.setProperty("stabWanderHold", stability.wanderHold, nullptr);
    state.setProperty("stabLossHold", stability.lossHold, nullptr);
    state.setProperty("stabHissHold", stability.hissHold, nullptr);
    state.setProperty("stabCrackWait", stability.crackleWait, nullptr);
    if (!stability.loop.empty()) {
        state.setProperty("stabLoop",
                          juce::Base64::toBase64(stability.loop.data(), stability.loop.size() * sizeof(float)),
                          nullptr);
        state.setProperty("stabClean",
                          juce::Base64::toBase64(stability.clean.data(), stability.clean.size() * sizeof(float)),
                          nullptr);
    }
    if (!stability.modDelay.empty()) {
        state.setProperty("stabMod",
                          juce::Base64::toBase64(stability.modDelay.data(), stability.modDelay.size() * sizeof(float)),
                          nullptr);
    }

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void DropperAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid()) {
            const auto seedLo = static_cast<std::uint64_t>(static_cast<juce::int64>(state.getProperty("seedLo", 0)));
            const auto seedHi = static_cast<std::uint64_t>(static_cast<juce::int64>(state.getProperty("seedHi", 0)));
            state.removeProperty("seedLo", nullptr);
            state.removeProperty("seedHi", nullptr);
            const bool hasDegrade = state.hasProperty("degElapsed");
            const double degElapsed = hasDegrade ? static_cast<double>(state.getProperty("degElapsed")) : 0.0;
            const float degLevel = hasDegrade ? static_cast<float>(static_cast<double>(state.getProperty("degLevel"))) : 0.f;
            state.removeProperty("degElapsed", nullptr);
            state.removeProperty("degLevel", nullptr);
            const bool hasStability = state.hasProperty("stabWow");
            dropper::StabilityState stability;
            if (hasStability) {
                const auto property = [&state](const char* name, double fallback) {
                    return static_cast<double>(state.getProperty(name, fallback));
                };
                stability.wowPhase = static_cast<float>(property("stabWow", 0.0));
                stability.flutterPhase = static_cast<float>(property("stabFlutter", 0.0));
                stability.drift = static_cast<float>(property("stabDrift", 0.0));
                stability.driftTarget = static_cast<float>(property("stabDriftTarget", 0.0));
                stability.ampPhase = static_cast<float>(property("stabAmp", 0.0));
                stability.amountSmoothed = static_cast<float>(property("stabAmount", 0.0));
                stability.engage = static_cast<float>(property("stabEngage", 0.0));
                stability.noiseState = static_cast<std::uint64_t>(static_cast<juce::int64>(state.getProperty("stabNoise", 1)));
                stability.sampleCount = static_cast<std::uint64_t>(static_cast<juce::int64>(state.getProperty("stabCount", 0)));
                stability.genPos = static_cast<int>(state.getProperty("stabGenPos", 0));
                stability.genCount = static_cast<int>(state.getProperty("stabGenCount", 0));
                stability.genLength = static_cast<int>(state.getProperty("stabGenLength", 0));
                stability.sampleRate = property("stabRate", 0.0);
                stability.wanderRate = static_cast<float>(property("stabWanderRate", 0.4));
                stability.wanderRateTarget = static_cast<float>(property("stabWanderTarget", 0.4));
                stability.wanderPhase2 = static_cast<float>(property("stabWander2", 0.0));
                stability.lossWander = static_cast<float>(property("stabLoss", 0.0));
                stability.lossWanderTarget = static_cast<float>(property("stabLossTarget", 0.0));
                stability.hissBreath = static_cast<float>(property("stabHiss", 0.8));
                stability.hissBreathTarget = static_cast<float>(property("stabHissTarget", 0.8));
                stability.wanderHold = static_cast<int>(state.getProperty("stabWanderHold", 0));
                stability.lossHold = static_cast<int>(state.getProperty("stabLossHold", 0));
                stability.hissHold = static_cast<int>(state.getProperty("stabHissHold", 0));
                stability.crackleWait = static_cast<int>(state.getProperty("stabCrackWait", 1));
                auto decode = [](const juce::String& text) {
                    juce::MemoryOutputStream decoded;
                    std::vector<float> samples;
                    if (text.isEmpty() || !juce::Base64::convertFromBase64(decoded, text))
                        return samples;
                    const auto bytes = static_cast<int>(decoded.getDataSize());
                    if (bytes <= 0 || (bytes % static_cast<int>(sizeof(float))) != 0)
                        return samples;
                    const auto count = static_cast<size_t>(bytes / static_cast<int>(sizeof(float)));
                    samples.resize(count);
                    std::memcpy(samples.data(), decoded.getData(), static_cast<size_t>(bytes));
                    return samples;
                };
                stability.loop = decode(state.getProperty("stabLoop").toString());
                stability.clean = decode(state.getProperty("stabClean").toString());
                stability.modDelay = decode(state.getProperty("stabMod").toString());
                stability.modWrite = static_cast<int>(state.getProperty("stabModWrite", 0));
                for (const char* name : { "stabWow", "stabFlutter", "stabDrift", "stabDriftTarget", "stabAmp",
                                          "stabAmount", "stabEngage", "stabNoise", "stabCount", "stabGenPos",
                                          "stabGenCount", "stabGenLength", "stabRate", "stabWanderRate",
                                          "stabWanderTarget", "stabWander2", "stabLoss", "stabLossTarget",
                                          "stabHiss", "stabHissTarget", "stabWanderHold", "stabLossHold",
                                          "stabHissHold", "stabCrackWait", "stabLoop", "stabClean",
                                          "stabMod", "stabModWrite" })
                    state.removeProperty(name, nullptr);
            }
            apvts.replaceState(state);
            if (seedLo != 0 || seedHi != 0)
                engine.setSeed((seedHi << 32) | (seedLo & 0xffffffffu));
            if (hasDegrade) {
                dropper::DegradationState restored;
                restored.elapsed = degElapsed;
                restored.level = degLevel;
                engine.restoreDegradationState(restored);
            }
            if (hasStability)
                engine.restoreStabilityState(stability);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new DropperAudioProcessor();
}
