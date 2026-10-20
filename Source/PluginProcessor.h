#pragma once

#include "DSP/DropperEngine.h"

#include <JuceHeader.h>

class DropperAudioProcessor : public juce::AudioProcessor {
public:
    DropperAudioProcessor();
    ~DropperAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    dropper::Engine& getEngine() noexcept { return engine; }
    const dropper::Engine& getEngine() const noexcept { return engine; }

    void requestRegenerate();
    void requestDegradeReset();
    void requestGenerationReset();
    dropper::Parameters readParameters() const;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    dropper::Transport readTransport() const;
    void processSlice(juce::AudioBuffer<float>& buffer, int start, int count,
                      dropper::Parameters params, dropper::Transport& transport);

    juce::AudioProcessorValueTreeState apvts;
    dropper::Engine engine;
    bool regenerateCcHigh = false;
    bool degradeResetHigh = false;
    bool generationResetHigh = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DropperAudioProcessor)
};
