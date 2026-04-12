#include "Recorder.h"

#include <filesystem>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace MPF {

// ---------------------------------------------------------------------------
// JSON escaping helper
// ---------------------------------------------------------------------------

static void JsonEscapeAppend(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

// ---------------------------------------------------------------------------
// Recorder
// ---------------------------------------------------------------------------

Recorder::Recorder() = default;

Recorder::~Recorder() {
    StopSession();
}

double Recorder::Now() const {
    if (!m_sessionActive) return 0.0;
    auto elapsed = std::chrono::steady_clock::now() - m_sessionStart;
    return std::chrono::duration<double>(elapsed).count();
}

std::string Recorder::GenerateFilename() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &time_t_now);
#else
    localtime_r(&time_t_now, &local_tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d_%02d-%02d-%02d_mpf_recording.jsonl",
                  local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday,
                  local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
    return std::string(buf);
}

void Recorder::StartSession() {
    if (!m_enabled || m_sessionActive) return;

    // Create output directory if needed
    if (!m_outputDir.empty()) {
        std::filesystem::create_directories(m_outputDir);
    }

    // Generate wall-clock anchor as ISO 8601 UTC with microsecond precision
    {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()) % 1000000;
        std::tm utc_tm{};
#ifdef _WIN32
        gmtime_s(&utc_tm, &time_t_now);
#else
        gmtime_r(&time_t_now, &utc_tm);
#endif
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%06ldZ",
                      utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday,
                      utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec,
                      static_cast<long>(micros.count()));
        m_wallClockAnchor = buf;
    }

    // Open file
    std::string filename = GenerateFilename();
    std::string filepath = m_outputDir.empty() ? filename : m_outputDir + "/" + filename;
    m_file.open(filepath, std::ios::out | std::ios::trunc);

    // Reset ring buffer
    m_writePos.store(0, std::memory_order_relaxed);
    m_readPos.store(0, std::memory_order_relaxed);

    // Mark session active and record start time
    m_sessionStart = std::chrono::steady_clock::now();
    m_sessionActive = true;

    // Start writer thread
    m_writerRunning.store(true, std::memory_order_release);
    m_writerThread = std::thread(&Recorder::WriterThread, this);
}

void Recorder::StopSession() {
    if (!m_sessionActive) return;
    m_sessionActive = false;

    // Signal writer to stop
    m_writerRunning.store(false, std::memory_order_release);
    m_cv.notify_one();

    // Join writer thread
    if (m_writerThread.joinable()) {
        m_writerThread.join();
    }

    // Close file
    if (m_file.is_open()) {
        m_file.close();
    }
}

void Recorder::Record(RecordEvent event) {
    if (!m_sessionActive) return;

    size_t current = m_writePos.load(std::memory_order_relaxed);
    size_t next = (current + 1) % kRingSize;

    // Check if buffer is full
    if (next == m_readPos.load(std::memory_order_acquire)) {
        return; // Drop event
    }

    // Store event
    m_ring[current] = std::move(event);
    m_writePos.store(next, std::memory_order_release);

    // Notify writer thread
    m_cv.notify_one();
}

std::string Recorder::FormatEvent(const RecordEvent& event,
                                   bool includeWallClock,
                                   const std::string& wallClockAnchor) {
    std::string out;
    out.reserve(256);

    char tsBuf[32];
    std::snprintf(tsBuf, sizeof(tsBuf), "%.6f", event.timestamp);

    out += "{\"ts\":";
    out += tsBuf;

    if (includeWallClock && !wallClockAnchor.empty()) {
        out += ",\"wall\":\"";
        out += wallClockAnchor;
        out += '"';
    }

    out += ",\"cat\":\"";
    if (event.category) out += event.category;
    out += "\",\"dir\":\"";
    if (event.direction) out += event.direction;
    out += "\",\"cmd\":";
    JsonEscapeAppend(out, event.command);

    if (!event.params.empty()) {
        out += ",\"params\":";
        out += event.params;
    }

    if (!event.result.empty()) {
        out += ",\"result\":";
        out += event.result;
    }

    out += '}';
    return out;
}

void Recorder::WriterThread() {
    bool isFirstEvent = true;

    while (true) {
        // Wait for events or stop signal
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                return !m_writerRunning.load(std::memory_order_acquire) ||
                       m_readPos.load(std::memory_order_acquire) !=
                       m_writePos.load(std::memory_order_acquire);
            });
        }

        // Drain all events from ring buffer
        bool wroteAny = false;
        while (true) {
            size_t rp = m_readPos.load(std::memory_order_relaxed);
            size_t wp = m_writePos.load(std::memory_order_acquire);
            if (rp == wp) break;

            const RecordEvent& event = m_ring[rp];
            std::string line = FormatEvent(event, isFirstEvent, m_wallClockAnchor);
            isFirstEvent = false;

            if (m_file.is_open()) {
                m_file << line << '\n';
            }

            m_readPos.store((rp + 1) % kRingSize, std::memory_order_release);
            wroteAny = true;
        }

        if (wroteAny && m_file.is_open()) {
            m_file.flush();
        }

        // Exit when stopped and buffer is drained
        if (!m_writerRunning.load(std::memory_order_acquire)) {
            // One final drain pass
            while (true) {
                size_t rp = m_readPos.load(std::memory_order_relaxed);
                size_t wp = m_writePos.load(std::memory_order_acquire);
                if (rp == wp) break;

                const RecordEvent& event = m_ring[rp];
                std::string line = FormatEvent(event, isFirstEvent, m_wallClockAnchor);
                isFirstEvent = false;

                if (m_file.is_open()) {
                    m_file << line << '\n';
                }

                m_readPos.store((rp + 1) % kRingSize, std::memory_order_release);
            }
            if (m_file.is_open()) {
                m_file.flush();
            }
            break;
        }
    }
}

} // namespace MPF
