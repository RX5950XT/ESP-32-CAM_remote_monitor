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
};

// Returns true if it's time to run monitor capture
bool shouldRunMonitor(SchedulerState& state);

// Returns true if it's time to run AI detection
bool shouldRunAI(SchedulerState& state);
