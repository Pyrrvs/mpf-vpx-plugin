#include "MPFController.h"

#include "plugins/MsgPlugin.h"
#include "plugins/ScriptablePlugin.h"

#include <cassert>
#include <cstdarg>
#include <string>

using std::string;

namespace MPF {

// Alias so PSC_CLASS_START(MPF_Controller) generates casts to MPFController
using MPF_Controller = MPFController;

// Scriptable class definition
PSC_CLASS_START(MPF_Controller)
    // Lifecycle
    PSC_FUNCTION0(MPF_Controller, void, Run)
    PSC_FUNCTION1(MPF_Controller, void, Run, string)
    PSC_FUNCTION2(MPF_Controller, void, Run, string, int)
    PSC_FUNCTION0(MPF_Controller, void, Stop)

    // Switch access (int-indexed)
    PSC_PROP_RW_ARRAY1(MPF_Controller, bool, Switch, int)
    PSC_FUNCTION1(MPF_Controller, void, PulseSW, int)

    // Switch access (string-indexed)
    PSC_PROP_RW_ARRAY1(MPF_Controller, bool, Switch, string)
    PSC_FUNCTION1(MPF_Controller, void, PulseSW, string)

    // Mech write (int-indexed)
    PSC_PROP_W_ARRAY1(MPF_Controller, int, Mech, int)

    // Mech read -> ReadMech(int) which uses BCP subcommand "mech"
    members.push_back( { { "Mech" }, { "int" }, 1, { { "int" } },
        [](void* me, int, ScriptVariant* pArgs, ScriptVariant* pRet) {
            pRet->vInt = static_cast<MPF_Controller*>(me)->ReadMech(pArgs[0].vInt);
        } });
    // GetMech read -> GetMech(int) which uses BCP subcommand "get_mech"
    members.push_back( { { "GetMech" }, { "int" }, 1, { { "int" } },
        [](void* me, int, ScriptVariant* pArgs, ScriptVariant* pRet) {
            pRet->vInt = static_cast<MPF_Controller*>(me)->GetMech(pArgs[0].vInt);
        } });

    // Polled state
    PSC_PROP_R(MPF_Controller, string, ChangedSolenoids)
    PSC_PROP_R(MPF_Controller, string, ChangedLamps)
    PSC_PROP_R(MPF_Controller, string, ChangedGIStrings)
    PSC_PROP_R(MPF_Controller, string, ChangedLEDs)
    PSC_PROP_R(MPF_Controller, string, ChangedBrightnessLEDs)
    PSC_PROP_R(MPF_Controller, string, ChangedFlashers)
    PSC_PROP_R(MPF_Controller, string, HardwareRules)
    PSC_FUNCTION1(MPF_Controller, bool, IsCoilActive, int)
    PSC_FUNCTION1(MPF_Controller, bool, IsCoilActive, string)

    // Stub properties
    PSC_PROP_R(MPF_Controller, string, Version)
    PSC_PROP_RW(MPF_Controller, string, GameName)
    PSC_PROP_RW(MPF_Controller, bool, ShowTitle)
    PSC_PROP_RW(MPF_Controller, bool, ShowFrame)
    PSC_PROP_RW(MPF_Controller, bool, ShowDMDOnly)
    PSC_PROP_RW(MPF_Controller, bool, HandleMechanics)
    PSC_PROP_RW(MPF_Controller, bool, HandleKeyboard)
    PSC_PROP_RW(MPF_Controller, bool, DIP)
    PSC_PROP_RW(MPF_Controller, bool, Pause)
    PSC_PROP_RW(MPF_Controller, string, SplashInfoLine)
PSC_CLASS_END(MPF_Controller)

// Plugin state
static const MsgPluginAPI* msgApi = nullptr;
static ScriptablePluginAPI* scriptApi = nullptr;
static unsigned int getScriptApiMsgId = 0;
static MPFController* controller = nullptr;

PSC_ERROR_IMPLEMENT(scriptApi);

static bool GetSettingBool(const MsgPluginAPI* api, const char* section, const char* key, bool def)
{
    char buf[64];
    api->GetSetting(section, key, buf, sizeof(buf));
    if (buf[0] == '\0') return def;
    return (buf[0] == '1' || buf[0] == 't' || buf[0] == 'T');
}

static string GetSettingString(const MsgPluginAPI* api, const char* section, const char* key, const string& def = string())
{
    char buf[1024];
    api->GetSetting(section, key, buf, sizeof(buf));
    return buf[0] ? string(buf) : def;
}

} // namespace MPF

using namespace MPF;

MSGPI_EXPORT void MSGPIAPI MPFPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api)
{
    msgApi = api;

    getScriptApiMsgId = msgApi->GetMsgID(SCRIPTPI_NAMESPACE, SCRIPTPI_MSG_GET_API);
    msgApi->BroadcastMsg(sessionId, getScriptApiMsgId, &scriptApi);

    auto regLambda = [](ScriptClassDef* scd) { scriptApi->RegisterScriptClass(scd); };
    RegisterMPF_ControllerSCD(regLambda);

    MPF_Controller_SCD->CreateObject = []() -> void*
    {
        assert(controller == nullptr);

        bool enableRecording = GetSettingBool(msgApi, "MPF", "EnableRecording", false);
        string recordingPath = GetSettingString(msgApi, "MPF", "RecordingPath");

        controller = new MPFController(enableRecording, recordingPath);
        return static_cast<void*>(controller);
    };

    scriptApi->SubmitTypeLibrary();
    scriptApi->SetCOMObjectOverride("MPF.Controller", MPF_Controller_SCD);
}

MSGPI_EXPORT void MSGPIAPI MPFPluginUnload()
{
    scriptApi->SetCOMObjectOverride("MPF.Controller", nullptr);

    auto unregLambda = [](ScriptClassDef* scd) { scriptApi->RegisterScriptClass(scd); };
    UnregisterMPF_ControllerSCD(unregLambda);

    msgApi->ReleaseMsgID(getScriptApiMsgId);

    controller = nullptr;
    scriptApi = nullptr;
    msgApi = nullptr;
}
