#pragma once

#include "../Models/Project.h"
#include <cstddef>
#include <vector>

struct NotePitchUndoSnapshot
{
    std::size_t noteIndex = 0;
    float midiNote = 60.0f;
    float pitchOffset = 0.0f;
    std::vector<float> originalDeltaPitch;
    std::vector<float> deltaPitch;
    float tiltLeft = 0.0f;
    float tiltRight = 0.0f;
    float varianceScale = 1.0f;
    float pitchDriftTrim = 0.0f;
    int smoothLeftFrames = 0;
    int smoothRightFrames = 0;
    float deltaScale = 1.0f;
    float deltaOffset = 0.0f;
};

namespace PitchCurveProcessor
{
    /**
     * Linearly interpolate pitch through unvoiced regions using the uv mask.
     * Returns a dense pitch (Hz) array with the same length as the input.
     */
    std::vector<float> interpolateWithUvMask(const std::vector<float>& pitchHz,
                                             const std::vector<bool>& uvMask);

    /**
     * Rebuild base pitch (midi) from current notes and keep existing delta.
     * Ensures base/delta are dense and aligned to the project frame count,
     * then composes audioData.f0 (without applying the uv mask).
     */
    void rebuildBaseFromNotes(Project& project);

    /**
     * Rebuild delta pitch and f0 only for specific notes (partial rebuild).
     * Skips basePitch regeneration (assumes positions unchanged).
     * Much faster than rebuildBaseFromNotes() when only transformation
     * parameters change on a few notes.
     */
    void rebuildDeltaForNotes(Project& project, const std::vector<Note*>& affectedNotes);

    /**
     * Lightweight rebuild for interactive stretch drag.
     * Regenerates basePitch from ALL notes (needed for correct pitch display),
     * but only processes deltaPitch for the specified affected notes instead
     * of iterating every note.  Recomposes f0 only for the affected range.
     * Much faster than rebuildBaseFromNotes() during interactive drag.
     */
    void rebuildBaseFromNotesForDrag(Project& project, const std::vector<Note*>& affectedNotes);

    /**
     * Rebuild base and delta from a source pitch (Hz). This is used after
     * detection/segmentation or when we need to recompute delta from edited
     * curves. The uv mask is kept as-is; the composed f0 omits uv masking.
     */
    void rebuildCurvesFromSource(Project& project,
                                 const std::vector<float>& sourcePitchHz);

    /**
     * Compose f0 (Hz) from base + delta + optional global offset.
     * When applyUvMask is true, frames marked unvoiced are forced to 0 for
     * synthesis; when false the curve stays dense for UI display.
     */
    std::vector<float> composeF0(const Project& project,
                                 bool applyUvMask,
                                 float globalPitchOffset = 0.0f);

    /**
     * Convenience to update audioData.f0 in-place using composeF0.
     */
    void composeF0InPlace(Project& project,
                          bool applyUvMask,
                          float globalPitchOffset = 0.0f);

    /** Copy global deltaPitch into overlapping notes (original + delta arrays). */
    void persistGlobalDeltaToOverlappingNotes(Project& project,
                                               int minFrame,
                                               int maxFrameExclusive);

    /**
     * Move each affected note's base MIDI toward the mean of the current
     * absolute pitch curve (base+delta), then rebuild residual deltas.
     * Resets pitch-tool parameters on affected notes for a consistent model.
     */
    void bindOverlappingNotesToDrawnPitch(Project& project,
                                          int minFrame,
                                          int maxFrameExclusive);

    std::vector<NotePitchUndoSnapshot>
    captureNotesOverlappingRange(const Project& project,
                                 int minFrame,
                                 int maxFrameExclusive);

    /**
     * Copies one note's pitch tool + delta state into out. If the note has no
     * per-note delta curves, copies the global dense delta for that note's span.
     * Returns false if noteIndex is out of range or the note is a rest.
     */
    bool tryCaptureNotePitchSnapshot(const Project& project,
                                     std::size_t noteIndex,
                                     NotePitchUndoSnapshot& out);

    void restoreNotesFromPitchSnapshots(Project& project,
                                        const std::vector<NotePitchUndoSnapshot>& snapshots);
} // namespace PitchCurveProcessor


