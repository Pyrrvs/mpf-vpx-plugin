#include "MPFController.h"
#include "ChangedItems.h"
#include "Log.h"

#include "plugins/MsgPlugin.h"
#include "plugins/ScriptablePlugin.h"

#include <cassert>
#include <cstdarg>
#include <string>

using std::string;

namespace MPF {

// Aliases so PSC_CLASS_START macros generate casts to the real types
using MPF_Controller = MPFController;
using MPF_ChangedItems = ChangedItems;
using MPF_HardwareRuleItems = HardwareRuleItems;

// PSC_VAR_SET / PSC_VAR accessors for object return types used in PSC_PROP_R
#define PSC_VAR_SET_MPF_ChangedItems(variant, value) (variant).vObject = static_cast<void*>(value)
#define PSC_VAR_SET_MPF_HardwareRuleItems(variant, value) (variant).vObject = static_cast<void*>(value)
#define PSC_VAR_MPF_ChangedItems(variant) static_cast<MPF_ChangedItems*>((variant).vObject)
#define PSC_VAR_MPF_HardwareRuleItems(variant) static_cast<MPF_HardwareRuleItems*>((variant).vObject)

// Scriptable class definitions for return types (must come before MPF_Controller)
PSC_CLASS_START(MPF_ChangedItems)
    PSC_PROP_R(MPF_ChangedItems, int, Count)
    PSC_PROP_R_ARRAY1(MPF_ChangedItems, string, Id, int)
    PSC_PROP_R_ARRAY1(MPF_ChangedItems, bool, State, int)
    PSC_PROP_R_ARRAY1(MPF_ChangedItems, float, Brightness, int)
PSC_CLASS_END(MPF_ChangedItems)

PSC_CLASS_START(MPF_HardwareRuleItems)
    PSC_PROP_R(MPF_HardwareRuleItems, int, Count)
    PSC_PROP_R_ARRAY1(MPF_HardwareRuleItems, string, Switch, int)
    PSC_PROP_R_ARRAY1(MPF_HardwareRuleItems, string, Coil, int)
    PSC_PROP_R_ARRAY1(MPF_HardwareRuleItems, bool, Hold, int)
PSC_CLASS_END(MPF_HardwareRuleItems)

// Scriptable class definition
PSC_CLASS_START(MPF_Controller)
    // Lifecycle
    PSC_FUNCTION0(MPF_Controller, void, Run)
    PSC_FUNCTION1(MPF_Controller, void, Run, string)
    PSC_FUNCTION2(MPF_Controller, void, Run, string, int)
    PSC_FUNCTION0(MPF_Controller, void, Stop)

    // Switch access — string-only registration. VBScript coerces int args to string
    // automatically. We can't register both int and string overloads because VPX's
    // DISPATCH_PROPERTYGET path doesn't disambiguate on arg types (always picks first match).
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
    PSC_PROP_R(MPF_Controller, MPF_ChangedItems, ChangedSolenoids)
    PSC_PROP_R(MPF_Controller, MPF_ChangedItems, ChangedLamps)
    PSC_PROP_R(MPF_Controller, MPF_ChangedItems, ChangedGIStrings)
    PSC_PROP_R(MPF_Controller, MPF_ChangedItems, ChangedLEDs)
    PSC_PROP_R(MPF_Controller, MPF_ChangedItems, ChangedBrightnessLEDs)
    PSC_PROP_R(MPF_Controller, MPF_ChangedItems, ChangedFlashers)
    PSC_PROP_R(MPF_Controller, MPF_HardwareRuleItems, HardwareRules)
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

    LPISetup(sessionId, const_cast<MsgPluginAPI*>(api));
    MPF_LOGI("MPF plugin loading");

    getScriptApiMsgId = msgApi->GetMsgID(SCRIPTPI_NAMESPACE, SCRIPTPI_MSG_GET_API);
    msgApi->BroadcastMsg(sessionId, getScriptApiMsgId, &scriptApi);

    auto regLambda = [](ScriptClassDef* scd) { scriptApi->RegisterScriptClass(scd); };
    RegisterMPF_ChangedItemsSCD(regLambda);
    RegisterMPF_HardwareRuleItemsSCD(regLambda);
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
    UnregisterMPF_HardwareRuleItemsSCD(unregLambda);
    UnregisterMPF_ChangedItemsSCD(unregLambda);

    msgApi->ReleaseMsgID(getScriptApiMsgId);

    controller = nullptr;
    scriptApi = nullptr;
    msgApi = nullptr;
}
