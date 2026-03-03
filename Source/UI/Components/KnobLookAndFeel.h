#pragma once

#include "../../JuceHeader.h"
#include "../../Utils/UI/Theme.h"
#include <cmath>

/**
 * VST-style 3D knob LookAndFeel.
 * Draws a realistic rotary knob with metallic appearance and pointer indicator.
 */
class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KnobLookAndFeel() = default;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override
    {
        const float diameter = static_cast<float>(juce::jmin(width, height));
        const float radius = (diameter / 2.0f) - 4.0f;
        const float centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
        const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        const bool isEnabled = slider.isEnabled();
        const float alpha = isEnabled ? 1.0f : 0.4f;

        // === Outer progress ring ===
        const float trackRadius = radius + 2.0f;
        const float ringThickness = 3.0f;
        juce::Path ringPath;
        ringPath.addCentredArc(centreX, centreY, trackRadius, trackRadius, 0.0f,
                               rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(APP_COLOR_BORDER.withAlpha(alpha));
        g.strokePath(ringPath, juce::PathStrokeType(ringThickness, juce::PathStrokeType::curved));

        // Bipolar progress from center (for symmetric ranges like -12..+12)
        const double minValue = slider.getMinimum();
        const double maxValue = slider.getMaximum();
        const double midValue = (minValue + maxValue) * 0.5;
        const double value = slider.getValue();
        const double span = static_cast<double>(rotaryEndAngle - rotaryStartAngle);
        const double midAngle = static_cast<double>(rotaryStartAngle) + span * 0.5;
        const double valueOffset = (value - midValue) / (maxValue - minValue); // [-0.5, 0.5]
        const double arcSpan = valueOffset * span;

        if (std::abs(arcSpan) > 1e-6)
        {
            juce::Path valuePath;
            const double startAngle = arcSpan > 0.0 ? midAngle : midAngle + arcSpan;
            const double endAngle = arcSpan > 0.0 ? midAngle + arcSpan : midAngle;
            valuePath.addCentredArc(centreX, centreY, trackRadius, trackRadius, 0.0f,
                                    static_cast<float>(startAngle), static_cast<float>(endAngle), true);
            g.setColour(APP_COLOR_PRIMARY.withAlpha(alpha));
            g.strokePath(valuePath, juce::PathStrokeType(ringThickness, juce::PathStrokeType::curved));
        }

        // === Knob body ===
        const float knobRadius = radius * 0.85f;

        // Outer shadow
        g.setColour(APP_COLOR_KNOB_SHADOW.withAlpha(alpha * 0.5f));
        g.fillEllipse(centreX - knobRadius - 1.0f, centreY - knobRadius + 2.0f,
                      knobRadius * 2.0f + 2.0f, knobRadius * 2.0f + 2.0f);

        // Main knob body - gradient from top-left to bottom-right
        juce::ColourGradient bodyGradient(
            APP_COLOR_SURFACE_RAISED.withAlpha(alpha), centreX - knobRadius * 0.7f, centreY - knobRadius * 0.7f,
            APP_COLOR_SURFACE_ALT.withAlpha(alpha), centreX + knobRadius * 0.7f, centreY + knobRadius * 0.7f, false);
        g.setGradientFill(bodyGradient);
        g.fillEllipse(centreX - knobRadius, centreY - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);

        // Inner bevel / rim
        g.setColour(APP_COLOR_BORDER.withAlpha(alpha));
        g.drawEllipse(centreX - knobRadius + 1.5f, centreY - knobRadius + 1.5f,
                      (knobRadius - 1.5f) * 2.0f, (knobRadius - 1.5f) * 2.0f, 1.0f);

        // === Pointer line ===
        const float pointerLength = knobRadius * 0.6f;
        const float pointerStartRadius = knobRadius * 0.2f;

        juce::Path pointer;
        pointer.startNewSubPath(0.0f, -pointerStartRadius);
        pointer.lineTo(0.0f, -pointerLength);

        g.setColour(APP_COLOR_PRIMARY.withAlpha(alpha));
        g.strokePath(pointer, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                     juce::AffineTransform::rotation(angle).translated(centreX, centreY));

        // Small dot at pointer tip
        float tipX = centreX + std::sin(angle) * (pointerLength - 2.0f);
        float tipY = centreY - std::cos(angle) * (pointerLength - 2.0f);
        g.fillEllipse(tipX - 2.5f, tipY - 2.5f, 5.0f, 5.0f);
    }

    static KnobLookAndFeel& getInstance()
    {
        static KnobLookAndFeel instance;
        return instance;
    }
};
