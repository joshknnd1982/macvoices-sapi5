// The engine process, and the framed-stdio protocol that drives it.
//
// panthera_host.exe is a 32-bit process that maps Apple's i386 MacinTalk and
// SpeechDictionary into itself and calls SESpeakBuffer directly. Keeping it in its own
// process is exactly what lets one binary serve both the 32-bit and the 64-bit SAPI DLL:
// a 64-bit process cannot load i386 code, but it can perfectly well talk to a 32-bit
// child over a pipe.
//
// One resident host serves the whole process - the request names the voice, so every
// voice goes through the same child - and it stays warm between utterances, because a
// replacement means reloading Alex's 670 MB bank.
//
// The protocol (see the NVDA driver, pantheradriver.py, which this matches byte for
// byte):
//
//   request   <IiiIII  magic 'TGR3' (one response) or 'TGR4' (streamed), wpm,
//             pitch offset in tenths of a semitone, reserved 0, voice name length,
//             text length; then the voice name (UTF-8) and the text (MacRoman).
//   'TGR3'    <IiI     magic 'TGRS', OSErr, frame count; then frames of 16-bit mono
//             PCM at 22050 Hz.
//   'TGR4'    <Ii      magic 'TGRS', OSErr; then <I frame-count chunks, a zero count
//             ending the utterance.
//
// Cancellation is a named auto-reset event the host inherits through the environment
// (TIGER_CANCEL_EVENT) and polls every 10 ms mid-render. Signalling it ends the render
// in tens of milliseconds; the response still completes, so the pipe never goes out of
// step and the host never has to be killed for an ordinary interruption.

#pragma once

#include <windows.h>

#include <functional>
#include <mutex>
#include <string>

namespace cmv {

inline constexpr DWORD kOutRate = 22050;

class HostClient
{
public:
    // Engine settings the host reads from its environment at startup. Changing either
    // needs a fresh process, which ensure_host arranges between utterances.
    struct EnvConfig
    {
        std::wstring phrasing = L"fewest";
        bool expand_abbreviations = true;

        [[nodiscard]] bool operator==(const EnvConfig& o) const
        {
            return phrasing == o.phrasing &&
                   expand_abbreviations == o.expand_abbreviations;
        }
    };

    // Receives each chunk of audio as it arrives. Returning false stops the caller
    // wanting more; the response is still drained so the pipe stays in step.
    using ChunkSink = std::function<bool(const void* pcm, DWORD bytes)>;

    HostClient();
    ~HostClient();

    HostClient(const HostClient&) = delete;
    HostClient& operator=(const HostClient&) = delete;

    // Renders one utterance, feeding audio to the sink. Returns false only on a
    // transport failure (the host died or answered nonsense); an interrupted render
    // still returns true, with however much audio was made.
    bool render(const std::string& voice_utf8, const std::string& text_macroman, int wpm,
                int pitch_tenths, const EnvConfig& env, const ChunkSink& sink);

    // Asks the host to give up on what it is rendering. Safe from any thread; never
    // blocks.
    void request_cancel();

    // Stops the host. The next render starts a fresh one.
    void shutdown();

private:
    [[nodiscard]] bool ensure_host(const EnvConfig& env);
    void kill_host_locked(bool graceful);
    [[nodiscard]] bool write_all(const void* data, DWORD size);
    [[nodiscard]] bool read_all(void* data, DWORD size);
    void start_stderr_pump();

    std::mutex mutex_;               // serialises render/shutdown
    PROCESS_INFORMATION proc_ = {};  // hProcess null when no host is running
    HANDLE child_stdin_ = nullptr;   // our write end
    HANDLE child_stdout_ = nullptr;  // our read end
    HANDLE child_stderr_ = nullptr;  // our read end, drained by a logging thread
    HANDLE job_ = nullptr;           // kills the host if this process dies
    HANDLE cancel_event_ = nullptr;  // named; the host opens it by name
    std::wstring cancel_event_name_;
    EnvConfig running_env_;
    bool streaming_ = true;  // falls back to 'TGR3' if the host refuses 'TGR4'
};

// The one host client this process keeps.
[[nodiscard]] HostClient& shared_host();

}  // namespace cmv
