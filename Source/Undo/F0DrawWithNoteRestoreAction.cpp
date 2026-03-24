#include "F0DrawWithNoteRestoreAction.h"
#include "../Models/Project.h"

F0DrawWithNoteRestoreAction::F0DrawWithNoteRestoreAction(
    std::unique_ptr<F0EditAction> f0ActionIn,
    std::vector<NotePitchUndoSnapshot> snapshotsIn,
    Project *projectIn,
    int minFrameIn,
    int maxFrameExclusiveIn,
    std::function<void(int, int)> onF0ChangedIn)
    : f0Action(std::move(f0ActionIn)),
      snapshots(std::move(snapshotsIn)),
      project(projectIn),
      minFrame(minFrameIn),
      maxFrameExclusive(maxFrameExclusiveIn),
      onF0Changed(std::move(onF0ChangedIn))
{
}

void F0DrawWithNoteRestoreAction::undo()
{
    if (f0Action)
        f0Action->undo();
    if (project)
    {
        PitchCurveProcessor::restoreNotesFromPitchSnapshots(*project, snapshots);
        PitchCurveProcessor::rebuildBaseFromNotes(*project);
    }
    if (onF0Changed && minFrame <= maxFrameExclusive - 1)
        onF0Changed(minFrame, maxFrameExclusive - 1);
}

void F0DrawWithNoteRestoreAction::redo()
{
    if (f0Action)
        f0Action->redo();
    if (project)
    {
        PitchCurveProcessor::persistGlobalDeltaToOverlappingNotes(*project, minFrame,
                                                                  maxFrameExclusive);
        PitchCurveProcessor::bindOverlappingNotesToDrawnPitch(*project, minFrame,
                                                               maxFrameExclusive);
    }
    if (onF0Changed && minFrame <= maxFrameExclusive - 1)
        onF0Changed(minFrame, maxFrameExclusive - 1);
}
