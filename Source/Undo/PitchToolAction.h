#pragma once

#include "UndoableAction.h"
#include "../Models/Note.h"
#include "../Models/Project.h"
#include "../Utils/PitchCurveProcessor.h"
#include "../Utils/TransformParams.h"
#include <vector>
#include <functional>
#include <limits>

/**
 * Undo action for pitch tool operations (tilt, variance, smooth).
 * Stores transformation parameters, not full pitch curves.
 */
class PitchToolAction : public UndoableAction
{
public:
    PitchToolAction(
        Project* project,
        std::vector<Note*> affectedNotes,
        const std::vector<TransformParams>& oldParams,
        const std::vector<TransformParams>& newParams,
        std::function<void(int, int)> onRangeChanged = nullptr)
        : project(project),
          notes(std::move(affectedNotes)),
          oldParams(oldParams),
          newParams(newParams),
          onRangeChanged(std::move(onRangeChanged)) {}

    void undo() override
    {
        applyParams(oldParams);
    }

    void redo() override
    {
        applyParams(newParams);
    }

    juce::String getName() const override { return "Apply Pitch Tool"; }

private:
    void applyParams(const std::vector<TransformParams>& params)
    {
        for (size_t i = 0; i < notes.size(); ++i)
        {
            if (notes[i] && i < params.size())
            {
                const auto& p = params[i];
                notes[i]->setMidiNote(p.midiNote);
                notes[i]->setTiltLeft(p.tiltLeft);
                notes[i]->setTiltRight(p.tiltRight);
                notes[i]->setVarianceScale(p.varianceScale);
                notes[i]->setSmoothLeftFrames(p.smoothLeftFrames);
                notes[i]->setSmoothRightFrames(p.smoothRightFrames);
                notes[i]->setDeltaScale(p.deltaScale);
                notes[i]->setDeltaOffset(p.deltaOffset);
            }
        }

        if (project)
        {
            PitchCurveProcessor::rebuildBaseFromNotes(*project);
            PitchCurveProcessor::composeF0InPlace(*project, /*applyUvMask=*/false);

            if (!notes.empty())
            {
                int minFrame = std::numeric_limits<int>::max();
                int maxFrame = std::numeric_limits<int>::min();
                for (const auto* note : notes)
                {
                    minFrame = std::min(minFrame, note->getStartFrame());
                    maxFrame = std::max(maxFrame, note->getEndFrame());
                }
                project->setF0DirtyRange(minFrame, maxFrame);
            }
        }

        if (onRangeChanged && !notes.empty())
        {
            int minFrame = notes[0]->getStartFrame();
            int maxFrame = notes[0]->getEndFrame();
            for (const auto* note : notes)
            {
                minFrame = std::min(minFrame, note->getStartFrame());
                maxFrame = std::max(maxFrame, note->getEndFrame());
            }
            onRangeChanged(minFrame, maxFrame);
        }
    }

    Project* project;
    std::vector<Note*> notes;
    std::vector<TransformParams> oldParams;
    std::vector<TransformParams> newParams;
    std::function<void(int, int)> onRangeChanged;
};
