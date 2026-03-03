#pragma once

#include "../../JuceHeader.h"
#include "../../Utils/UI/Theme.h"
#include <cmath>

/**
 * Premium rotary knob LookAndFeel.
 *
 * Renders a smooth, modern knob with:
 *  - Concentric arc value indicator (bipolar from centre)
 *  - Soft radial gradient body with top highlight
 *  - Clean pointer line with accent-coloured tip dot
 *  - Subtle drop-shadow for depth
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
        const float radius   = (diameter * 0.5f) - 4.0f;
        const float centreX  = static_cast<float>(x) + static_cast<float>(width)  * 0.5f;
        const float centreY  = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        const float angle    = rotaryStartAngle
                             + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        const bool  isEnabled = slider.isEnabled();
        const float alpha     = isEnabled ? 1.0f : 0.35f;

        // ── 1. Track arc (inactive portion) ──────────────────────
        const float arcRadius   = radius + 2.5f;
        const float arcWidth    = 2.8f;

        {
            juce::Path track;
            track.addCentredArc(centreX, centreY, arcRadius, arcRadius, 0.0f,
                                rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(APP_COLOR_BORDER.withAlpha(alpha * 0.55f));
            g.strokePath(track, juce::PathStrokeType(arcWidth, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }

        // ── 2. Value arc (bipolar from centre) ───────────────────
        {
            const double minVal   = slider.getMinimum();
            const double maxVal   = slider.getMaximum();
            const double midVal   = (minVal + maxVal) * 0.5;
            const double curVal   = slider.getValue();
            const double span     = static_cast<double>(rotaryEndAngle - rotaryStartAngle);
            const double midAngle = static_cast<double>(rotaryStartAngle) + span * 0.5;
            const double offset   = (curVal - midVal) / (maxVal - minVal); // [-0.5, 0.5]
            const double arcSpan  = offset * span;

            if (std::abs(arcSpan) > 1e-6)
            {
                const double a0 = arcSpan > 0.0 ? midAngle : midAngle + arcSpan;
                const double a1 = arcSpan > 0.0 ? midAngle + arcSpan : midAngle;

                juce::Path valueArc;
                valueArc.addCentredArc(centreX, centreY, arcRadius, arcRadius, 0.0f,
                                       static_cast<float>(a0), static_cast<float>(a1), true);
                g.setColour(APP_COLOR_PRIMARY.withAlpha(alpha));
                g.strokePath(valueArc, juce::PathStrokeType(arcWidth, juce::PathStrokeType::curved,
                                                             juce::PathStrokeType::rounded));
            }
        }

        // ── 3. Knob body ─────────────────────────────────────────
        const float knobR = radius * 0.82f;

        // Drop shadow — two layers for soft spread
        {
            g.setColour(APP_COLOR_KNOB_SHADOW.withAlpha(alpha * 0.35f));
            g.fillEllipse(centreX - knobR - 0.5f, centreY - knobR + 2.5f,
                          knobR * 2.0f + 1.0f, knobR * 2.0f + 1.0f);
            g.setColour(APP_COLOR_KNOB_SHADOW.withAlpha(alpha * 0.2f));
            g.fillEllipse(centreX - knobR - 1.0f, centreY - knobR + 4.0f,
                          knobR * 2.0f + 2.0f, knobR * 2.0f + 2.0f);
        }

        // Radial gradient body (lighter at top-left, darker at bottom-right)
        {
            auto topColour = APP_COLOR_SURFACE_RAISED.brighter(0.12f).withAlpha(alpha);
            auto botColour = APP_COLOR_SURFACE.darker(0.05f).withAlpha(alpha);
            juce::ColourGradient bodyGrad(topColour, centreX, centreY - knobR * 0.7f,
                                          botColour, centreX, centreY + knobR * 0.7f, true);
            g.setGradientFill(bodyGrad);
            g.fillEllipse(centreX - knobR, centreY - knobR, knobR * 2.0f, knobR * 2.0f);
        }

        // Rim / bevel
        {
            g.setColour(APP_COLOR_BORDER.withAlpha(alpha * 0.6f));
            g.drawEllipse(centreX - knobR, centreY - knobR, knobR * 2.0f, knobR * 2.0f, 1.0f);
        }

        // Top highlight crescent (gives the 3-D convex look)
        {
            auto hlRadius = knobR * 0.72f;
            auto hlY = centreY - knobR * 0.25f;
            g.setColour(juce::Colour(0x0AFFFFFF).withAlpha(alpha));
            g.fillEllipse(centreX - hlRadius, hlY - hlRadius, hlRadius * 2.0f, hlRadius * 2.0f);
        }

        // ── 4. Pointer line ──────────────────────────────────────
        const float ptrLen   = knobR * 0.55f;
        const float ptrStart = knobR * 0.18f;

        {
            juce::Path pointer;
            pointer.startNewSubPath(0.0f, -ptrStart);
            pointer.lineTo(0.0f, -ptrLen);

            g.setColour(APP_COLOR_PRIMARY.withAlpha(alpha));
            g.strokePath(pointer,
                         juce::PathStrokeType(2.6f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded),
                         juce::AffineTransform::rotation(angle).translated(centreX, centreY));
        }

        // Tip dot
        {
            const float tipX = centreX + std::sin(angle) * (ptrLen - 1.5f);
            const float tipY = centreY - std::cos(angle) * (ptrLen - 1.5f);
            g.setColour(APP_COLOR_PRIMARY.withAlpha(alpha));
            g.fillEllipse(tipX - 2.2f, tipY - 2.2f, 4.4f, 4.4f);
        }
    }

    static KnobLookAndFeel& getInstance()
    {
        static KnobLookAndFeel instance;
        return instance;
    }
};
