#pragma once

struct SchedulerState;

void mcpInit();
bool mcpEnabled();
bool mcpMdnsReady();
// Runs on the main loop so MCP, Telegram and AI never use the camera concurrently.
// Returns true when a request was handled; skip other work until the next loop.
bool handleMcpClient(SchedulerState& state);
