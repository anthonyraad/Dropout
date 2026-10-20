#include "UI/ScheduleView.h"

#include "UI/DropperLookAndFeel.h"

ScheduleView::ScheduleView(DropperAudioProcessor& processorIn)
    : processor(processorIn) {}

dropper::ScheduleContext ScheduleView::liveContext() const {
    const auto snap = processor.getEngine().snapshot();
    auto params = processor.readParameters();

    dropper::Transport transport;
    transport.sampleRate = snap.sampleRate > 1000.0 ? snap.sampleRate : processor.getSampleRate();
    if (!(transport.sampleRate > 1000.0))
        transport.sampleRate = 48000.0;
    transport.bpm = snap.valid ? snap.bpm : 120.0;
    transport.timeSigNumerator = snap.valid ? snap.timeSigNumerator : 4;
    transport.timeSigDenominator = snap.valid ? snap.timeSigDenominator : 4;
    transport.hasHostPosition = params.timingSync != 0;
    transport.isPlaying = true;

    auto ctx = dropper::contextFor(params, transport, processor.getEngine().getSeed());
    if (snap.valid && params.degrade.enabled)
        ctx.degradeLevel = snap.degradation;
    return ctx;
}

void ScheduleView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(DropperLookAndFeel::surface());
    g.fillRoundedRectangle(bounds, 2.f);
    g.setColour(DropperLookAndFeel::line());
    g.drawRoundedRectangle(bounds.reduced(0.5f), 2.f, 1.f);

    const auto ctx = liveContext();
    const auto geo = dropper::geometryFor(ctx);
    const auto snap = processor.getEngine().snapshot();

    auto plot = bounds.reduced(10.f, 18.f);
    plot.removeFromTop(6.f);
    const bool showDegrade = ctx.params.degrade.enabled;
    float shownLevel = ctx.params.degrade.initial * ctx.params.degrade.ceiling;
    float shownCeiling = ctx.params.degrade.ceiling;
    double shownElapsed = 0.0;
    double shownRemaining = ctx.params.degrade.timeSec;
    bool shownHeld = ctx.params.degrade.hold;
    if (ctx.params.degrade.manual)
        shownLevel = ctx.params.degrade.manualLevel * shownCeiling;
    if (snap.valid && snap.degradationEnabled) {
        shownLevel = snap.degradation;
        shownCeiling = snap.degradationCeiling;
        shownElapsed = snap.degradationElapsed;
        shownRemaining = snap.degradationRemaining;
        shownHeld = snap.degradationHeld;
    }
    if (showDegrade)
        plot.removeFromBottom(18.f);

    const double playhead = snap.valid ? snap.playhead : 0.0;
    double windowStart = 0.0;
    double window = 2.0;
    if (geo.repeatable && geo.patternLength > 0.0) {
        window = geo.patternLength;
        windowStart = geo.unit == dropper::TimeUnit::Beats
                          ? std::floor(playhead / window) * window
                          : std::floor(playhead / window) * window;
    } else if (geo.unit == dropper::TimeUnit::Beats) {
        window = std::max(1.0, (snap.valid ? snap.bpm : 120.0) / 60.0 * 2.0);
        windowStart = playhead;
    } else {
        window = 2.0;
        windowStart = playhead;
    }

    const int columns = std::max(2, static_cast<int>(plot.getWidth()));
    juce::Path envelope;
    juce::Path present;
    int drops = 0;

    for (int i = 0; i < columns; ++i) {
        const double t = windowStart + window * (static_cast<double>(i) / static_cast<double>(columns - 1));
        const float gain = dropper::scheduleGain(ctx, t);
        if (gain < 0.5f)
            ++drops;
        const float x = plot.getX() + plot.getWidth() * static_cast<float>(i) / static_cast<float>(columns - 1);
        const float y = plot.getBottom() - plot.getHeight() * gain;
        if (i == 0) {
            envelope.startNewSubPath(x, y);
            present.startNewSubPath(x, plot.getBottom());
            present.lineTo(x, y);
        } else {
            envelope.lineTo(x, y);
            present.lineTo(x, y);
        }
    }
    present.lineTo(plot.getRight(), plot.getBottom());
    present.closeSubPath();

    g.setColour(DropperLookAndFeel::knobFace());
    g.fillRect(plot);
    g.setColour(DropperLookAndFeel::accent().withAlpha(0.16f));
    g.fillPath(present);

    g.setColour(DropperLookAndFeel::track());
    for (int grid = 1; grid < 4; ++grid) {
        const float y = plot.getY() + plot.getHeight() * static_cast<float>(grid) / 4.f;
        g.drawHorizontalLine(static_cast<int>(y), plot.getX(), plot.getRight());
    }

    g.setColour(DropperLookAndFeel::text());
    g.strokePath(envelope, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    if (window > 0.0) {
        double phase = playhead - windowStart;
        if (geo.repeatable)
            phase = std::fmod(phase, window);
        if (phase < 0.0)
            phase += window;
        const float px = plot.getX() + plot.getWidth() * static_cast<float>(phase / window);
        g.setColour(DropperLookAndFeel::accent());
        g.drawLine(px, plot.getY(), px, plot.getBottom(), 1.f);
    }

    g.setColour(DropperLookAndFeel::muted());
    g.setFont(DropperLookAndFeel::uiFont(11.f));
    juce::String title = geo.repeatable ? "FIXED PATTERN" : "UPCOMING DROPOUTS";
    if (geo.bypass)
        title = "SIGNAL PASSING";
    g.drawText(title, bounds.reduced(10.f, 4.f).removeFromTop(16.f), juce::Justification::centredLeft);

    const int pct = columns > 0 ? static_cast<int>(std::lround(100.0 * drops / columns)) : 0;
    g.setColour(geo.bypass ? DropperLookAndFeel::muted() : DropperLookAndFeel::accent());
    g.drawText(geo.bypass ? "DRY" : juce::String(pct) + "% DROP",
               bounds.reduced(12.f, 4.f).removeFromTop(16.f), juce::Justification::centredRight);

    if (!showDegrade)
        return;

    auto status = bounds.reduced(10.f, 6.f).removeFromBottom(16.f);
    const float ceiling = std::max(0.001f, shownCeiling);
    const float fraction = std::max(0.f, std::min(1.f, shownLevel / ceiling));
    auto bar = status.removeFromLeft(72.f).reduced(0.f, 5.f);
    g.setColour(DropperLookAndFeel::track());
    g.fillRect(bar);
    g.setColour(DropperLookAndFeel::accent());
    g.fillRect(bar.withWidth(bar.getWidth() * fraction));

    const auto clock = [](double seconds) {
        const int total = std::max(0, static_cast<int>(std::lround(seconds)));
        return juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2);
    };
    const int percent = static_cast<int>(std::lround(100.0 * std::min(1.0, static_cast<double>(shownLevel))));
    juce::String text = juce::String(percent) + "%   " + clock(shownElapsed)
                        + "   " + clock(shownRemaining) + " LEFT";
    if (shownHeld)
        text += "   HOLD";
    g.setColour(DropperLookAndFeel::muted());
    g.setFont(DropperLookAndFeel::uiFont(11.f));
    g.drawText(text, status.reduced(8.f, 0.f), juce::Justification::centredLeft);
}
