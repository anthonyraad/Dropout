#include "PluginEditor.h"

#include "BinaryData.h"

namespace {
juce::Font titleFont() {
    static const auto typeface = juce::Typeface::createSystemTypefaceFor(
        BinaryData::Inter_Bold_ttf, static_cast<size_t>(BinaryData::Inter_Bold_ttfSize));
    if (typeface == nullptr)
        return DropperLookAndFeel::uiFont(22.f, true);
    return juce::Font(juce::FontOptions(typeface).withHeight(22.f));
}
} // namespace

namespace {
constexpr int kWidth = 760;
constexpr int kBaseHeight = 500;
constexpr int kAdvancedExtra = 140;
constexpr int kDegradeExtra = 156;
constexpr int kStabilityExtra = 116;

juce::String percentText(double value) {
    return juce::String(static_cast<int>(std::lround(value))) + "%";
}
} // namespace

DropperAudioProcessorEditor::DropperAudioProcessorEditor(DropperAudioProcessor& processorIn)
    : AudioProcessorEditor(&processorIn),
      processor(processorIn),
      schedule(processorIn) {
    setLookAndFeel(&lookAndFeel);
    setSize(kWidth, kBaseHeight);
    setResizable(false, false);

    configureKnob(dropper, dropperName, "DROPS");
    configureKnob(density, densityName, "DENSITY");
    configureKnob(fade, fadeName, "FADE");
    configureKnob(minDuration, minName, "MIN");
    configureKnob(maxDuration, maxName, "MAX");
    configureKnob(randomness, randomName, "DRIFT");
    configureKnob(patternLength, patternName, "BARS");
    configureKnob(mix, mixName, "MIX");
    configureKnob(degradeAmount, degradeAmountName, "THE END");
    degradeAmount.setComponentID("the-end");
    degradeAmountName.setMinimumHorizontalScale(0.7f);
    configureKnob(degradeTime, degradeTimeName, "TIME");
    configureKnob(degradeRecovery, degradeRecoveryName, "RECOVER");
    configureKnob(degradeManual, degradeManualName, "MANUAL");
    configureKnob(stability, stabilityName, "AMOUNT");
    configureKnob(wowDepth, wowName, "WANDER");
    configureKnob(noiseLevel, noiseName, "NOISE");
    configureKnob(freqLoss, freqName, "LOSS");

    dropperValue.setJustificationType(juce::Justification::centred);
    dropperValue.setFont(DropperLookAndFeel::uiFont(16.f));
    dropperValue.setColour(juce::Label::textColourId, DropperLookAndFeel::text());
    addAndMakeVisible(dropperValue);

    auto& apvts = processor.getAPVTS();
    dropperAttachment = std::make_unique<SliderAttachment>(apvts, "dropper", dropper);
    densityAttachment = std::make_unique<SliderAttachment>(apvts, "density", density);
    fadeAttachment = std::make_unique<SliderAttachment>(apvts, "fade", fade);
    minAttachment = std::make_unique<SliderAttachment>(apvts, "minDuration", minDuration);
    maxAttachment = std::make_unique<SliderAttachment>(apvts, "maxDuration", maxDuration);
    randomAttachment = std::make_unique<SliderAttachment>(apvts, "randomness", randomness);
    patternAttachment = std::make_unique<SliderAttachment>(apvts, "patternLength", patternLength);
    mixAttachment = std::make_unique<SliderAttachment>(apvts, "mix", mix);
    degradeAmountAttachment = std::make_unique<SliderAttachment>(apvts, "degradeAmount", degradeAmount);
    degradeTimeAttachment = std::make_unique<SliderAttachment>(apvts, "degradeTime", degradeTime);
    degradeRecoveryAttachment = std::make_unique<SliderAttachment>(apvts, "degradeRecovery", degradeRecovery);
    degradeManualAttachment = std::make_unique<SliderAttachment>(apvts, "degradeManual", degradeManual);
    stabilityAttachment = std::make_unique<SliderAttachment>(apvts, "stability", stability);
    wowAttachment = std::make_unique<SliderAttachment>(apvts, "wowDepth", wowDepth);
    noiseAttachment = std::make_unique<SliderAttachment>(apvts, "noise", noiseLevel);
    freqAttachment = std::make_unique<SliderAttachment>(apvts, "freqLoss", freqLoss);

    auto bindDefault = [&apvts](juce::Slider& slider, const char* id) {
        if (auto* parameter = apvts.getParameter(id))
            slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
    };
    bindDefault(dropper, "dropper");
    bindDefault(density, "density");
    bindDefault(fade, "fade");
    bindDefault(minDuration, "minDuration");
    bindDefault(maxDuration, "maxDuration");
    bindDefault(randomness, "randomness");
    bindDefault(patternLength, "patternLength");
    bindDefault(mix, "mix");
    bindDefault(degradeAmount, "degradeAmount");
    bindDefault(degradeTime, "degradeTime");
    bindDefault(degradeRecovery, "degradeRecovery");
    bindDefault(degradeManual, "degradeManual");
    bindDefault(stability, "stability");
    bindDefault(wowDepth, "wowDepth");
    bindDefault(noiseLevel, "noise");
    bindDefault(freqLoss, "freqLoss");

    randomButton.setComponentID("random");
    repeatButton.setComponentID("repeat");
    syncButton.setComponentID("sync");
    midiButton.setComponentID("midi");
    randomButton.setClickingTogglesState(false);
    repeatButton.setClickingTogglesState(false);
    syncButton.setClickingTogglesState(false);
    midiButton.setClickingTogglesState(false);

    randomButton.onClick = [this] {
        if (auto* p = processor.getAPVTS().getParameter("mode"))
            p->setValueNotifyingHost(0.f);
    };
    repeatButton.onClick = [this] {
        if (auto* p = processor.getAPVTS().getParameter("mode"))
            p->setValueNotifyingHost(1.f);
    };
    syncButton.onClick = [this] {
        if (auto* p = processor.getAPVTS().getParameter("sync")) {
            const float next = p->getValue() >= 0.5f ? 0.f : 1.f;
            p->setValueNotifyingHost(next);
        }
    };
    midiButton.onClick = [this] {
        if (auto* p = processor.getAPVTS().getParameter("midiRetrigger")) {
            const float next = p->getValue() >= 0.5f ? 0.f : 1.f;
            p->setValueNotifyingHost(next);
        }
    };
    regenerateButton.onClick = [this] { processor.requestRegenerate(); };
    advancedButton.onClick = [this] { setAdvancedOpen(!advancedOpen); };
    degradeButton.onClick = [this] { setDegradeOpen(!degradeOpen); };
    stabilityButton.onClick = [this] { setStabilityOpen(!stabilityOpen); };
    degradeResetButton.onClick = [this] { processor.requestDegradeReset(); };

    degradeOnButton.setClickingTogglesState(true);
    degradeHoldButton.setClickingTogglesState(true);
    degradeManualButton.setClickingTogglesState(true);

    for (auto* button : { &randomButton, &repeatButton, &regenerateButton, &syncButton, &advancedButton, &midiButton,
                          &degradeButton, &stabilityButton, &degradeOnButton, &degradeHoldButton, &degradeResetButton,
                          &degradeManualButton })
        addAndMakeVisible(button);

    subdivision.addItemList({ "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
                              "1/4T", "1/8T", "1/16T", "1/8D", "1/16D" },
                            1);
    addAndMakeVisible(subdivision);
    subdivisionAttachment = std::make_unique<ComboAttachment>(apvts, "subdivision", subdivision);

    degradeCurve.addItemList({ "Slow Burn", "Steady Decay", "Rapid Collapse", "Unstable" }, 1);
    addAndMakeVisible(degradeCurve);
    degradeCurveAttachment = std::make_unique<ComboAttachment>(apvts, "degradeCurve", degradeCurve);
    degradeOnAttachment = std::make_unique<ButtonAttachment>(apvts, "degrade", degradeOnButton);
    degradeHoldAttachment = std::make_unique<ButtonAttachment>(apvts, "degradeHold", degradeHoldButton);
    degradeManualOnAttachment = std::make_unique<ButtonAttachment>(apvts, "degradeManualOn", degradeManualButton);

    addAndMakeVisible(schedule);
    setAdvancedOpen(false);
    setDegradeOpen(false);
    setStabilityOpen(false);
    startTimerHz(30);
}

