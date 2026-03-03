#pragma once

#include "UndoableAction.h"
#include <vector>
#include <memory>
#include <functional>

/**
 * Simple undo manager for the pitch editor.
 */
class PitchUndoManager
{
public:
    PitchUndoManager(size_t maxHistory = 100) : maxHistory(maxHistory) {}
    
    void addAction(std::unique_ptr<UndoableAction> action)
    {
        // Clear redo stack when new action is added
        redoStack.clear();
        redoStack.shrink_to_fit();

        undoStack.push_back(std::move(action));

        // Limit history size
        while (undoStack.size() > maxHistory)
        {
            undoStack.erase(undoStack.begin());
        }

        if (onHistoryChanged)
            onHistoryChanged();
    }
    
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    
    void undo()
    {
        if (undoStack.empty()) return;
        
        auto action = std::move(undoStack.back());
        undoStack.pop_back();
        
        action->undo();
        redoStack.push_back(std::move(action));
        
        if (onHistoryChanged)
            onHistoryChanged();
    }
    
    void redo()
    {
        if (redoStack.empty()) return;
        
        auto action = std::move(redoStack.back());
        redoStack.pop_back();
        
        action->redo();
        undoStack.push_back(std::move(action));
        
        if (onHistoryChanged)
            onHistoryChanged();
    }
    
    void clear()
    {
        undoStack.clear();
        undoStack.shrink_to_fit();
        redoStack.clear();
        redoStack.shrink_to_fit();

        if (onHistoryChanged)
            onHistoryChanged();
    }
    
    juce::String getUndoName() const
    {
        return undoStack.empty() ? "" : undoStack.back()->getName();
    }
    
    juce::String getRedoName() const
    {
        return redoStack.empty() ? "" : redoStack.back()->getName();
    }
    
    std::function<void()> onHistoryChanged;
    
private:
    std::vector<std::unique_ptr<UndoableAction>> undoStack;
    std::vector<std::unique_ptr<UndoableAction>> redoStack;
    size_t maxHistory;
};
