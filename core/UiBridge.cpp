// UiBridge.cpp - global bridge holder. The concrete implementation is
// provided by the UI layer (ui/UiBridge.cpp) and installed at startup.
#include "core/UiBridge.h"

namespace {
UiBridge *g_bridge = nullptr;
}

UiBridge *uiBridge() { return g_bridge; }
void setUiBridge(UiBridge *bridge) { g_bridge = bridge; }
