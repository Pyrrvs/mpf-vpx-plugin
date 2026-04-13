#pragma once

// Thin wrapper over VPX's shared LoggingPlugin API. Every source file that
// wants to log includes this header and uses MPF_LOGD / MPF_LOGI / MPF_LOGW /
// MPF_LOGE. The actual symbol (LPILog) is defined once via LPI_IMPLEMENT in
// Log.cpp, and wired up to VPX at plugin load via LPISetup().
//
// When the plugin runs outside VPX (e.g. the test binary), LPISetup() is never
// called, loggingApi stays null, and LPILog is a no-op — so tests stay quiet.

#include "plugins/LoggingPlugin.h"

struct MsgPluginAPI;

namespace MPF {
LPI_USE();
void LPISetup(unsigned int endpointId, MsgPluginAPI* msgApi);
} // namespace MPF

#define MPF_LOGD(...) MPF::LPILog(LPI_LVL_DEBUG, __VA_ARGS__)
#define MPF_LOGI(...) MPF::LPILog(LPI_LVL_INFO,  __VA_ARGS__)
#define MPF_LOGW(...) MPF::LPILog(LPI_LVL_WARN,  __VA_ARGS__)
#define MPF_LOGE(...) MPF::LPILog(LPI_LVL_ERROR, __VA_ARGS__)
