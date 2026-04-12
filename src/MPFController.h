#pragma once

#include "BCPClient.h"
#include "Recorder.h"
#include "plugins/ScriptablePlugin.h"

#include <string>
#include <map>

namespace MPF {

class MPFController {
    PSC_IMPLEMENT_REFCOUNT()

public:
    MPFController(bool recordingEnabled, const std::string& recordingPath);
    ~MPFController();

    MPFController(const MPFController&) = delete;
    MPFController& operator=(const MPFController&) = delete;

    // --- Lifecycle ---
    void Run();
    void Run(const std::string& addr);
    void Run(const std::string& addr, int port);
    void Stop();

    // --- Switch access ---
    bool GetSwitch(int number);
    bool GetSwitch(const std::string& number);
    void SetSwitch(int number, bool value);
    void SetSwitch(const std::string& number, bool value);
    void PulseSW(int number);
    void PulseSW(const std::string& number);

    // --- Mech access ---
    int ReadMech(int number);
    void SetMech(int number, int value);
    int GetMech(int number);

    // --- Polled state ---
    std::string GetChangedSolenoids();
    std::string GetChangedLamps();
    std::string GetChangedGIStrings();
    std::string GetChangedLEDs();
    std::string GetChangedBrightnessLEDs();
    std::string GetChangedFlashers();
    std::string GetHardwareRules();
    bool IsCoilActive(int number);
    bool IsCoilActive(const std::string& number);

    // --- Stub properties (local state, no BCP) ---
    std::string GetVersion() const { return "1.0.0"; }
    std::string GetGameName() const { return m_gameName; }
    void SetGameName(const std::string& name) { m_gameName = name; }
    bool GetShowTitle() const { return m_showTitle; }
    void SetShowTitle(bool v) { m_showTitle = v; }
    bool GetShowFrame() const { return false; }
    void SetShowFrame(bool) {}
    bool GetShowDMDOnly() const { return false; }
    void SetShowDMDOnly(bool) {}
    bool GetHandleMechanics() const { return m_handleMechanics; }
    void SetHandleMechanics(bool v) { m_handleMechanics = v; }
    bool GetHandleKeyboard() const { return false; }
    void SetHandleKeyboard(bool) {}
    bool GetDIP() const { return false; }
    void SetDIP(bool) {}
    bool GetPause() const { return m_pause; }
    void SetPause(bool v) { m_pause = v; }
    std::string GetSplashInfoLine() const { return m_splashInfoLine; }
    void SetSplashInfoLine(const std::string& v) { m_splashInfoLine = v; }

private:
    // Dispatch a BCP command and return the "result" param from the response.
    std::string DispatchToMPF(const char* category,
                              const std::string& subcommand,
                              const std::map<std::string, std::string>& extraParams = {});
    static std::string ParamsToJson(const std::map<std::string, std::string>& params);

    BCPClient m_bcp;
    Recorder m_recorder;

    std::string m_gameName = "Game";
    bool m_showTitle = false;
    bool m_handleMechanics = true;
    bool m_pause = false;
    std::string m_splashInfoLine;
};

} // namespace MPF
