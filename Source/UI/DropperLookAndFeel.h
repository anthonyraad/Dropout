#pragma once

#include <JuceHeader.h>

class DropperLookAndFeel : public juce::LookAndFeel_V4 {
public:
    DropperLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getPopupMenuFont() override;

    static juce::Colour background() { return juce::Colour(0xffd6d6d6); }
    static juce::Colour panel() { return juce::Colour(0xffececec); }
    static juce::Colour surface() { return juce::Colour(0xffffffff); }
    static juce::Colour line() { return juce::Colour(0xff7a7a7a); }
    static juce::Colour text() { return juce::Colour(0xff141414); }
    static juce::Colour muted() { return juce::Colour(0xff3a3a3a); }
    static juce::Colour accent() { return juce::Colour(0xff3e4ed8); }
    static juce::Colour accentEdge() { return juce::Colour(0xff2a38b0); }
    static juce::Colour track() { return juce::Colour(0xffb4b8d0); }
    static juce::Colour wash() { return juce::Colour(0xffe4e7ff); }
    static juce::Colour knobFace() { return juce::Colour(0xfff4f6ff); }
    static juce::Colour knobEdge() { return juce::Colour(0xff4a4a4a); }
    static juce::Colour buttonFill() { return juce::Colour(0xff2c2c2c); }
    static juce::Colour buttonText() { return juce::Colour(0xfff6f6f6); }

    static juce::Font uiFont(float size, bool bold = false);
};
