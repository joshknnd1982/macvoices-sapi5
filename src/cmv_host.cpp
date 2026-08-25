#include "cmv_host.hpp"

#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "cmv_log.hpp"
#include "cmv_paths.hpp"

namespace cmv {
namespace {

constexpr DWORD kReqMagic = 0x54475233;        // 'TGR3'
constexpr DWORD kReqMagicStream = 0x54475234;  // 'TGR4'
constexpr DWORD kRspMagic = 0x54475253;        // 'TGRS'

#pragma pack(push, 1)
struct Request
{
    DWORD magic;
    INT32 wpm;
    INT32 pitch;
    DWORD reserved;
    DWORD voice_len;
    DWORD text_len;
};
#pragma pack(pop)
static_assert(sizeof(Request) == 24, "request framing must match the host");

// What each phrasing choice tells the engine. Empty means it is told nothing, which is
// Leopard's own model - not the same thing as being told 0. The thresholds were measured
// against the engine; see the NVDA driver for the ladder.
[[nodiscard]] std::wstring phrasing_param(const std::wstring& phrasing)
{
    if (phrasing == L"fewest") return L"Boundaries.SilThreshold=-8";
    if (phrasing == L"fewer") return L"Boundaries.SilThreshold=-4";
    if (phrasing == L"more") return L"Boundaries.SilThreshold=0";
    if (phrasing == L"most") return L"Boundaries.SilThreshold=5";
    return {};  // "leopard": the parameter left unanswered
}

// Builds a child environment block: ours, minus the TIGER_* variables, plus the ones the
// given configuration needs.
[[nodiscard]] std::vector<wchar_t> build_environment(const HostClient::EnvConfig& env,
                                                     const std::wstring& cancel_event_name)
{
    std::vector<std::pair<std::wstring, std::wstring>> vars;

    wchar_t* block = GetEnvironmentStringsW();
    if (block) {
        for (wchar_t* p = block; *p;) {
            std::wstring entry(p);
            p += entry.size() + 1;
            const std::size_t eq = entry.find(L'=', 1);
            if (eq == std::wstring::npos) {
                continue;
            }
            std::wstring name = entry.substr(0, eq);
            if (_wcsnicmp(name.c_str(), L"TIGER_", 6) == 0) {
                continue;
            }
            vars.emplace_back(std::move(name), entry.substr(eq + 1));
        }
        FreeEnvironmentStringsW(block);
    }

    if (!cancel_event_name.empty()) {
        vars.emplace_back(L"TIGER_CANCEL_EVENT", cancel_event_name);
    }
    const std::wstring params = phrasing_param(env.phrasing);
    if (!params.empty()) {
        vars.emplace_back(L"TIGER_PARAMS", params);
    }
    if (!env.expand_abbreviations) {
        vars.emplace_back(L"TIGER_NO_ABBREV", L"1");
    }
    if (log_enabled(LogLevel::Debug)) {
        vars.emplace_back(L"TIGER_HOST_VERBOSE", L"1");
    }

    std::vector<wchar_t> out;
    for (const auto& [name, value] : vars) {
        out.insert(out.end(), name.begin(), name.end());
        out.push_back(L'=');
        out.insert(out.end(), value.begin(), value.end());
        out.push_back(L'\0');
    }
    out.push_back(L'\0');
    return out;
}

void close_handle(HANDLE& h)
{
    if (h) {
        CloseHandle(h);
        h = nullptr;
    }
}

}  // namespace

HostClient::HostClient()
{
    wchar_t name[128];
    static long counter = 0;
    _snwprintf_s(name, _TRUNCATE, L"Local\\ClassicMacVoices-cancel-%lu-%ld",
                 GetCurrentProcessId(), InterlockedIncrement(&counter));
    // Manual reset off, initial state off: the host consumes the signal by waiting on
    // it, and render() clears any stale one before it sends the next request.
    cancel_event_ = CreateEventW(nullptr, FALSE, FALSE, name);
    if (cancel_event_) {
        cancel_event_name_ = name;
    } else {
        CMV_LOG_W("no cancel event; interruptions will wait for the render to finish");
    }
}

HostClient::~HostClient()
{
    shutdown();
    close_handle(cancel_event_);
}

void HostClient::request_cancel()
{
    if (cancel_event_) {
        SetEvent(cancel_event_);
    }
}

void HostClient::shutdown()
{
    std::lock_guard<std::mutex> lock(mutex_);
    kill_host_locked(true);
}

void HostClient::kill_host_locked(bool graceful)
{
    if (proc_.hProcess) {
        // Closing stdin is the polite request: the host's serve loop exits at EOF.
        close_handle(child_stdin_);
        if (graceful) {
            WaitForSingleObject(proc_.hProcess, 1000);
        }
        DWORD code = 0;
        if (GetExitCodeProcess(proc_.hProcess, &code) && code == STILL_ACTIVE) {
            TerminateProcess(proc_.hProcess, 1);
            WaitForSingleObject(proc_.hProcess, 2000);
        }
        CMV_LOG_I("host %lu stopped", proc_.dwProcessId);
    }
    close_handle(child_stdin_);
    close_handle(child_stdout_);
    // The stderr pump thread owns child_stderr_ and closes it at EOF.
    child_stderr_ = nullptr;
    close_handle(proc_.hThread);
    close_handle(proc_.hProcess);
    proc_ = {};
    close_handle(job_);
}

void HostClient::start_stderr_pump()
{
    // A pipe nobody reads fills up, and then the host blocks inside a printf and the
    // screen reader goes quiet. The thread also earns the only diagnosis anyone will
    // ever get from a machine we do not have.
    HANDLE err = child_stderr_;
    if (!err) {
        return;
    }
    std::thread([err]() {
        std::string line;
        char buf[512];
        DWORD got = 0;
        while (ReadFile(err, buf, sizeof(buf), &got, nullptr) && got > 0) {
            for (DWORD i = 0; i < got; ++i) {
                const char c = buf[i];
                if (c == '\n') {
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                        line.pop_back();
                    }
                    if (!line.empty()) {
                        CMV_LOG_D("host: %s", line.c_str());
                    }
                    line.clear();
                } else {
                    line += c;
                }
            }
        }
        if (!line.empty()) {
            CMV_LOG_D("host: %s", line.c_str());
        }
        CloseHandle(err);
    }).detach();
}

