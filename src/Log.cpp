#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "plugins/LoggingPlugin.h"

namespace MPF {

// Defines MPF::LPILog(level, fmt, ...) and MPF::LPISetup(endpointId, msgApi).
// Until LPISetup() is called (e.g. in the test binary, which never loads as a
// VPX plugin), loggingApi is null and LPILog is a silent no-op.
LPI_IMPLEMENT

} // namespace MPF
