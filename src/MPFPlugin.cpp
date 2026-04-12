#include "plugins/MsgPlugin.h"
#include "plugins/ScriptablePlugin.h"

MSGPI_EXPORT void MSGPIAPI MPFPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api)
{
    (void)sessionId;
    (void)api;
}

MSGPI_EXPORT void MSGPIAPI MPFPluginUnload()
{
}
