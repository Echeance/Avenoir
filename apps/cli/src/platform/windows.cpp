// Lancement du démon sous Windows : CreateProcessW détaché (sans console),
// dans la session de l'utilisateur ; ligne de commande construite avec les
// règles de guillemets de Windows, jamais par cmd.exe.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include "avenoir/core/error.hpp"

#include "../launcher.hpp"

namespace avenoir::cli {

namespace {

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

/// Argument entre guillemets selon les règles de CommandLineToArgvW.
std::wstring quote(const std::wstring& argument) {
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

} // namespace

core::Result<void> start_daemon(const std::vector<std::string>& arguments) {
    std::wstring self(MAX_PATH, L'\0');
    DWORD length = 0;
    while (true) {
        length = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
        if (length == 0) {
            return core::make_error(core::ErrorCode::Ipc, "exécutable introuvable");
        }
        if (length < self.size()) {
            break;
        }
        self.resize(self.size() * 2);
    }
    self.resize(length);
    const std::filesystem::path program =
        std::filesystem::path(self).parent_path() / L"avenoird.exe";
    std::error_code error;
    if (!std::filesystem::is_regular_file(program, error)) {
        return core::make_error(core::ErrorCode::Ipc, "démon introuvable : avenoird.exe");
    }
    std::wstring command = quote(program.wstring());
    for (const auto& argument : arguments) {
        command += L" " + quote(widen(argument));
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE,
                       DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW, nullptr,
                       nullptr, &startup, &process) == FALSE) {
        return core::make_error(core::ErrorCode::Ipc, "démon impossible à lancer (code " +
                                                          std::to_string(GetLastError()) + ")");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return {};
}

std::optional<int> run_in_terminal(const std::vector<std::string>& /*arguments*/) {
    return std::nullopt; // seulement sous Linux (installation de swaylock)
}

} // namespace avenoir::cli