DropperAudioProcessorEditor::~DropperAudioProcessorEditor() {
    setLookAndFeel(nullptr);
}

void DropperAudioProcessorEditor::configureKnob(juce::Slider& slider, juce::Label& name, const juce::String& title) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(slider);

    name.setText(title, juce::dontSendNotification);
    name.setJustificationType(juce::Justification::centred);
    name.setFont(DropperLookAndFeel::uiFont(11.f));
    name.setColour(juce::Label::textColourId, DropperLookAndFeel::muted());
    addAndMakeVisible(name);
}

void DropperAudioProcessorEditor::setAdvancedOpen(bool open) {
    advancedOpen = open;
    advancedButton.setButtonText(open ? "DESPOND  -" : "DESPOND  +");
    for (auto* c : { static_cast<juce::Component*>(&minDuration), static_cast<juce::Component*>(&maxDuration),
                     static_cast<juce::Component*>(&randomness), static_cast<juce::Component*>(&patternLength),
                     static_cast<juce::Component*>(&mix), static_cast<juce::Component*>(&midiButton),
                     static_cast<juce::Component*>(&minName), static_cast<juce::Component*>(&maxName),
                     static_cast<juce::Component*>(&randomName), static_cast<juce::Component*>(&patternName),
                     static_cast<juce::Component*>(&mixName) })
        c->setVisible(open);
    updatePanelSize();
}

