#pragma once

#include "PluginProcessor.h"
#include "UI/DropperLookAndFeel.h"
#include "UI/ScheduleView.h"

#include <JuceHeader.h>

class DropperAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer {
public:
    explicit DropperAudioProcessorEditor(DropperAudioProcessor&);
    ~DropperAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void configureKnob(juce::Slider& slider, juce::Label& name, const juce::String& title);
    void syncModeButtons();
    void setAdvancedOpen(bool open);
    void setDegradeOpen(bool open);
    void setStabilityOpen(bool open);
    void updatePanelSize();

    DropperAudioProcessor& processor;
    DropperLookAndFeel lookAndFeel;

    juce::Slider dropper;
    juce::Slider density;
    juce::Slider fade;
    juce::Slider minDuration;
    juce::Slider maxDuration;
    juce::Slider randomness;
    juce::Slider patternLength;
    juce::Slider mix;
    juce::Slider degradeAmount;
    juce::Slider degradeTime;
    juce::Slider degradeRecovery;
    juce::Slider degradeManual;
    juce::Slider stability;
    juce::Slider wowDepth;
    juce::Slider noiseLevel;
    juce::Slider freqLoss;

    juce::Label dropperName;
    juce::Label dropperValue;
    juce::Label densityName;
    juce::Label fadeName;
    juce::Label minName;
    juce::Label maxName;
    juce::Label randomName;
    juce::Label patternName;
    juce::Label mixName;
    juce::Label degradeAmountName;
    juce::Label degradeTimeName;
    juce::Label degradeRecoveryName;
    juce::Label degradeManualName;
    juce::Label stabilityName;
    juce::Label wowName;
    juce::Label noiseName;
    juce::Label freqName;

    juce::TextButton randomButton { "VARIABLE" };
    juce::TextButton repeatButton { "FIXED" };
    juce::TextButton regenerateButton { "REGENERATE" };
    juce::TextButton syncButton { "SYNC" };
    juce::TextButton advancedButton { "DESPOND" };
    juce::TextButton midiButton { "MIDI RETRIGGER" };
    juce::TextButton degradeButton { "DESTROY" };
    juce::TextButton stabilityButton { "DESTABILIZE" };
    juce::TextButton degradeOnButton { "DESTROY" };
    juce::TextButton degradeHoldButton { "HOLD" };
    juce::TextButton degradeResetButton { "RESET" };
    juce::TextButton degradeManualButton { "MANUAL" };

    juce::ComboBox subdivision;
    juce::ComboBox degradeCurve;
    ScheduleView schedule;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> dropperAttachment;
    std::unique_ptr<SliderAttachment> densityAttachment;
    std::unique_ptr<SliderAttachment> fadeAttachment;
    std::unique_ptr<SliderAttachment> minAttachment;
    std::unique_ptr<SliderAttachment> maxAttachment;
    std::unique_ptr<SliderAttachment> randomAttachment;
    std::unique_ptr<SliderAttachment> patternAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> degradeAmountAttachment;
    std::unique_ptr<SliderAttachment> degradeTimeAttachment;
    std::unique_ptr<SliderAttachment> degradeRecoveryAttachment;
    std::unique_ptr<SliderAttachment> degradeManualAttachment;
    std::unique_ptr<SliderAttachment> stabilityAttachment;
    std::unique_ptr<SliderAttachment> wowAttachment;
    std::unique_ptr<SliderAttachment> noiseAttachment;
    std::unique_ptr<SliderAttachment> freqAttachment;
    std::unique_ptr<ComboAttachment> subdivisionAttachment;
    std::unique_ptr<ComboAttachment> degradeCurveAttachment;
    std::unique_ptr<ButtonAttachment> degradeOnAttachment;
    std::unique_ptr<ButtonAttachment> degradeHoldAttachment;
    std::unique_ptr<ButtonAttachment> degradeManualOnAttachment;

    bool advancedOpen = false;
    bool degradeOpen = false;
    bool stabilityOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DropperAudioProcessorEditor)
};
