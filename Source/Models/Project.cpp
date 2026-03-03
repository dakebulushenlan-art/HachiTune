#include "Project.h"
#include "../Utils/Constants.h"
#include "../Utils/PitchCurveProcessor.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float twoPi = 6.2831853071795864769f;

    int normalizeBeatNumerator(int numerator)
    {
        return juce::jlimit(1, 32, numerator);
    }

    int normalizeBeatDenominator(int denominator)
    {
        denominator = juce::jlimit(1, 32, denominator);
        int normalized = 1;
        while (normalized < denominator)
            normalized <<= 1;
        const int lower = normalized >> 1;
        if (lower >= 1 && (denominator - lower) < (normalized - denominator))
            normalized = lower;
        return juce::jlimit(1, 32, normalized);
    }

    TimelineGridDivision normalizeGridDivision(TimelineGridDivision division)
    {
        switch (division)
        {
            case TimelineGridDivision::Whole:
            case TimelineGridDivision::Half:
            case TimelineGridDivision::Quarter:
            case TimelineGridDivision::Eighth:
            case TimelineGridDivision::Sixteenth:
            case TimelineGridDivision::ThirtySecond:
                return division;
            default:
                return TimelineGridDivision::Quarter;
        }
    }
}

Project::Project()
{
}

Note* Project::getNoteAtFrame(int frame)
{
    for (auto& note : notes)
    {
        if (note.containsFrame(frame))
            return &note;
    }
    return nullptr;
}

std::vector<Note*> Project::getNotesInRange(int startFrame, int endFrame)
{
    std::vector<Note*> result;
    for (auto& note : notes)
    {
        if (note.getStartFrame() < endFrame && note.getEndFrame() > startFrame)
            result.push_back(&note);
    }
    return result;
}

std::vector<Note*> Project::getSelectedNotes()
{
    std::vector<Note*> result;
    for (auto& note : notes)
    {
        if (note.isSelected())
            result.push_back(&note);
    }
    return result;
}

bool Project::removeNoteByStartFrame(int startFrame)
{
    for (auto it = notes.begin(); it != notes.end(); ++it)
    {
        if (it->getStartFrame() == startFrame)
        {
            notes.erase(it);
            return true;
        }
    }
    return false;
}

void Project::deselectAllNotes()
{
    for (auto& note : notes)
        note.setSelected(false);
}

void Project::selectAllNotes(bool includeRests)
{
    for (auto& note : notes)
    {
        if (!includeRests && note.isRest())
            continue;
        note.setSelected(true);
    }
}

std::vector<Note*> Project::getDirtyNotes()
{
    std::vector<Note*> result;
    for (auto& note : notes)
    {
        if (note.isDirty())
            result.push_back(&note);
    }
    return result;
}

void Project::clearAllDirty()
{
    for (auto& note : notes)
        note.clearDirty();
    // Also clear F0 dirty range
    f0DirtyStart = -1;
    f0DirtyEnd = -1;
    // Clear synthesis ceiling (set by ripple stretch)
    synthesisCeiling_ = -1;
}

bool Project::hasDirtyNotes() const
{
    for (const auto& note : notes)
    {
        if (note.isDirty())
            return true;
    }
    return false;
}

void Project::setF0DirtyRange(int startFrame, int endFrame)
{
    if (f0DirtyStart < 0 || startFrame < f0DirtyStart)
        f0DirtyStart = startFrame;
    if (f0DirtyEnd < 0 || endFrame > f0DirtyEnd)
        f0DirtyEnd = endFrame;
}

void Project::clearF0DirtyRange()
{
    f0DirtyStart = -1;
    f0DirtyEnd = -1;
}

bool Project::hasF0DirtyRange() const
{
    return f0DirtyStart >= 0 && f0DirtyEnd >= 0;
}

std::pair<int, int> Project::getF0DirtyRange() const
{
    return {f0DirtyStart, f0DirtyEnd};
}

