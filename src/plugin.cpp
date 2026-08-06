// SPDX-License-Identifier: GPL-3.0-only
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "adjustment_controller.hpp"

#include "scssdk_input.h"
#include "scssdk_telemetry.h"
#include "common/scssdk_telemetry_truck_common_channels.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "eurotrucks2/scssdk_input_eut2.h"

namespace {

constexpr wchar_t kWindowClassName[] = L"ICC_MiniHud_Window";
constexpr wchar_t kWindowTitle[] = L"Intelligent Cruise Control";
constexpr UINT kHudHotkeyId = 0x49434301;
constexpr UINT_PTR kRefreshTimerId = 1;
constexpr UINT kRefreshIntervalMs = 200;
constexpr int kToggleButtonId = 1001;

HMODULE g_module = nullptr;
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_stop_ui{false};
std::atomic<float> g_cruise_mps{0.0F};
std::atomic<float> g_limit_mps{0.0F};
std::atomic<bool> g_telemetry_started{false};

std::mutex g_controller_mutex;
icc::AdjustmentController g_controller(true);

std::thread g_ui_thread;
HWND g_hud_window = nullptr;
HWND g_toggle_button = nullptr;
DWORD g_ui_thread_id = 0;
int g_next_reported_input = 0;
bool g_increase_pressed = false;
bool g_decrease_pressed = false;
scs_log_t g_scs_log = nullptr;

std::wstring settings_path() {
    wchar_t local_app_data[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"intelligent_cruise_control.ini";
    }

    std::wstring directory(local_app_data);
    directory += L"\\IntelligentCruiseControl";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory + L"\\settings.ini";
}

void save_enabled_setting(bool enabled) {
    const std::wstring path = settings_path();
    WritePrivateProfileStringW(
        L"intelligent_cruise_control", L"enabled", enabled ? L"1" : L"0", path.c_str());
}

int read_setting(const wchar_t* name, int default_value) {
    const std::wstring path = settings_path();
    return GetPrivateProfileIntW(
        L"intelligent_cruise_control", name, default_value, path.c_str());
}

void ensure_default_settings_file() {
    const std::wstring path = settings_path();
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return;
    }

    WritePrivateProfileStringW(
        L"intelligent_cruise_control",
        L"enabled",
        L"1",
        path.c_str());
    WritePrivateProfileStringW(
        L"intelligent_cruise_control",
        L"hud_hotkey_vk",
        L"33",
        path.c_str());  // 33 is the Windows virtual-key code for Page Up.
    WritePrivateProfileStringW(
        L"intelligent_cruise_control", L"hud_x", L"40", path.c_str());
    WritePrivateProfileStringW(
        L"intelligent_cruise_control", L"hud_y", L"120", path.c_str());
}

void log_message(scs_log_type_t type, const char* message) {
    if (g_scs_log != nullptr) {
        g_scs_log(type, message);
    }
}

void set_enabled(bool enabled, bool persist) {
    g_enabled.store(enabled, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_controller_mutex);
        g_controller.set_enabled(enabled);
    }
    if (persist) {
        save_enabled_setting(enabled);
    }
}

std::wstring button_text() {
    return g_enabled.load(std::memory_order_relaxed)
        ? L"Automatic adjustment:  ON"
        : L"Automatic adjustment:  OFF";
}

void refresh_hud_controls() {
    if (g_toggle_button == nullptr) {
        return;
    }

    const std::wstring text = button_text();
    SetWindowTextW(g_toggle_button, text.c_str());
}

