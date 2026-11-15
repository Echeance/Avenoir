// Fenêtre du widget sous Windows : fenêtre Win32 sans bordure, au premier
// plan, en haut au centre de l'écran principal, avec WebView2 (Edge, déjà
// présent dans Windows 10 et 11). Chargeur WebView2 lié en statique.
//
// La page est servie sous https://app.avenoir/ depuis le seul dossier de
// l'interface (correspondance d'hôte virtuel) ; toute autre navigation et
// toute nouvelle fenêtre sont refusées.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// clang-format off
#include <windows.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <wrl.h>
// clang-format on
#include <WebView2.h>
#include <spdlog/spdlog.h>

#include "avenoir/core/error.hpp"

#include "../assets.hpp"
#include "../relay.hpp"
#include "../window.hpp"

namespace avenoir::ui {

namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

constexpr std::wstring_view kHost = L"app.avenoir";
constexpr std::wstring_view kOrigin = L"https://app.avenoir/";
constexpr UINT kPostMessage = WM_APP + 1; ///< Message pour la page (chaîne à rendre).
constexpr UINT kQuitMessage = WM_APP + 2;
constexpr wchar_t kClassName[] = L"AvenoirWidget";

std::wstring wide(std::string_view text) {
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string narrow(std::wstring_view text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size,
                        nullptr, nullptr);
    return out;
}

/// Dossier des données de WebView2 : %LOCALAPPDATA%\avenoir\webview2.
std::wstring user_data_folder() {
    std::wstring folder(MAX_PATH, L'\0');
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", folder.data(), MAX_PATH);
    folder.resize(size);
    return folder + L"\\avenoir\\webview2";
}

class WebViewWindow final : public Window {
  public:
    explicit WebViewWindow(WindowOptions options) : options_(std::move(options)) {}

    ~WebViewWindow() override {
        controller_.Reset();
        view_.Reset();
        if (window_ != nullptr) {
            DestroyWindow(window_);
        }
        if (com_) {
            CoUninitialize();
        }
    }
    WebViewWindow(const WebViewWindow&) = delete;
    WebViewWindow& operator=(const WebViewWindow&) = delete;
    WebViewWindow(WebViewWindow&&) = delete;
    WebViewWindow& operator=(WebViewWindow&&) = delete;

    core::Result<void> build() {
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
            return core::make_error(core::ErrorCode::Config, "COM indisponible");
        }
        com_ = true;
        thread_ = GetCurrentThreadId();
        // Net sur les écrans mis à l'échelle : tailles en pixels réels.
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        scale_ = static_cast<double>(GetDpiForSystem()) / 96.0;
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW type{};
        type.cbSize = sizeof(type);
        type.lpfnWndProc = &WebViewWindow::procedure;
        type.hInstance = instance;
        type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW (forme large)
        type.lpszClassName = kClassName;
        type.hbrBackground = CreateSolidBrush(RGB(0x13, 0x1a, 0x2e));
        RegisterClassExW(&type);

