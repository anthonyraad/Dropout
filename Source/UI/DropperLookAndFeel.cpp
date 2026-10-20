#include "UI/DropperLookAndFeel.h"

juce::Font DropperLookAndFeel::uiFont(float size, bool bold) {
    auto options = juce::FontOptions(size).withName("Segoe UI");
    if (bold)
        options = options.withStyle("Bold");
    return juce::Font(options);
}

DropperLookAndFeel::DropperLookAndFeel() {
    setColour(juce::Slider::textBoxTextColourId, text());
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, muted());
    setColour(juce::ComboBox::backgroundColourId, surface());
    setColour(juce::ComboBox::textColourId, text());
    setColour(juce::ComboBox::outlineColourId, knobEdge());
    setColour(juce::ComboBox::arrowColourId, text());
    setColour(juce::PopupMenu::backgroundColourId, surface());
    setColour(juce::PopupMenu::textColourId, text());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, wash());
    setColour(juce::PopupMenu::highlightedTextColourId, text());
    setColour(juce::TextButton::textColourOffId, buttonText());
    setColour(juce::TextButton::textColourOnId, surface());
}

void DropperLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                         juce::Slider& slider) {
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                static_cast<float>(width), static_cast<float>(height))
                            .reduced(2.f);
    const float size = std::min(bounds.getWidth(), bounds.getHeight());
    const auto knob = juce::Rectangle<float>(size, size).withCentre(bounds.getCentre());
    const auto centre = knob.getCentre();
    const float radius = size * 0.5f - 1.f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    juce::Graphics::ScopedSaveState saved(g);
    g.setOpacity(slider.isEnabled() ? 1.f : 0.38f);
    const bool bipolar = slider.getMinimum() < 0.0;
    const bool endKnob = slider.getComponentID() == "the-end";
    const float centrePos = bipolar ? 0.5f : 0.f;
    const float arcRadius = radius - 4.f;
    const float stroke = size > 120.f ? 3.f : 2.f;

    g.setColour(endKnob ? juce::Colour(0xff6e6e6e) : knobFace());
    g.fillEllipse(knob);
    g.setColour(endKnob ? juce::Colour(0xff2a2a2a) : knobEdge());
    g.drawEllipse(knob.reduced(0.5f), 1.f);

    juce::Path trackPath;
    trackPath.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(endKnob ? juce::Colour(0xff3f3f3f) : track());
    g.strokePath(trackPath, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    const float origin = bipolar ? rotaryStartAngle + centrePos * (rotaryEndAngle - rotaryStartAngle) : rotaryStartAngle;
    if (std::abs(sliderPos - centrePos) > 0.004f) {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.f,
                            std::min(origin, angle), std::max(origin, angle), true);
        g.setColour(endKnob ? juce::Colours::white
                           : (bipolar && sliderPos < centrePos ? knobEdge() : accent()));
        g.strokePath(value, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    if (bipolar) {
        const auto tip = centre.getPointOnCircumference(radius - 1.f, origin);
        const auto root = centre.getPointOnCircumference(radius - 9.f, origin);
        g.setColour(text());
        g.drawLine({ root, tip }, 1.f);
    }

    const auto p0 = centre.getPointOnCircumference(size * 0.18f, angle);
    const auto p1 = centre.getPointOnCircumference(size * 0.36f, angle);
    g.setColour(endKnob ? juce::Colours::white : text());
    g.drawLine({ p0, p1 }, endKnob ? 2.2f : 1.5f);
}

void DropperLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                             const juce::Colour&, bool highlighted, bool down) {
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = button.getToggleState();
    juce::Colour fill = buttonFill();
    juce::Colour outline = juce::Colour(0xff1a1a1a);

    if (on) {
        fill = accent();
        outline = accentEdge();
    } else if (down) {
        fill = juce::Colour(0xff1a1a1a);
    } else if (highlighted) {
        fill = juce::Colour(0xff3f3f3f);
        outline = accent();
    }

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 2.f);
    g.setColour(outline);
    g.drawRoundedRectangle(bounds, 2.f, 1.f);
}

void DropperLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) {
    g.setColour(button.getToggleState() ? surface() : buttonText());
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 2),
                     juce::Justification::centred, 1);
}

void DropperLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                     int, int, int, int, juce::ComboBox& box) {
    auto bounds = juce::Rectangle<float>(0.f, 0.f, static_cast<float>(width), static_cast<float>(height));
    g.setColour(box.isEnabled() ? surface() : panel());
    g.fillRoundedRectangle(bounds, 2.f);
    g.setColour(box.isEnabled() ? knobEdge() : line());
    g.drawRoundedRectangle(bounds.reduced(0.5f), 2.f, 1.f);

    juce::Path arrow;
    const float cx = static_cast<float>(width) - 12.f;
    const float cy = static_cast<float>(height) * 0.5f;
    arrow.addTriangle(cx - 3.5f, cy - 1.5f, cx + 3.5f, cy - 1.5f, cx, cy + 2.5f);
    g.setColour(box.isEnabled() ? text() : line());
    g.fillPath(arrow);
}

void DropperLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(8, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

juce::Font DropperLookAndFeel::getComboBoxFont(juce::ComboBox&) {
    return uiFont(13.f);
}

juce::Font DropperLookAndFeel::getTextButtonFont(juce::TextButton&, int) {
    return uiFont(12.f);
}

juce::Font DropperLookAndFeel::getLabelFont(juce::Label& label) {
    return label.getFont();
}

juce::Font DropperLookAndFeel::getPopupMenuFont() {
    return uiFont(13.f);
}
