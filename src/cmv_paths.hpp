// Where the engine lives, worked out without the registry.
//
// The SAPI DLLs, the configuration utility and the command-line tools all find the engine
// the same way: relative to their own module, walking a few levels up and probing for a
// folder that holds panthera_host.exe next to a Leopard speech tree. The environment
// variable CLASSICMAC_ENGINE_DIR overrides the search, for running out of odd places.
//
// Two layouts are recognised, because both genuinely exist:
//
//   installed        {app}\engine\panthera_host.exe
//                    {app}\engine\leopard\Speech\Voices\...
//
//   development      {repo}\bin\_panthera\panthera_host.exe
//                    {repo}\bin\leopard\Speech\Voices\...

#pragma once

#include <windows.h>
#include <string>

namespace cmv {

[[nodiscard]] bool file_exists(const std::wstring& path);
[[nodiscard]] bool dir_exists(const std::wstring& path);

// The directory holding the given module, without a trailing backslash.
[[nodiscard]] std::wstring module_directory(HMODULE module);

// The directory holding whichever binary this code is linked into.
[[nodiscard]] std::wstring own_module_directory();

// Absolute path of panthera_host.exe, or empty when no engine was found.
[[nodiscard]] const std::wstring& host_exe_path();

// The Leopard tree - the directory that contains Speech\ and
// SpeechDictionary.framework\ - or empty when no engine was found.
[[nodiscard]] const std::wstring& tree_root();

// Engine files inside the tree.
[[nodiscard]] std::wstring macintalk_path();
[[nodiscard]] std::wstring speechdict_path();
[[nodiscard]] std::wstring voices_dir();

// True when every piece the host needs is present.
[[nodiscard]] bool engine_usable();

// %APPDATA%\ClassicMacVoices, created on first use.
[[nodiscard]] std::wstring config_dir();

// %APPDATA%\ClassicMacVoices\settings.ini.
[[nodiscard]] std::wstring settings_path();

// Overrides the search, for tools that take the engine location as an argument.
void set_engine_dir(const std::wstring& dir);

}  // namespace cmv