std::pair<int, int> Project::getDirtyFrameRange() const
{
    int minStart = -1;
    int maxEnd = -1;
    
    // Check dirty notes
    for (const auto& note : notes)
    {
        if (note.isDirty())
        {
            if (minStart < 0 || note.getStartFrame() < minStart)
                minStart = note.getStartFrame();
            if (maxEnd < 0 || note.getEndFrame() > maxEnd)
                maxEnd = note.getEndFrame();
        }
    }
    
    // Also include F0 dirty range from Draw mode edits
    if (f0DirtyStart >= 0)
    {
        if (minStart < 0 || f0DirtyStart < minStart)
            minStart = f0DirtyStart;
    }
    if (f0DirtyEnd >= 0)
    {
        if (maxEnd < 0 || f0DirtyEnd > maxEnd)
            maxEnd = f0DirtyEnd;
    }
    
    return {minStart, maxEnd};
}

std::vector<float> Project::getAdjustedF0() const
{
    if (audioData.basePitch.empty() || audioData.deltaPitch.empty())
        return {};

    // Compose base + delta as dense curve; UV blending is handled downstream
    // by synthesis masks, so we do not zero F0 here.
    std::vector<float> adjustedF0 = PitchCurveProcessor::composeF0(*this,
                                                                   /*applyUvMask=*/false,
                                                                   globalPitchOffset);

    // Apply vibrato per note on top of composed curve
    for (const auto& note : notes)
    {
        const bool hasVibrato = note.isVibratoEnabled() &&
                                note.getVibratoDepthSemitones() > 0.0001f &&
                                note.getVibratoRateHz() > 0.0001f;
        if (!hasVibrato)
            continue;

        const int start = std::max(0, note.getStartFrame());
        const int end = std::min(note.getEndFrame(), static_cast<int>(adjustedF0.size()));

        for (int i = start; i < end; ++i)
        {
            if (i < static_cast<int>(audioData.voicedMask.size()) && !audioData.voicedMask[i])
                continue;

            float vib = note.getVibratoDepthSemitones() *
                        std::sin(twoPi * note.getVibratoRateHz() * framesToSeconds(i - start) +
                                 note.getVibratoPhaseRadians());
            adjustedF0[static_cast<size_t>(i)] *= std::pow(2.0f, vib / 12.0f);
        }
    }

    return adjustedF0;
}

std::vector<float> Project::getAdjustedF0ForRange(int startFrame, int endFrame) const
{
    if (audioData.basePitch.empty() || audioData.deltaPitch.empty())
        return {};

    // Clamp range
    startFrame = std::max(0, startFrame);
    endFrame = std::min(endFrame, static_cast<int>(audioData.basePitch.size()));

    if (startFrame >= endFrame)
        return {};

    const int rangeSize = endFrame - startFrame;
    std::vector<float> adjustedF0(static_cast<size_t>(rangeSize), 0.0f);

    for (int i = 0; i < rangeSize; ++i)
    {
        const int globalIdx = startFrame + i;
        const float base = audioData.basePitch[static_cast<size_t>(globalIdx)];
        const float delta = (globalIdx < static_cast<int>(audioData.deltaPitch.size()))
                                ? audioData.deltaPitch[static_cast<size_t>(globalIdx)]
                                : 0.0f;
        float midi = base + delta + globalPitchOffset;
        adjustedF0[static_cast<size_t>(i)] = midiToFreq(midi);
    }

    // Apply vibrato for overlapping notes
    for (const auto& note : notes)
    {
        const bool hasVibrato = note.isVibratoEnabled() &&
                                note.getVibratoDepthSemitones() > 0.0001f &&
                                note.getVibratoRateHz() > 0.0001f;
        if (!hasVibrato)
            continue;

        const int overlapStart = std::max(note.getStartFrame(), startFrame);
        const int overlapEnd = std::min(note.getEndFrame(), endFrame);
        for (int frame = overlapStart; frame < overlapEnd; ++frame)
        {
            const int localIdx = frame - startFrame;
            if (frame < static_cast<int>(audioData.voicedMask.size()) && !audioData.voicedMask[frame])
                continue;

            float vib = note.getVibratoDepthSemitones() *
                        std::sin(twoPi * note.getVibratoRateHz() * framesToSeconds(frame - note.getStartFrame()) +
                                 note.getVibratoPhaseRadians());
            adjustedF0[static_cast<size_t>(localIdx)] *= std::pow(2.0f, vib / 12.0f);
        }
    }

    return adjustedF0;
}

