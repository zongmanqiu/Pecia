// UiBridge.cpp - global bridge holder. The concrete implementation is
// provided by the UI layer (ui/UiBridge.cpp) and installed at startup.
#include "core/UiBridge.h"

#include <atomic>

namespace {
// Core/worker threads read the bridge while the UI thread installs or
// replaces it at startup/teardown; a plain pointer would be a data race.
std::atomic<UiBridge *> g_bridge{nullptr};
}

UiBridge *uiBridge() { return g_bridge.load(); }
void setUiBridge(UiBridge *bridge) { g_bridge.store(bridge); }
