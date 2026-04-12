#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <chrono>
#include <array>

namespace MPF {

struct RecordEvent {
    double timestamp;       // seconds relative to session start (steady_clock)
    const char* category;   // "input", "state", "query" — static string, not owned
    const char* direction;  // "vpx_to_mpf", "mpf_to_vpx" — static string, not owned
    std::string command;
    std::string params;     // pre-serialized JSON
    std::string result;     // pre-serialized JSON
};

class Recorder {
public:
    Recorder();
    ~Recorder();

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }
    void SetOutputDirectory(const std::string& path) { m_outputDir = path; }

    void StartSession();
    void StopSession();

    // Non-blocking. Pushes event to the ring buffer. Drops if buffer is full.
    void Record(RecordEvent event);

    // Returns the current session elapsed time in seconds.
    double Now() const;

    // Exposed for testing — pure function.
    static std::string FormatEvent(const RecordEvent& event,
                                   bool includeWallClock,
                                   const std::string& wallClockAnchor);

private:
    void WriterThread();
    std::string GenerateFilename() const;

    bool m_enabled = false;
    std::string m_outputDir;
    bool m_sessionActive = false;
    std::chrono::steady_clock::time_point m_sessionStart;
    std::string m_wallClockAnchor;

    // SPSC ring buffer
    static constexpr size_t kRingSize = 8192;
    std::array<RecordEvent, kRingSize> m_ring;
    std::atomic<size_t> m_writePos{0};
    std::atomic<size_t> m_readPos{0};

    // Writer thread
    std::thread m_writerThread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_writerRunning{false};
    std::ofstream m_file;
};

} // namespace MPF
