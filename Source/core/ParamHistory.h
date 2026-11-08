#pragma once
// Undo/redo history of the plugin's parameter state. JUCE-free so it can be unit tested.
//
// HistoryRecorder is fed the current state on a timer (message thread). It commits a new undo step
// once the state has stopped changing for `settleMs`, so one knob drag or one "Apply all" becomes
// one undo step, whatever the host does with parameter gestures.
#include <vector>
#include <cmath>
#include <cstddef>

namespace polaris
{
struct StateSnapshot
{
    std::vector<float> values;   // normalised (0..1) parameter values, in a fixed order
    int slot = 0;                // active A/B/C/D snapshot slot

    bool sameAs (const StateSnapshot& o, float tolerance = 1.0e-5f) const
    {
        if (slot != o.slot || values.size() != o.values.size()) return false;
        for (size_t i = 0; i < values.size(); ++i)
            if (std::abs (values[i] - o.values[i]) > tolerance) return false;
        return true;
    }
};

class ParamHistory
{
public:
    explicit ParamHistory (size_t limit = 100) : maxSteps (limit < 2 ? 2 : limit) {}

    void reset (const StateSnapshot& s) { steps.assign (1, s); pos = 0; }
    bool empty() const                  { return steps.empty(); }
    size_t size() const                 { return steps.size(); }

    /** Adds a step if it differs from the current one. Drops any redo steps. */
    bool commit (const StateSnapshot& s)
    {
        if (steps.empty()) { reset (s); return true; }
        if (s.sameAs (steps[pos])) return false;
        steps.resize (pos + 1);
        steps.push_back (s);
        if (steps.size() > maxSteps) steps.erase (steps.begin());
        pos = steps.size() - 1;
        return true;
    }

    bool canUndo() const { return ! steps.empty() && pos > 0; }
    bool canRedo() const { return ! steps.empty() && pos + 1 < steps.size(); }

    StateSnapshot undo() { if (canUndo()) --pos; return steps[pos]; }
    StateSnapshot redo() { if (canRedo()) ++pos; return steps[pos]; }
    const StateSnapshot& current() const { return steps[pos]; }

private:
    std::vector<StateSnapshot> steps;
    size_t pos = 0;
    size_t maxSteps;
};

class HistoryRecorder
{
public:
    ParamHistory history;
    double settleMs = 350.0;

    void reset (const StateSnapshot& s, double nowMs)
    {
        history.reset (s);
        lastSeen = s;
        lastChangeMs = nowMs;
    }

    /** Call regularly with the live state. Returns true when a new undo step was committed. */
    bool tick (const StateSnapshot& now, double nowMs)
    {
        if (history.empty()) { reset (now, nowMs); return false; }

        if (! now.sameAs (lastSeen))
        {
            lastSeen = now;              // still moving: wait for it to settle
            lastChangeMs = nowMs;
            return false;
        }
        if (nowMs - lastChangeMs >= settleMs)
            return history.commit (now);
        return false;
    }

    /** After undo/redo/apply has set the parameters to `s`, so it isn't recorded as a new edit. */
    void markApplied (const StateSnapshot& s, double nowMs)
    {
        lastSeen = s;
        lastChangeMs = nowMs;
    }

    /** True while an edit is still settling (not yet committed). */
    bool hasPendingEdit() const { return ! history.empty() && ! lastSeen.sameAs (history.current()); }

    /** Commit a still-settling edit now (call before undo so a quick edit-then-undo reverts that edit). */
    void flushPending() { if (hasPendingEdit()) history.commit (lastSeen); }

private:
    StateSnapshot lastSeen;
    double lastChangeMs = 0.0;
};
} // namespace polaris