void DropperAudioProcessorEditor::setDegradeOpen(bool open) {
    degradeOpen = open;
    degradeButton.setButtonText(open ? "DESTROY  -" : "DESTROY  +");
    if (open)
        if (auto* param = processor.getAPVTS().getParameter("degrade"))
            if (param->getValue() < 0.5f)
                param->setValueNotifyingHost(1.f);
    for (auto* c : { static_cast<juce::Component*>(&degradeAmount), static_cast<juce::Component*>(&degradeTime),
                     static_cast<juce::Component*>(&degradeRecovery), static_cast<juce::Component*>(&degradeManual),
                     static_cast<juce::Component*>(&degradeAmountName), static_cast<juce::Component*>(&degradeTimeName),
                     static_cast<juce::Component*>(&degradeRecoveryName), static_cast<juce::Component*>(&degradeManualName),
                     static_cast<juce::Component*>(&degradeOnButton), static_cast<juce::Component*>(&degradeHoldButton),
                     static_cast<juce::Component*>(&degradeResetButton),
                     static_cast<juce::Component*>(&degradeManualButton),
                     static_cast<juce::Component*>(&degradeCurve) })
        c->setVisible(open);
    updatePanelSize();
}

void DropperAudioProcessorEditor::setStabilityOpen(bool open) {
    stabilityOpen = open;
    stabilityButton.setButtonText(open ? "DESTABILIZE  -" : "DESTABILIZE  +");
    for (auto* c : { static_cast<juce::Component*>(&stability), static_cast<juce::Component*>(&wowDepth),
                     static_cast<juce::Component*>(&noiseLevel), static_cast<juce::Component*>(&freqLoss),
                     static_cast<juce::Component*>(&stabilityName), static_cast<juce::Component*>(&wowName),
                     static_cast<juce::Component*>(&noiseName), static_cast<juce::Component*>(&freqName) })
        c->setVisible(open);
    updatePanelSize();
}

void DropperAudioProcessorEditor::updatePanelSize() {
    int height = kBaseHeight;
    if (advancedOpen)
        height += kAdvancedExtra;
    if (degradeOpen)
        height += kDegradeExtra;
    if (stabilityOpen)
        height += kStabilityExtra;
    setSize(kWidth, height);
    resized();
}

