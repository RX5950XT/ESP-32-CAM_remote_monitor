#include "scheduler.h"

static unsigned long intervalMsFromSec(int sec) {
    if (sec < 0) sec = 0;
    // Cap to avoid 32-bit overflow on sec * 1000 (ESP32 unsigned long is 32-bit).
    const unsigned long maxSec = 4294967UL;  // floor((2^32-1)/1000)
    unsigned long s = (unsigned long)sec;
    if (s > maxSec) s = maxSec;
    return s * 1000UL;
}

bool shouldRunMonitor(const SchedulerState& state) {
    if (!state.monitorEnabled || state.monitorRunning) return false;
    unsigned long now = millis();
    return (now - state.lastMonitorTime) >= intervalMsFromSec(state.monitorIntervalSec);
}

bool shouldRunAI(const SchedulerState& state) {
    if (!state.aiEnabled || state.aiRunning) return false;
    unsigned long now = millis();
    return (now - state.lastAITime) >= intervalMsFromSec(state.aiIntervalSec);
}

void markMonitorRunning(SchedulerState& state) {
    state.monitorRunning = true;
}

void markAIRunning(SchedulerState& state) {
    state.aiRunning = true;
}

void markMonitorDone(SchedulerState& state) {
    state.lastMonitorTime = millis();
    state.monitorRunning = false;
}

void markAIDone(SchedulerState& state) {
    state.lastAITime = millis();
    state.aiRunning = false;
}
