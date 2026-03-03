#include "SplitHandler.h"
#include "../../PianoRollComponent.h"
#include "../NoteSplitter.h"

SplitHandler::SplitHandler(PianoRollComponent &owner)
    : InteractionHandler(owner) {}

bool SplitHandler::mouseDown(const juce::MouseEvent &e, float worldX,
                             float worldY) {
  juce::ignoreUnused(e);

  Note *note = owner_.noteSplitter->findNoteAt(worldX, worldY);
  if (note) {
    owner_.noteSplitter->splitNoteAtX(note, worldX);
    return true;
  }
  return false;
}

void SplitHandler::mouseMove(const juce::MouseEvent &e, float worldX,
                             float worldY) {
  juce::ignoreUnused(e);

  if (!owner_.project) {
    clearGuide();
    return;
  }

  Note *note = owner_.noteSplitter->findNoteAt(worldX, worldY);
  if (note) {
    splitGuideX = worldX;
    splitGuideNote = note;
  } else {
    splitGuideX = -1.0f;
    splitGuideNote = nullptr;
  }
  owner_.repaint();
}

void SplitHandler::cancel() { clearGuide(); }

void SplitHandler::clearGuide() {
  if (splitGuideX >= 0) {
    splitGuideX = -1.0f;
    splitGuideNote = nullptr;
    owner_.repaint();
  }
}