void DropperAudioProcessorEditor::syncModeButtons() {
    auto& apvts = processor.getAPVTS();
    const bool repeatable = apvts.getRawParameterValue("mode")->load() >= 0.5f;
    const bool sync = apvts.getRawParameterValue("sync")->load() >= 0.5f;
    const bool midi = apvts.getRawParameterValue("midiRetrigger")->load() >= 0.5f;
    randomButton.setToggleState(!repeatable, juce::dontSendNotification);
    repeatButton.setToggleState(repeatable, juce::dontSendNotification);
    syncButton.setToggleState(sync, juce::dontSendNotification);
    midiButton.setToggleState(midi, juce::dontSendNotification);
    subdivision.setEnabled(sync);
    patternLength.setEnabled(repeatable);
    patternName.setEnabled(repeatable);
    const bool manual = apvts.getRawParameterValue("degradeManualOn")->load() >= 0.5f;
    degradeManual.setEnabled(manual);
    degradeManualName.setEnabled(manual);
    const bool amountOn = apvts.getRawParameterValue("stability")->load() > 0.08f;
    wowDepth.setEnabled(amountOn);
    noiseLevel.setEnabled(amountOn);
    freqLoss.setEnabled(amountOn);
    wowName.setEnabled(amountOn);
    noiseName.setEnabled(amountOn);
    freqName.setEnabled(amountOn);

    dropperValue.setText(percentText(dropper.getValue()), juce::dontSendNotification);
    dropperValue.setColour(juce::Label::textColourId, DropperLookAndFeel::text());

    juce::String destroyLabel = "DESTROY";
    float destroyProgress = 0.f;
    if (apvts.getRawParameterValue("degrade")->load() >= 0.5f) {
        const auto snap = processor.getEngine().snapshot();
        float ceiling = apvts.getRawParameterValue("degradeAmount")->load() / 100.f;
        float level = 0.f;
        if (apvts.getRawParameterValue("degradeManualOn")->load() >= 0.5f)
            level = apvts.getRawParameterValue("degradeManual")->load() / 100.f * ceiling;
        if (snap.valid && snap.degradationEnabled) {
            level = snap.degradation;
            ceiling = snap.degradationCeiling;
        }
        const bool atEnd = ceiling <= 0.001f || level >= ceiling - 0.005f;
        destroyProgress = juce::jlimit(0.f, 1.f, level / std::max(0.001f, ceiling));
        destroyLabel = atEnd ? "DESTROYED" : "DESTROYING";
    }
    degradeAmount.getProperties().set("destroyProgress", destroyProgress);
    degradeAmount.repaint();
    if (degradeOnButton.getButtonText() != destroyLabel)
        degradeOnButton.setButtonText(destroyLabel);
}

void DropperAudioProcessorEditor::timerCallback() {
    syncModeButtons();
    schedule.repaint();
    repaint();
}

void DropperAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(DropperLookAndFeel::background());

    auto card = getLocalBounds().toFloat().reduced(8.f);
    g.setColour(DropperLookAndFeel::panel());
    g.fillRoundedRectangle(card, 2.f);
    g.setColour(DropperLookAndFeel::line());
    g.drawRoundedRectangle(card.reduced(0.5f), 2.f, 1.f);

    g.setColour(DropperLookAndFeel::text());
    g.setFont(titleFont());
    g.drawText("dropout", 24, 16, 220, 24, juce::Justification::centredLeft);
}

void DropperAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(36);

    auto header = area.removeFromTop(30);
    repeatButton.setBounds(header.removeFromRight(118).reduced(0, 2));
    header.removeFromRight(6);
    randomButton.setBounds(header.removeFromRight(110).reduced(0, 2));

    area.removeFromTop(6);
    auto body = area.removeFromTop(206);
    auto left = body.removeFromLeft(230);
    dropper.setBounds(left.removeFromTop(156).reduced(8, 0));
    dropperValue.setBounds(left.removeFromTop(24));
    dropperName.setBounds(left.removeFromTop(18));

    body.removeFromLeft(8);
    schedule.setBounds(body);

    area.removeFromTop(8);
    auto row = area.removeFromTop(100);
    auto placeKnob = [](juce::Rectangle<int>& host, juce::Slider& slider, juce::Label& name, int width) {
        auto slot = host.removeFromLeft(width);
        slider.setBounds(slot.removeFromTop(74).reduced(6, 0));
        name.setBounds(slot.removeFromTop(16));
    };
    placeKnob(row, density, densityName, 110);
    placeKnob(row, fade, fadeName, 110);

    auto buttons = row.reduced(8, 18);
    regenerateButton.setBounds(buttons.removeFromTop(32));
    buttons.removeFromTop(8);
    auto syncRow = buttons.removeFromTop(30);
    syncButton.setBounds(syncRow.removeFromLeft(72));
    syncRow.removeFromLeft(8);
    subdivision.setBounds(syncRow.removeFromLeft(110));

    area.removeFromTop(8);
    auto toggles = area.removeFromTop(28);
    advancedButton.setBounds(toggles.removeFromLeft(130));
    toggles.removeFromLeft(8);
    stabilityButton.setBounds(toggles.removeFromLeft(168));
    toggles.removeFromLeft(8);
    degradeButton.setBounds(toggles.removeFromLeft(130));

    // Despond keeps its knob widths. Equal gaps are center-to-center, because
    // every rotary is the same diameter. Destroy and Destabilize share those centers.
    constexpr int knobWidths[] = { 100, 100, 120, 90, 90 };
    constexpr int midiReserve = 148;
    constexpr int midiGap = 12;
    const int knobSpan = area.getWidth() - midiReserve - midiGap;
    const int firstCenter = knobWidths[0] / 2;
    const int lastCenter = knobSpan - knobWidths[4] / 2;
    const int step = juce::jmax(1, (lastCenter - firstCenter) / 4);
    int centers[5];
    for (int i = 0; i < 5; ++i)
        centers[i] = firstCenter + i * step;
    auto placeKnobAt = [](juce::Rectangle<int> row, int centerX, int width,
                           juce::Slider& slider, juce::Label& name) {
        auto slot = juce::Rectangle<int>(row.getX() + centerX - width / 2, row.getY(), width, row.getHeight());
        slider.setBounds(slot.removeFromTop(74).reduced(6, 0));
        name.setBounds(slot.removeFromTop(16));
    };

    if (advancedOpen) {
        area.removeFromTop(8);
        auto adv = area.removeFromTop(108);
        placeKnobAt(adv, centers[0], knobWidths[0], minDuration, minName);
        placeKnobAt(adv, centers[1], knobWidths[1], maxDuration, maxName);
        placeKnobAt(adv, centers[2], knobWidths[2], randomness, randomName);
        placeKnobAt(adv, centers[3], knobWidths[3], patternLength, patternName);
        placeKnobAt(adv, centers[4], knobWidths[4], mix, mixName);
        midiButton.setBounds(adv.getX() + centers[4] + knobWidths[4] / 2 + midiGap, adv.getY() + 24,
                             midiReserve, 32);
    }

    if (stabilityOpen) {
        area.removeFromTop(8);
        auto stabilityRow = area.removeFromTop(100);
        placeKnobAt(stabilityRow, centers[0], knobWidths[0], stability, stabilityName);
        placeKnobAt(stabilityRow, centers[1], knobWidths[1], wowDepth, wowName);
        placeKnobAt(stabilityRow, centers[2], knobWidths[2], noiseLevel, noiseName);
        placeKnobAt(stabilityRow, centers[3], knobWidths[3], freqLoss, freqName);
    }

    if (degradeOpen) {
        area.removeFromTop(8);
        auto degradeKnobs = area.removeFromTop(100);
        placeKnobAt(degradeKnobs, centers[0], knobWidths[0], degradeAmount, degradeAmountName);
        placeKnobAt(degradeKnobs, centers[1], knobWidths[1], degradeTime, degradeTimeName);
        placeKnobAt(degradeKnobs, centers[2], knobWidths[2], degradeRecovery, degradeRecoveryName);
        placeKnobAt(degradeKnobs, centers[3], knobWidths[3], degradeManual, degradeManualName);

        area.removeFromTop(6);
        auto degradeRow = area.removeFromTop(30);
        degradeOnButton.setBounds(degradeRow.removeFromLeft(132).reduced(0, 1));
        degradeRow.removeFromLeft(6);
        degradeHoldButton.setBounds(degradeRow.removeFromLeft(64).reduced(0, 1));
        degradeRow.removeFromLeft(6);
        degradeResetButton.setBounds(degradeRow.removeFromLeft(68).reduced(0, 1));
        degradeRow.removeFromLeft(6);
        degradeManualButton.setBounds(degradeRow.removeFromLeft(78).reduced(0, 1));
        degradeRow.removeFromLeft(8);
        degradeCurve.setBounds(degradeRow.reduced(0, 1));
    }
}