bool HostClient::ensure_host(const EnvConfig& env)
{
    if (proc_.hProcess) {
        DWORD code = 0;
        const bool alive =
            GetExitCodeProcess(proc_.hProcess, &code) && code == STILL_ACTIVE;
        if (alive && running_env_ == env) {
            return true;
        }
        if (alive) {
            CMV_LOG_I("engine settings changed; restarting the host");
        } else {
            CMV_LOG_W("host exited with code %lu; restarting", code);
        }
        kill_host_locked(alive);
    }

    const std::wstring& host = host_exe_path();
    const std::wstring mt = macintalk_path();
    const std::wstring sd = speechdict_path();
    const std::wstring voices = voices_dir();
    if (host.empty() || !file_exists(host)) {
        CMV_LOG_E("panthera_host.exe was not found; nothing can speak");
        return false;
    }
    if (!file_exists(mt) || !file_exists(sd) || !dir_exists(voices)) {
        CMV_LOG_E("the engine tree is incomplete under %s",
                  log_narrow(tree_root().c_str()).c_str());
        return false;
    }

    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE stdin_read = nullptr, stdin_write = nullptr;
    HANDLE stdout_read = nullptr, stdout_write = nullptr;
    HANDLE stderr_read = nullptr, stderr_write = nullptr;
    if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0) ||
        !CreatePipe(&stdout_read, &stdout_write, &sa, 0) ||
        !CreatePipe(&stderr_read, &stderr_write, &sa, 0)) {
        CMV_LOG_E("could not create pipes for the host");
        close_handle(stdin_read); close_handle(stdin_write);
        close_handle(stdout_read); close_handle(stdout_write);
        close_handle(stderr_read); close_handle(stderr_write);
        return false;
    }
    // Only the child's ends may be inherited, or the pipes never signal EOF.
    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = L"\"" + host + L"\" --serve \"" + mt + L"\" \"" + sd + L"\" \"" +
                       voices + L"\"";
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');
    std::vector<wchar_t> env_block = build_environment(env, cancel_event_name_);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = stdin_read;
    si.hStdOutput = stdout_write;
    si.hStdError = stderr_write;

    PROCESS_INFORMATION pi = {};
    const BOOL ok = CreateProcessW(
        host.c_str(), cmd_buf.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, env_block.data(), nullptr, &si, &pi);

    close_handle(stdin_read);
    close_handle(stdout_write);
    close_handle(stderr_write);

    if (!ok) {
        CMV_LOG_E("could not start %s (error %lu)", log_narrow(host.c_str()).c_str(),
                  GetLastError());
        close_handle(stdin_write);
        close_handle(stdout_read);
        close_handle(stderr_read);
        return false;
    }

    proc_ = pi;
    child_stdin_ = stdin_write;
    child_stdout_ = stdout_read;
    child_stderr_ = stderr_read;
    running_env_ = env;
    start_stderr_pump();

    // If this process dies with an utterance in flight, the job takes the host with it
    // rather than leaving an orphan holding 700 MB of voice bank.
    job_ = CreateJobObjectW(nullptr, nullptr);
    if (job_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits,
                                sizeof(limits));
        if (!AssignProcessToJobObject(job_, proc_.hProcess)) {
            close_handle(job_);
        }
    }

    CMV_LOG_I("host %lu started: phrasing=%s abbreviations=%s", proc_.dwProcessId,
              log_narrow(env.phrasing.c_str()).c_str(),
              env.expand_abbreviations ? "on" : "OFF");
    return true;
}