        // En haut au centre de l'écran principal, au-dessus des fenêtres,
        // absent de la barre des tâches.
        RECT area{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
        if (options_.application) {
            // Application principale : fenêtre ordinaire, centrée.
            const int width = pixels(kAppWidth);
            const int height = pixels(kAppHeight);
            window_ = CreateWindowExW(0, kClassName, L"Avenoir", WS_OVERLAPPEDWINDOW,
                                      area.left + ((area.right - area.left - width) / 2),
                                      area.top + ((area.bottom - area.top - height) / 2), width,
                                      height, nullptr, nullptr, instance, this);
        } else {
            const int width = pixels(kWidgetWidth);
            const int left = area.left + ((area.right - area.left - width) / 2);
            window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName, L"Avenoir",
                                      WS_POPUP, left, area.top, width, pixels(kMinHeight), nullptr,
                                      nullptr, instance, this);
        }
        if (window_ == nullptr) {
            return core::make_error(core::ErrorCode::Config, "fenêtre impossible à créer");
        }
        // Coins arrondis de Windows 11 (sans effet ailleurs).
        const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUNDSMALL;
        DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));

        const std::wstring folder = user_data_folder();
        const HRESULT created = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, folder.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                    if (FAILED(result) || environment == nullptr) {
                        spdlog::error("WebView2 indisponible (code {:#x})",
                                      static_cast<unsigned>(result));
                        PostQuitMessage(1);
                        return S_OK;
                    }
                    return environment->CreateCoreWebView2Controller(
                        window_,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](HRESULT done, ICoreWebView2Controller* controller) -> HRESULT {
                                return attach(done, controller);
                            })
                            .Get());
                })
                .Get());
        if (FAILED(created)) {
            return core::make_error(core::ErrorCode::Config,
                                    "WebView2 introuvable : installer le « Microsoft Edge "
                                    "WebView2 Runtime »");
        }
        return {};
    }

    void post(std::string json) override {
        // Rendue au fil de la fenêtre, qui la libère après l'avoir donnée à la page.
        auto text = std::make_unique<std::wstring>(wide(json));
        if (PostMessageW(window_, kPostMessage, 0, reinterpret_cast<LPARAM>(text.get())) != FALSE) {
            static_cast<void>(text.release());
        }
    }

    void resize(int height) override {
        // Appelée depuis le fil de la fenêtre (message de la page).
        if (options_.application) {
            return;
        }
        RECT frame{};
        GetWindowRect(window_, &frame);
        SetWindowPos(window_, HWND_TOPMOST, frame.left, frame.top, pixels(kWidgetWidth),
                     pixels(height), SWP_NOACTIVATE);
        fit();
    }

    int run() override {
        ShowWindow(window_, options_.application ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (message.hwnd == nullptr && message.message == kQuitMessage) {
                PostQuitMessage(0); // demande d'arrêt venue d'un autre fil
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

    void quit() override { PostThreadMessageW(thread_, kQuitMessage, 0, 0); }

  private:
    HRESULT attach(HRESULT result, ICoreWebView2Controller* controller) {
        if (FAILED(result) || controller == nullptr) {
            spdlog::error("vue WebView2 impossible à créer (code {:#x})",
                          static_cast<unsigned>(result));
            PostQuitMessage(1);
            return S_OK;
        }
        controller_ = controller;
        controller_->get_CoreWebView2(&view_);
        // Fond de la vue : celui du widget (la fenêtre n'est pas transparente).
        ComPtr<ICoreWebView2Controller2> colored;
        if (SUCCEEDED(controller_.As(&colored))) {
            colored->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{255, 0x13, 0x1a, 0x2e});
        }

        ComPtr<ICoreWebView2Settings> settings;
        view_->get_Settings(&settings);
        settings->put_AreDevToolsEnabled(options_.developer_tools ? TRUE : FALSE);
        settings->put_AreDefaultContextMenusEnabled(options_.developer_tools ? TRUE : FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreHostObjectsAllowed(FALSE);
        settings->put_IsWebMessageEnabled(TRUE);
        settings->put_IsZoomControlEnabled(FALSE);
        ComPtr<ICoreWebView2Settings3> accelerators;
        if (SUCCEEDED(settings.As(&accelerators))) {
            accelerators->put_AreBrowserAcceleratorKeysEnabled(options_.developer_tools ? TRUE
                                                                                        : FALSE);
        }

        // Les fichiers de la page, et rien d'autre.
        ComPtr<ICoreWebView2_3> mapped;
        if (FAILED(view_.As(&mapped)) ||
            FAILED(mapped->SetVirtualHostNameToFolderMapping(
                std::wstring(kHost).c_str(), options_.ui_directory.wstring().c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY))) {
            spdlog::error("WebView2 trop ancien (correspondance d'hôte virtuel absente)");
            PostQuitMessage(1);
            return S_OK;
        }

        EventRegistrationToken token{};
        view_->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [](ICoreWebView2* /*sender*/, ICoreWebView2NavigationStartingEventArgs* args) {
                    LPWSTR uri = nullptr;
                    args->get_Uri(&uri);
                    const bool own = uri != nullptr && std::wstring_view(uri).starts_with(kOrigin);
                    CoTaskMemFree(uri);
                    if (!own) {
                        spdlog::warn("interface : navigation refusée");
                        args->put_Cancel(TRUE);
                    }
                    return S_OK;
                })
                .Get(),
            &token);
        view_->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2* /*sender*/, ICoreWebView2NewWindowRequestedEventArgs* args) {
                    args->put_Handled(TRUE); // aucune fenêtre ouverte par la page
                    return S_OK;
                })
                .Get(),
            &token);
        view_->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](ICoreWebView2* /*sender*/, ICoreWebView2WebMessageReceivedEventArgs* args) {
                    LPWSTR text = nullptr;
                    if (SUCCEEDED(args->TryGetWebMessageAsString(&text)) && text != nullptr) {
                        std::string message = narrow(text);
                        if (message.size() <= kMaxPageMessage && options_.from_page) {
                            options_.from_page(std::move(message));
                        }
                    }
                    CoTaskMemFree(text);
                    return S_OK;
                })
                .Get(),
            &token);

        serve_module_tabs();

        fit();
        std::wstring page = L"index.html";
        if (options_.application) {
            page = L"index.html#app";
            if (is_view_name(options_.view)) {
                page += L"/" + wide(options_.view);
            }
        } else if (options_.pinned) {
            page = L"index.html#ouvert";
            if (is_view_name(options_.view)) {
                page += L"/" + wide(options_.view);
            }
        }
        const std::wstring start = std::wstring(kOrigin) + page;
        view_->Navigate(start.c_str());
        return S_OK;
    }

    /// Onglets web des modules (directive 12) : chacun sous sa propre origine
    /// `https://<module>.module.avenoir/`, servi depuis son dossier `ui` avec
    /// une politique de sécurité sans réseau ; les iframes ne vont nulle part
    /// ailleurs.
    void serve_module_tabs() {
        constexpr std::wstring_view kFilter = L"https://*.module.avenoir/*";
        ComPtr<ICoreWebView2_22> sources;
        if (SUCCEEDED(view_.As(&sources))) {
            // Iframes d'une autre origine : seulement avec ce filtre.
            sources->AddWebResourceRequestedFilterWithRequestSourceKinds(
                std::wstring(kFilter).c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL,
                COREWEBVIEW2_WEB_RESOURCE_REQUEST_SOURCE_KINDS_DOCUMENT);
        } else {
            view_->AddWebResourceRequestedFilter(std::wstring(kFilter).c_str(),
                                                 COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        }
        EventRegistrationToken token{};
        view_->add_WebResourceRequested(
            Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                [this](ICoreWebView2* sender, ICoreWebView2WebResourceRequestedEventArgs* args) {
                    ComPtr<ICoreWebView2WebResourceRequest> request;
                    LPWSTR uri = nullptr;
                    if (FAILED(args->get_Request(&request)) || FAILED(request->get_Uri(&uri)) ||
                        uri == nullptr) {
                        return S_OK;
                    }
                    const std::string text = narrow(uri);
                    CoTaskMemFree(uri);
                    respond_module_asset(sender, args, text);
                    return S_OK;
                })
                .Get(),
            &token);
        view_->add_FrameNavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [](ICoreWebView2* /*sender*/, ICoreWebView2NavigationStartingEventArgs* args) {
                    LPWSTR uri = nullptr;
                    args->get_Uri(&uri);
                    const bool module =
                        uri != nullptr && parse_module_address(narrow(uri)).has_value();
                    CoTaskMemFree(uri);
                    if (!module) {
                        spdlog::warn("interface : navigation d'iframe refusée");
                        args->put_Cancel(TRUE);
                    }
                    return S_OK;
                })
                .Get(),
            &token);
    }

    void respond_module_asset(ICoreWebView2* sender,
                              ICoreWebView2WebResourceRequestedEventArgs* args,
                              const std::string& uri) const {
        std::optional<Asset> asset;
        std::string policy;
        if (const auto address = parse_module_address(uri); address && options_.module_root) {
            if (const auto root = options_.module_root(address->module)) {
                asset = resolve_asset(*root, address->path);
                policy = module_tab_policy(address->origin);
            }
        }
        ComPtr<ICoreWebView2_2> view;
        ComPtr<ICoreWebView2Environment> environment;
        if (FAILED(sender->QueryInterface(IID_PPV_ARGS(&view))) ||
            FAILED(view->get_Environment(&environment))) {
            return;
        }
        ComPtr<ICoreWebView2WebResourceResponse> response;
        if (!asset) {
            environment->CreateWebResourceResponse(nullptr, 404, L"Not Found", L"", &response);
            args->put_Response(response.Get());
            return;
        }
        std::ifstream file(asset->path, std::ios::binary);
        const std::string content{std::istreambuf_iterator<char>(file),
                                  std::istreambuf_iterator<char>()};
        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(content.data()),
                                        static_cast<UINT>(content.size())));
        const std::wstring headers = L"Content-Type: " + wide(asset->mime) +
                                     L"\r\nContent-Security-Policy: " + wide(policy) +
                                     L"\r\nX-Content-Type-Options: nosniff";
        environment->CreateWebResourceResponse(stream.Get(), 200, L"OK", headers.c_str(),
                                               &response);
        args->put_Response(response.Get());
    }

    /// Pixels CSS → pixels de l'écran.
    [[nodiscard]] int pixels(int css) const {
        return static_cast<int>((static_cast<double>(css) * scale_) + 0.5);
    }

    void fit() {
        if (controller_) {
            RECT bounds{};
            GetClientRect(window_, &bounds);
            controller_->put_Bounds(bounds);
        }
    }

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* self = reinterpret_cast<WebViewWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        switch (message) {
        case kPostMessage: {
            const std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lparam));
            if (self != nullptr && self->view_) {
                self->view_->PostWebMessageAsJson(text->c_str());
            }
            return 0;
        }
        case WM_SIZE:
            if (self != nullptr) {
                self->fit();
            }
            return 0;
        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    WindowOptions options_;
    bool com_ = false;
    DWORD thread_ = 0;
    HWND window_ = nullptr;
    double scale_ = 1.0;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> view_;
};

} // namespace

core::Result<std::unique_ptr<Window>> create_window(WindowOptions options) {
    auto window = std::make_unique<WebViewWindow>(std::move(options));
    if (auto built = window->build(); !built) {
        return std::unexpected(std::move(built.error()));
    }
    return window;
}

void launch_application(const std::optional<std::filesystem::path>& config) {
    std::wstring path(MAX_PATH, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), MAX_PATH);
    if (size == 0 || size >= MAX_PATH) {
        return;
    }
    path.resize(size);
    std::wstring command = L"\"" + path + L"\" --app";
    if (config) {
        command += L" \"" + std::filesystem::absolute(*config).wstring() + L"\"";
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                       &startup, &process) != FALSE) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

std::optional<std::filesystem::path> default_ui_directory() {
    std::wstring path(MAX_PATH, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), MAX_PATH);
    if (size == 0 || size >= MAX_PATH) {
        return std::nullopt;
    }
    path.resize(size);
    return std::filesystem::path(path).parent_path() / "ui";
}

} // namespace avenoir::ui
