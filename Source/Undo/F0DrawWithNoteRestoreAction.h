#pragma once

#include "../Utils/PitchCurveProcessor.h"
#include "F0Actions.h"
#include "UndoableAction.h"
#include <functional>
#include <memory>
#include <vector>

/**
 * Groups F0 hand-draw undo with restoring note MIDI / delta snapshots and
 * redoing Repitch-style note binding (persist global delta + align note blocks).
 */
class F0DrawWithNoteRestoreAction : public UndoableAction
{
public:
    F0DrawWithNoteRestoreAction(std::unique_ptr<F0EditAction> f0ActionIn,
                                std::vector<NotePitchUndoSnapshot> snapshotsIn,
                                Project *projectIn,
                                int minFrameIn,
                                int maxFrameExclusiveIn,
                                std::function<void(int, int)> onF0ChangedIn = nullptr);

    void undo() override;
    void redo() override;
    juce::String getName() const override { return "Edit Pitch Curve"; }

private:
    std::unique_ptr<F0EditAction> f0Action;
    std::vector<NotePitchUndoSnapshot> snapshots;
    Project *project;
    int minFrame = 0;
    int maxFrameExclusive = 0;
    std::function<void(int, int)> onF0Changed;
};
