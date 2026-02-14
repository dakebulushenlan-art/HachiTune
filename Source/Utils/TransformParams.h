#pragma once

/**
 * Stores pitch tool transformation parameters for a single note.
 * Used by UndoManager to capture and restore transformation state non-destructively.
 */
struct TransformParams
{
    float tiltLeft = 0.0f;           // Tilt amount at left edge (semitones)
    float tiltRight = 0.0f;          // Tilt amount at right edge (semitones)
    float varianceScale = 1.0f;      // Variance scaling factor (1.0=unchanged, 0.0=flat, >1.0=amplify, <0.0=invert)
    int smoothLeftFrames = 0;        // Smoothing transition length at left boundary
    int smoothRightFrames = 0;       // Smoothing transition length at right boundary

    TransformParams() = default;

    TransformParams(float tiltL, float tiltR, float varScale, int smoothL, int smoothR)
        : tiltLeft(tiltL), tiltRight(tiltR), varianceScale(varScale),
          smoothLeftFrames(smoothL), smoothRightFrames(smoothR) {}

    bool operator==(const TransformParams& other) const
    {
        return tiltLeft == other.tiltLeft &&
               tiltRight == other.tiltRight &&
               varianceScale == other.varianceScale &&
               smoothLeftFrames == other.smoothLeftFrames &&
               smoothRightFrames == other.smoothRightFrames;
    }

    bool operator!=(const TransformParams& other) const
    {
        return !(*this == other);
    }

    // Check if parameters are at default (identity transformation)
    bool isIdentity() const
    {
        return tiltLeft == 0.0f &&
               tiltRight == 0.0f &&
               varianceScale == 1.0f &&
               smoothLeftFrames == 0 &&
               smoothRightFrames == 0;
    }
};