bool HostClient::write_all(const void* data, DWORD size)
{
    const char* p = static_cast<const char*>(data);
    while (size > 0) {
        DWORD written = 0;
        if (!WriteFile(child_stdin_, p, size, &written, nullptr) || written == 0) {
            return false;
        }
        p += written;
        size -= written;
    }
    return true;
}

bool HostClient::read_all(void* data, DWORD size)
{
    char* p = static_cast<char*>(data);
    while (size > 0) {
        DWORD got = 0;
        if (!ReadFile(child_stdout_, p, size, &got, nullptr) || got == 0) {
            return false;
        }
        p += got;
        size -= got;
    }
    return true;
}

bool HostClient::render(const std::string& voice_utf8, const std::string& text_macroman,
                        int wpm, int pitch_tenths, const EnvConfig& env,
                        const ChunkSink& sink)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!ensure_host(env)) {
            return false;
        }

        // A cancel that arrived while nothing was rendering must not be waiting here to
        // kill the utterance that follows it.
        if (cancel_event_) {
            ResetEvent(cancel_event_);
        }

        Request req = {};
        req.magic = streaming_ ? kReqMagicStream : kReqMagic;
        req.wpm = wpm;
        req.pitch = pitch_tenths;
        req.reserved = 0;
        req.voice_len = static_cast<DWORD>(voice_utf8.size());
        req.text_len = static_cast<DWORD>(text_macroman.size());

        const bool sent = write_all(&req, sizeof(req)) &&
                          write_all(voice_utf8.data(), req.voice_len) &&
                          write_all(text_macroman.data(), req.text_len);
        if (!sent) {
            CMV_LOG_W("could not write to the host; restarting it");
            kill_host_locked(false);
            continue;
        }

        if (streaming_) {
            struct { DWORD magic; INT32 status; } head = {};
            if (!read_all(&head, sizeof(head))) {
                // A host that does not know 'TGR4' exits rather than answer it. Fall
                // back to whole-utterance responses for the rest of this process.
                CMV_LOG_W("the host did not answer a streamed request; falling back to "
                          "whole-utterance audio");
                streaming_ = false;
                kill_host_locked(false);
                continue;
            }
            if (head.magic != kRspMagic) {
                CMV_LOG_E("bad response magic %08lX; restarting the host", head.magic);
                kill_host_locked(false);
                continue;
            }
            if (head.status != 0) {
                CMV_LOG_W("engine OSErr %ld for this utterance",
                          static_cast<long>(head.status));
            }

            bool feeding = true;
            std::vector<char> chunk;
            for (;;) {
                DWORD frames = 0;
                if (!read_all(&frames, sizeof(frames))) {
                    CMV_LOG_E("the host stopped mid-stream; restarting it");
                    kill_host_locked(false);
                    return false;
                }
                if (frames == 0) {
                    break;
                }
                chunk.resize(static_cast<std::size_t>(frames) * 2);
                if (!read_all(chunk.data(), frames * 2)) {
                    CMV_LOG_E("the host stopped mid-chunk; restarting it");
                    kill_host_locked(false);
                    return false;
                }
                // Read to the end of the response even once the sink has stopped
                // wanting it: the same pipe carries the next utterance, and chunks left
                // unread put the protocol out of step.
                if (feeding && sink && !sink(chunk.data(), frames * 2)) {
                    feeding = false;
                }
            }
            return true;
        }

        struct { DWORD magic; INT32 status; DWORD frames; } head = {};
        if (!read_all(&head, sizeof(head))) {
            CMV_LOG_W("no answer from the host; restarting it");
            kill_host_locked(false);
            continue;
        }
        if (head.magic != kRspMagic) {
            CMV_LOG_E("bad response magic %08lX; restarting the host", head.magic);
            kill_host_locked(false);
            continue;
        }
        if (head.status != 0) {
            CMV_LOG_W("engine OSErr %ld for this utterance",
                      static_cast<long>(head.status));
        }
        std::vector<char> pcm(static_cast<std::size_t>(head.frames) * 2);
        if (!pcm.empty() && !read_all(pcm.data(), static_cast<DWORD>(pcm.size()))) {
            CMV_LOG_E("the host stopped mid-response; restarting it");
            kill_host_locked(false);
            return false;
        }
        if (sink && !pcm.empty()) {
            sink(pcm.data(), static_cast<DWORD>(pcm.size()));
        }
        return true;
    }

    CMV_LOG_E("giving up on this utterance after two host starts");
    return false;
}

HostClient& shared_host()
{
    static HostClient client;
    return client;
}

}  // namespace cmv
