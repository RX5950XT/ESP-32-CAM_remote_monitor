#pragma once

#include <Arduino.h>
#include "config.h"

struct SchedulerState {
    bool monitorEnabled = false;
    bool aiEnabled = false;
    int monitorIntervalSec = DEFAULT_MONITOR_INTERVAL_SEC;
    int aiIntervalSec = DEFAULT_AI_INTERVAL_SEC;
    unsigned long lastMonitorTime = 0;
    unsigned long lastAITime = 0;
    bool monitorRunning = false;
    bool aiRunning = false;
};

// Returns true if due (does not claim; use mark*Running / mark*Done).
bool shouldRunMonitor(const SchedulerState& state);
bool shouldRunAI(const SchedulerState& state);

// Claim slot so loop will not re-enter until mark*Done.
void markMonitorRunning(SchedulerState& state);
void markAIRunning(SchedulerState& state);

// Call after work finishes: next interval starts from completion time.
void markMonitorDone(SchedulerState& state);
void markAIDone(SchedulerState& state);