void Project::setLoopRange(double startSeconds, double endSeconds)
{
    if (startSeconds > endSeconds)
        std::swap(startSeconds, endSeconds);

    const double duration = audioData.getDuration();
    if (duration > 0.0)
    {
        startSeconds = juce::jlimit(0.0, duration, startSeconds);
        endSeconds = juce::jlimit(0.0, duration, endSeconds);
    }

    loopRange.startSeconds = startSeconds;
    loopRange.endSeconds = endSeconds;
    loopRange.enabled = loopRange.endSeconds > loopRange.startSeconds;
}

void Project::setLoopEnabled(bool enabled)
{
    if (enabled && loopRange.endSeconds <= loopRange.startSeconds)
        loopRange.enabled = false;
    else
        loopRange.enabled = enabled;
}

void Project::clearLoopRange()
{
    loopRange = {};
}

void Project::setScaleMode(ScaleMode mode)
{
    if (scaleMode == mode)
        return;

    scaleMode = mode;
    modified = true;
}

void Project::setScaleRootNote(int noteInOctave)
{
    const int normalized = juce::jlimit(-1, 11, noteInOctave);
    if (scaleRootNote == normalized)
        return;

    scaleRootNote = normalized;
    modified = true;
}

void Project::setPitchReferenceHz(int hz)
{
    const int normalized = juce::jlimit(380, 480, hz);
    if (pitchReferenceHz == normalized)
        return;

    pitchReferenceHz = normalized;
    modified = true;
}

void Project::setShowScaleColors(bool enabled)
{
    if (showScaleColors == enabled)
        return;

    showScaleColors = enabled;
    modified = true;
}

void Project::setSnapToSemitones(bool enabled)
{
    if (snapToSemitones == enabled)
        return;

    snapToSemitones = enabled;
    modified = true;
}

void Project::setDoubleClickSnapMode(DoubleClickSnapMode mode)
{
    if (doubleClickSnapMode == mode)
        return;

    doubleClickSnapMode = mode;
    modified = true;
}

void Project::setTimelineDisplayMode(TimelineDisplayMode mode)
{
    if (timelineDisplayMode == mode)
        return;

    timelineDisplayMode = mode;
    modified = true;
}

void Project::setTimelineBeatSignature(int numerator, int denominator)
{
    const int normalizedNumerator = normalizeBeatNumerator(numerator);
    const int normalizedDenominator = normalizeBeatDenominator(denominator);

    if (timelineBeatNumerator == normalizedNumerator &&
        timelineBeatDenominator == normalizedDenominator)
        return;

    timelineBeatNumerator = normalizedNumerator;
    timelineBeatDenominator = normalizedDenominator;
    modified = true;
}

void Project::setTimelineTempoBpm(double bpm)
{
    const double normalized = juce::jlimit(20.0, 300.0, bpm);
    if (std::abs(timelineTempoBpm - normalized) < 1.0e-6)
        return;

    timelineTempoBpm = normalized;
    modified = true;
}

void Project::setTimelineGridDivision(TimelineGridDivision division)
{
    const auto normalized = normalizeGridDivision(division);
    if (timelineGridDivision == normalized)
        return;

    timelineGridDivision = normalized;
    modified = true;
}

void Project::setTimelineSnapCycle(bool enabled)
{
    if (timelineSnapCycle == enabled)
        return;

    timelineSnapCycle = enabled;
    modified = true;
}
