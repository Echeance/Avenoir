// Lancement du démon sous Windows : CreateProcessW détaché (sans console),
// dans la session de l'utilisateur ; ligne de commande construite avec les
// règles de guillemets de Windows, jamais par cmd.exe.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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

std::optional<std::string> read_hidden_line() {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD saved = 0;
    if (input == INVALID_HANDLE_VALUE || GetConsoleMode(input, &saved) == FALSE) {
        return std::nullopt;
    }
    // Mode ligne sans écho.
    const DWORD hidden = (saved | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT) &
                         ~static_cast<DWORD>(ENABLE_ECHO_INPUT);
    SetConsoleMode(input, hidden);
    std::wstring buffer(4096, L'\0');
    DWORD read = 0;
    const BOOL ok =
        ReadConsoleW(input, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr);
    SetConsoleMode(input, saved);
    if (ok == FALSE || read == 0) {
        return std::nullopt;
    }
    buffer.resize(read);
    while (!buffer.empty() && (buffer.back() == L'\n' || buffer.back() == L'\r')) {
        buffer.pop_back();
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(buffer.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string line(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(buffer.size()), line.data(),
                        size, nullptr, nullptr);
    return line;
}

bool copy_to_clipboard(std::string_view text) {
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size < 0 || OpenClipboard(nullptr) == FALSE) {
        return false;
    }
    bool copied = false;
    const std::size_t bytes = (static_cast<std::size_t>(size) + 1) * sizeof(wchar_t);
    if (EmptyClipboard() != FALSE) {
        // Mémoire globale cédée au presse-papiers en cas de succès.
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory != nullptr) {
            auto* target = static_cast<wchar_t*>(GlobalLock(memory));
            if (target != nullptr) {
                MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), target,
                                    size);
                target[size] = L'\0';
                GlobalUnlock(memory);
                copied = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
            }
            if (!copied) {
                GlobalFree(memory);
            }
        }
    }
    CloseClipboard();
    return copied;
}

} // namespace avenoir::cli
