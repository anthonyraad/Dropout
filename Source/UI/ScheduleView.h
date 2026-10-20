#pragma once

#include "PluginProcessor.h"

#include <JuceHeader.h>

class ScheduleView : public juce::Component {
public:
    explicit ScheduleView(DropperAudioProcessor& processor);

    void paint(juce::Graphics& g) override;

private:
    dropper::ScheduleContext liveContext() const;

    DropperAudioProcessor& processor;
};