LRESULT CALLBACK hud_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            g_toggle_button = CreateWindowExW(
                0,
                L"BUTTON",
                button_text().c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                18,
                42,
                284,
                38,
                window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToggleButtonId)),
                g_module,
                nullptr);

            const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            SendMessageW(g_toggle_button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SetTimer(window, kRefreshTimerId, kRefreshIntervalMs, nullptr);
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(wparam) == kToggleButtonId && HIWORD(wparam) == BN_CLICKED) {
                set_enabled(!g_enabled.load(std::memory_order_relaxed), true);
                refresh_hud_controls();
                return 0;
            }
            break;

        case WM_HOTKEY:
            if (wparam == kHudHotkeyId) {
                if (IsWindowVisible(window)) {
                    ShowWindow(window, SW_HIDE);
                } else {
                    ShowWindow(window, SW_SHOWNOACTIVATE);
                    SetWindowPos(
                        window,
                        HWND_TOPMOST,
                        0,
                        0,
                        0,
                        0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
                return 0;
            }
            break;

        case WM_TIMER:
            if (wparam == kRefreshTimerId) {
                refresh_hud_controls();
                return 0;
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT paint = {};
            HDC dc = BeginPaint(window, &paint);
            RECT client = {};
            GetClientRect(window, &client);
            FillRect(dc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(30, 30, 30));
            RECT title_rect{16, 12, client.right - 16, 36};
            DrawTextW(
                dc,
                kWindowTitle,
                -1,
                &title_rect,
                DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            EndPaint(window, &paint);
            return 0;
        }

        case WM_CLOSE:
            ShowWindow(window, SW_HIDE);
            return 0;

        case WM_DESTROY:
            KillTimer(window, kRefreshTimerId);
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

void ui_thread_main() {
    g_ui_thread_id = GetCurrentThreadId();

    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = hud_window_proc;
    window_class.hInstance = g_module;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClassName;
    RegisterClassExW(&window_class);

    const int x = read_setting(L"hud_x", 40);
    const int y = read_setting(L"hud_y", 120);
    const int hotkey = read_setting(L"hud_hotkey_vk", VK_PRIOR);

    g_hud_window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kWindowClassName,
        kWindowTitle,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x,
        y,
        328,
        122,
        nullptr,
        nullptr,
        g_module,
        nullptr);

    if (g_hud_window == nullptr) {
        return;
    }

    if (!RegisterHotKey(g_hud_window, kHudHotkeyId, MOD_NOREPEAT, hotkey)) {
        log_message(SCS_LOG_TYPE_warning, "[ICC] Unable to register the mini HUD hotkey.");
    }

    MSG message = {};
    while (!g_stop_ui.load(std::memory_order_relaxed) &&
           GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    UnregisterHotKey(g_hud_window, kHudHotkeyId);
    if (IsWindow(g_hud_window)) {
        DestroyWindow(g_hud_window);
    }
    g_hud_window = nullptr;
    g_toggle_button = nullptr;
    UnregisterClassW(kWindowClassName, g_module);
}

void start_ui() {
    if (g_ui_thread.joinable()) {
        return;
    }

    ensure_default_settings_file();
    set_enabled(read_setting(L"enabled", 1) != 0, false);
    g_stop_ui.store(false, std::memory_order_relaxed);
    g_ui_thread = std::thread(ui_thread_main);
}

void stop_ui() {
    g_stop_ui.store(true, std::memory_order_relaxed);
    if (g_hud_window != nullptr) {
        PostMessageW(g_hud_window, WM_CLOSE, 0, 0);
        PostThreadMessageW(g_ui_thread_id, WM_QUIT, 0, 0);
    }
    if (g_ui_thread.joinable()) {
        g_ui_thread.join();
    }
}

SCSAPI_VOID telemetry_store_cruise(
    const scs_string_t,
    const scs_u32_t,
    const scs_value_t* const value,
    const scs_context_t) {
    const float cruise = value == nullptr ? 0.0F : value->value_float.value;
    g_cruise_mps.store(cruise, std::memory_order_relaxed);
}

SCSAPI_VOID telemetry_store_speed_limit(
    const scs_string_t,
    const scs_u32_t,
    const scs_value_t* const value,
    const scs_context_t) {
    const float limit = value == nullptr ? 0.0F : value->value_float.value;
    g_limit_mps.store(limit, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(g_controller_mutex);
    g_controller.observe_speed_limit(
        limit, g_cruise_mps.load(std::memory_order_relaxed));
}

SCSAPI_VOID telemetry_started(
    const scs_event_t,
    const void* const,
    const scs_context_t) {
    g_telemetry_started.store(true, std::memory_order_relaxed);
}

SCSAPI_VOID telemetry_paused(
    const scs_event_t,
    const void* const,
    const scs_context_t) {
    g_telemetry_started.store(false, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_controller_mutex);
    g_controller.cancel();
}

SCSAPI_RESULT input_event_callback(
    scs_input_event_t* const event_info,
    const scs_u32_t flags,
    const scs_context_t) {
    if ((flags & SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame) != 0) {
        g_next_reported_input = 0;
        g_increase_pressed = false;
        g_decrease_pressed = false;

        if (g_telemetry_started.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lock(g_controller_mutex);
            const int pulse =
                g_controller.next_pulse(g_cruise_mps.load(std::memory_order_relaxed));
            g_increase_pressed = pulse > 0;
            g_decrease_pressed = pulse < 0;
        }
    }

    if (g_next_reported_input >= 2) {
        return SCS_RESULT_not_found;
    }

    event_info->input_index = static_cast<scs_u32_t>(g_next_reported_input);
    event_info->value_bool.value =
        (g_next_reported_input == 0 ? g_increase_pressed : g_decrease_pressed) ? 1 : 0;
    ++g_next_reported_input;
    return SCS_RESULT_ok;
}

}  // namespace

SCSAPI_RESULT scs_input_init(
    const scs_u32_t version,
    const scs_input_init_params_t* const params) {
    if (version != SCS_INPUT_VERSION_1_00) {
        return SCS_RESULT_unsupported;
    }

    const auto* const version_params =
        static_cast<const scs_input_init_params_v100_t*>(params);
    g_scs_log = version_params->common.log;

    if (std::strcmp(version_params->common.game_id, SCS_GAME_ID_EUT2) != 0) {
        log_message(SCS_LOG_TYPE_error, "[ICC] This plugin supports Euro Truck Simulator 2 only.");
        return SCS_RESULT_unsupported;
    }

    static const scs_input_device_input_t inputs[] = {
        {"cruiectrlinc", "ICC cruise increase", SCS_VALUE_TYPE_bool},
        {"cruiectrldec", "ICC cruise decrease", SCS_VALUE_TYPE_bool},
    };

    scs_input_device_t device = {};
    device.name = "icc_semantic";
    device.display_name = "Intelligent Cruise Control";
    device.type = SCS_INPUT_DEVICE_TYPE_semantical;
    device.input_count = 2;
    device.inputs = inputs;
    device.input_event_callback = input_event_callback;

    if (version_params->register_device(&device) != SCS_RESULT_ok) {
        log_message(SCS_LOG_TYPE_error, "[ICC] Failed to register semantic cruise controls.");
        return SCS_RESULT_generic_error;
    }

    log_message(SCS_LOG_TYPE_message, "[ICC] Input device initialized.");
    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_input_shutdown() {
    log_message(SCS_LOG_TYPE_message, "[ICC] Input device shut down.");
}

SCSAPI_RESULT scs_telemetry_init(
    const scs_u32_t version,
    const scs_telemetry_init_params_t* const params) {
    if (version != SCS_TELEMETRY_VERSION_1_00 &&
        version != SCS_TELEMETRY_VERSION_1_01) {
        return SCS_RESULT_unsupported;
    }

    const auto* const version_params =
        static_cast<const scs_telemetry_init_params_v101_t*>(params);
    g_scs_log = version_params->common.log;

    if (std::strcmp(version_params->common.game_id, SCS_GAME_ID_EUT2) != 0) {
        return SCS_RESULT_unsupported;
    }

    const bool channels_registered =
        version_params->register_for_channel(
            SCS_TELEMETRY_TRUCK_CHANNEL_cruise_control,
            SCS_U32_NIL,
            SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_each_frame | SCS_TELEMETRY_CHANNEL_FLAG_no_value,
            telemetry_store_cruise,
            nullptr) == SCS_RESULT_ok &&
        version_params->register_for_channel(
            SCS_TELEMETRY_TRUCK_CHANNEL_navigation_speed_limit,
            SCS_U32_NIL,
            SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_no_value,
            telemetry_store_speed_limit,
            nullptr) == SCS_RESULT_ok;

    const bool events_registered =
        version_params->register_for_event(
            SCS_TELEMETRY_EVENT_started, telemetry_started, nullptr) == SCS_RESULT_ok &&
        version_params->register_for_event(
            SCS_TELEMETRY_EVENT_paused, telemetry_paused, nullptr) == SCS_RESULT_ok;

    if (!channels_registered || !events_registered) {
        log_message(SCS_LOG_TYPE_error, "[ICC] Failed to register required telemetry.");
        return SCS_RESULT_generic_error;
    }

    start_ui();
    log_message(
        SCS_LOG_TYPE_message,
        "[ICC] Telemetry initialized. Press Page Up for the mini HUD.");
    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_telemetry_shutdown() {
    {
        std::lock_guard<std::mutex> lock(g_controller_mutex);
        g_controller.cancel();
    }
    stop_ui();
    log_message(SCS_LOG_TYPE_message, "[ICC] Telemetry shut down.");
    g_scs_log = nullptr;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
