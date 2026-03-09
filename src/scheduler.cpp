#include "scheduler.h"

bool shouldRunMonitor(SchedulerState& state) {
    if (!state.monitorEnabled) return false;

    unsigned long now = millis();
    unsigned long intervalMs = (unsigned long)state.monitorIntervalSec * 1000UL;

    if (now - state.lastMonitorTime >= intervalMs) {
        state.lastMonitorTime = now;
        return true;
    }
    return false;
}

bool shouldRunAI(SchedulerState& state) {
    if (!state.aiEnabled) return false;

    unsigned long now = millis();
    unsigned long intervalMs = (unsigned long)state.aiIntervalSec * 1000UL;

    if (now - state.lastAITime >= intervalMs) {
        state.lastAITime = now;
        return true;
    }
    return false;
}
