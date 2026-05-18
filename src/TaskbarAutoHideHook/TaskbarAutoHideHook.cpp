#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cwchar>
#include <algorithm>

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

namespace {

struct NativeSettings {
    int show_speedup = 250;
    int hide_speedup = 250;
    int frame_rate = 90;
    BOOL run_at_startup = TRUE;
    BOOL start_minimized = FALSE;
};

using AnimateWindowFn = BOOL (WINAPI*)(HWND, DWORD, DWORD);
using SetWindowPosFn = BOOL (WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using MoveWindowFn = BOOL (WINAPI*)(HWND, int, int, int, int, BOOL);
using GetTickCountFn = DWORD (WINAPI*)();
using SleepFn = void (WINAPI*)(DWORD);
using TaskbarViewSlideFn = PVOID (WINAPI*)(PVOID, PVOID, DWORD, DWORD, DWORD);

struct InlineHook {
    PVOID target_fn;
    PVOID hook_fn;
    BYTE original_bytes[32];
    DWORD original_size;
    BYTE trampoline[64];
    PVOID trampoline_ptr;
};

static NativeSettings g_settings{};
static wchar_t g_settings_path[MAX_PATH]{};
static wchar_t g_dll_path[MAX_PATH]{};
static wchar_t g_log_path[MAX_PATH]{};
static AnimateWindowFn g_original_animate_window = nullptr;
static SetWindowPosFn g_original_set_window_pos = nullptr;
static MoveWindowFn g_original_move_window = nullptr;
static GetTickCountFn g_original_get_tick_count = nullptr;
static SleepFn g_original_sleep = nullptr;
static HMODULE g_self_module = nullptr;
static HANDLE g_worker_thread = nullptr;
static volatile LONG g_hooks_installed = 0;
static volatile LONG g_patching_imports = 0;
static volatile LONG g_taskbar_slide_thread_id = 0;
static DWORD g_taskbar_slide_start_tick = 0;
static DWORD g_taskbar_slide_last_frame_tick = 0;
static DWORD g_taskbar_slide_speedup = 100;
static BOOL g_taskbar_slide_showing = FALSE;
static HMODULE g_last_taskbar_dll = nullptr;
static HMODULE g_last_taskbar_view_dll = nullptr;
static HMODULE g_last_explorer_extensions_dll = nullptr;
static BOOL g_taskbar_modules_logged = FALSE;
static BOOL g_settings_logged = FALSE;
static NativeSettings g_last_logged_settings{};
static BOOL g_trace_enabled = FALSE;
static BOOL g_taskbar_view_hooked = FALSE;
static PVOID g_taskbar_view_slide_fn = nullptr;
static DWORD g_taskbar_view_last_attempt_tick = 0;
static TaskbarViewSlideFn g_original_taskbar_view_slide = nullptr;
static InlineHook g_taskbar_slide_hook{};

DWORD WINAPI HookWorkerThread(LPVOID);
BOOL LoadSettings();
BOOL InstallProcessHooks();
BOOL PatchImportTable(HMODULE module_handle);
BOOL PatchProcessModules();
BOOL InjectSelfIntoExplorer();
DWORD GetExplorerPID();
BOOL IsTaskbarWindow(HWND hWnd);
void MarkTaskbarSlide(HWND hWnd, const RECT& old_rect, const RECT& new_rect);
void LogTaskbarModules();
void ClearExpiredTaskbarSlide();

void LogMessage(const wchar_t* message) {
    if (!g_log_path[0] || !message) {
        return;
    }

    HANDLE file = CreateFileW(g_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD bytes_written = 0;
    DWORD message_bytes = static_cast<DWORD>(wcslen(message) * sizeof(wchar_t));
    WriteFile(file, message, message_bytes, &bytes_written, nullptr);
    WriteFile(file, L"\r\n", sizeof(wchar_t) * 2, &bytes_written, nullptr);
    CloseHandle(file);
}

void LogLastError(const wchar_t* prefix) {
    wchar_t buffer[256]{};
    wsprintfW(buffer, L"%s. GetLastError=%lu", prefix, GetLastError());
    LogMessage(buffer);
}

int GetSpeedupPercent(DWORD dwFlags) {
    return (dwFlags & AW_HIDE) ? g_settings.hide_speedup : g_settings.show_speedup;
}

DWORD ScaleDuration(DWORD original_duration, int speedup_percent) {
    if (speedup_percent <= 1) {
        return original_duration;
    }

    double factor = static_cast<double>(speedup_percent) / 100.0;
    if (factor <= 0.0) {
        return original_duration;
    }

    DWORD scaled = static_cast<DWORD>(original_duration / factor);
    return std::max<DWORD>(1, scaled);
}

DWORD GetOriginalTickCount() {
    return g_original_get_tick_count ? g_original_get_tick_count() : ::GetTickCount();
}

int RectArea(const RECT& rect) {
    const int width = std::max<LONG>(0, rect.right - rect.left);
    const int height = std::max<LONG>(0, rect.bottom - rect.top);
    return width * height;
}

int GetVisibleTaskbarArea(HWND hWnd, const RECT& rect) {
    HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{ sizeof(monitor_info) };
    if (!GetMonitorInfoW(monitor, &monitor_info)) {
        return RectArea(rect);
    }

    RECT intersection{};
    if (!IntersectRect(&intersection, &rect, &monitor_info.rcMonitor)) {
        return 0;
    }

    return RectArea(intersection);
}

BOOL IsTaskbarWindow(HWND hWnd) {
    if (!IsWindow(hWnd)) {
        return FALSE;
    }

    wchar_t class_name[64]{};
    if (!GetClassNameW(hWnd, class_name, ARRAYSIZE(class_name))) {
        return FALSE;
    }

    return wcscmp(class_name, L"Shell_TrayWnd") == 0 ||
        wcscmp(class_name, L"Shell_SecondaryTrayWnd") == 0 ||
        wcscmp(class_name, L"Shell_SecondaryTrayWnd2") == 0 ||
        wcscmp(class_name, L"SecondaryTrayWnd") == 0;
}

void MarkTaskbarSlide(HWND hWnd, const RECT& old_rect, const RECT& new_rect) {
    if (InterlockedCompareExchange(&g_patching_imports, 0, 0) != 0 || !IsTaskbarWindow(hWnd)) {
        return;
    }

    const int old_visible_area = GetVisibleTaskbarArea(hWnd, old_rect);
    const int new_visible_area = GetVisibleTaskbarArea(hWnd, new_rect);
    if (old_visible_area == new_visible_area) {
        return;
    }

    const BOOL showing = new_visible_area > old_visible_area;
    const DWORD now = GetOriginalTickCount();
    const LONG current_thread_id = static_cast<LONG>(GetCurrentThreadId());
    const LONG active_thread_id = InterlockedCompareExchange(&g_taskbar_slide_thread_id, 0, 0);
    if (active_thread_id == current_thread_id && now - g_taskbar_slide_start_tick <= 5000) {
        if (g_taskbar_slide_showing == showing) {
            return;
        }
    }

    g_taskbar_slide_thread_id = current_thread_id;
    g_taskbar_slide_start_tick = now;
    g_taskbar_slide_last_frame_tick = now;
    g_taskbar_slide_speedup = static_cast<DWORD>(std::max<int>(1, showing ? g_settings.show_speedup : g_settings.hide_speedup));
    g_taskbar_slide_showing = showing;

    wchar_t buffer[256]{};
    wsprintfW(buffer, L"Taskbar slide detected: %s speedup=%lu oldVisible=%d newVisible=%d",
        showing ? L"show" : L"hide",
        g_taskbar_slide_speedup,
        old_visible_area,
        new_visible_area);
    LogMessage(buffer);
}

void ClearExpiredTaskbarSlide() {
    const LONG thread_id = InterlockedCompareExchange(&g_taskbar_slide_thread_id, 0, 0);
    if (thread_id == 0) {
        return;
    }

    const DWORD now = GetOriginalTickCount();
    if (now - g_taskbar_slide_start_tick > 5000) {
        InterlockedCompareExchange(&g_taskbar_slide_thread_id, 0, thread_id);
    }
}

BOOL IsTaskbarSlideThread() {
    const LONG thread_id = InterlockedCompareExchange(&g_taskbar_slide_thread_id, 0, 0);
    if (thread_id == 0 || static_cast<DWORD>(thread_id) != GetCurrentThreadId()) {
        return FALSE;
    }

    const DWORD now = GetOriginalTickCount();
    if (now - g_taskbar_slide_start_tick > 5000) {
        InterlockedCompareExchange(&g_taskbar_slide_thread_id, 0, thread_id);
        return FALSE;
    }

    return TRUE;
}

BOOL WINAPI HookedAnimateWindow(HWND hWnd, DWORD dwTime, DWORD dwFlags) {
    if (!g_original_animate_window) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    DWORD adjusted_time = ScaleDuration(dwTime, GetSpeedupPercent(dwFlags));
    if (adjusted_time == 0) {
        adjusted_time = 1;
    }

    wchar_t buffer[256]{};
    wsprintfW(buffer, L"AnimateWindow hit: original=%lu adjusted=%lu flags=0x%08lX", dwTime, adjusted_time, dwFlags);
    LogMessage(buffer);

    return g_original_animate_window(hWnd, adjusted_time, dwFlags);
}

BOOL WINAPI HookedSetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    RECT old_rect{};
    GetWindowRect(hWnd, &old_rect);

    RECT new_rect = old_rect;
    if ((uFlags & SWP_NOMOVE) == 0) {
        const int width = (uFlags & SWP_NOSIZE) ? old_rect.right - old_rect.left : cx;
        const int height = (uFlags & SWP_NOSIZE) ? old_rect.bottom - old_rect.top : cy;
        new_rect = { X, Y, X + width, Y + height };
    } else if ((uFlags & SWP_NOSIZE) == 0) {
        new_rect.right = new_rect.left + cx;
        new_rect.bottom = new_rect.top + cy;
    }

    MarkTaskbarSlide(hWnd, old_rect, new_rect);
    return g_original_set_window_pos
        ? g_original_set_window_pos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags)
        : ::SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

BOOL WINAPI HookedMoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint) {
    RECT old_rect{};
    GetWindowRect(hWnd, &old_rect);
    const RECT new_rect{ X, Y, X + nWidth, Y + nHeight };
    MarkTaskbarSlide(hWnd, old_rect, new_rect);
    return g_original_move_window
        ? g_original_move_window(hWnd, X, Y, nWidth, nHeight, bRepaint)
        : ::MoveWindow(hWnd, X, Y, nWidth, nHeight, bRepaint);
}

DWORD WINAPI HookedGetTickCount() {
    const DWORD now = GetOriginalTickCount();
    if (!IsTaskbarSlideThread()) {
        return now;
    }

    const DWORD elapsed = now - g_taskbar_slide_start_tick;
    const DWORD adjusted = g_taskbar_slide_start_tick +
        static_cast<DWORD>(elapsed * (static_cast<double>(std::max<DWORD>(1, g_taskbar_slide_speedup)) / 100.0));
    return adjusted;
}

void WINAPI HookedSleep(DWORD dwMilliseconds) {
    if (!g_original_sleep) {
        ::Sleep(dwMilliseconds);
        return;
    }

    if (!IsTaskbarSlideThread()) {
        g_original_sleep(dwMilliseconds);
        return;
    }

    const DWORD speedup = std::max<DWORD>(1, g_taskbar_slide_speedup);
    DWORD adjusted_sleep = std::max<DWORD>(1, static_cast<DWORD>(dwMilliseconds * 100.0 / speedup));

    if (g_settings.frame_rate > 0) {
        const DWORD now = GetOriginalTickCount();
        const DWORD frame_time = std::max<DWORD>(1, 1000 / static_cast<DWORD>(std::max<int>(1, g_settings.frame_rate)));
        const DWORD elapsed_since_frame = now - g_taskbar_slide_last_frame_tick;
        if (elapsed_since_frame < frame_time) {
            adjusted_sleep = std::min<DWORD>(adjusted_sleep, frame_time - elapsed_since_frame);
        }
        g_taskbar_slide_last_frame_tick = now + adjusted_sleep;
    }

    g_original_sleep(adjusted_sleep);
}

BOOL LoadTextFile(const wchar_t* path, wchar_t* buffer, DWORD buffer_chars) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        size.QuadPart >= 4096) {
        CloseHandle(file);
        return FALSE;
    }

    char raw_buffer[4096]{};
    DWORD bytes_read = 0;
    BOOL ok = ReadFile(file, raw_buffer, static_cast<DWORD>(size.QuadPart), &bytes_read, nullptr);
    CloseHandle(file);
    if (!ok || bytes_read == 0) {
        return FALSE;
    }

    const int written = MultiByteToWideChar(CP_UTF8, 0, raw_buffer, static_cast<int>(bytes_read), buffer, static_cast<int>(buffer_chars - 1));
    if (written <= 0) {
        return FALSE;
    }

    buffer[written] = L'\0';
    return TRUE;
}

BOOL ParseIntField(const wchar_t* text, const wchar_t* key, int* value) {
    const wchar_t* location = wcsstr(text, key);
    if (!location) {
        return FALSE;
    }

    location = wcschr(location, L':');
    if (!location) {
        return FALSE;
    }

    *value = _wtoi(location + 1);
    return TRUE;
}

BOOL ParseBoolField(const wchar_t* text, const wchar_t* key, BOOL* value) {
    const wchar_t* location = wcsstr(text, key);
    if (!location) {
        return FALSE;
    }

    location = wcschr(location, L':');
    if (!location) {
        return FALSE;
    }

    while (*++location == L' ' || *location == L'\t') {
    }

    if (_wcsnicmp(location, L"true", 4) == 0) {
        *value = TRUE;
        return TRUE;
    }

    if (_wcsnicmp(location, L"false", 5) == 0) {
        *value = FALSE;
        return TRUE;
    }

    return FALSE;
}

BOOL LoadSettings() {
    wchar_t buffer[4096]{};
    if (!g_settings_path[0]) {
        return FALSE;
    }

    if (!LoadTextFile(g_settings_path, buffer, ARRAYSIZE(buffer))) {
        g_settings = {};
        LogMessage(L"LoadSettings: settings file unavailable, using defaults");
        return FALSE;
    }

    NativeSettings loaded_settings{};
    ParseIntField(buffer, L"\"ShowSpeedup\"", &loaded_settings.show_speedup);
    ParseIntField(buffer, L"\"HideSpeedup\"", &loaded_settings.hide_speedup);
    ParseIntField(buffer, L"\"FrameRate\"", &loaded_settings.frame_rate);
    loaded_settings.show_speedup = std::max<int>(1, loaded_settings.show_speedup);
    loaded_settings.hide_speedup = std::max<int>(1, loaded_settings.hide_speedup);
    loaded_settings.frame_rate = std::clamp<int>(loaded_settings.frame_rate, 1, 240);
    ParseBoolField(buffer, L"\"RunAtStartup\"", &loaded_settings.run_at_startup);
    ParseBoolField(buffer, L"\"StartMinimized\"", &loaded_settings.start_minimized);
    g_settings = loaded_settings;

    if (g_trace_enabled && (!g_settings_logged ||
        g_last_logged_settings.show_speedup != g_settings.show_speedup ||
        g_last_logged_settings.hide_speedup != g_settings.hide_speedup ||
        g_last_logged_settings.frame_rate != g_settings.frame_rate)) {
        wchar_t buffer_log[256]{};
        wsprintfW(buffer_log, L"Loaded settings: show=%d hide=%d fps=%d", g_settings.show_speedup, g_settings.hide_speedup, g_settings.frame_rate);
        LogMessage(buffer_log);
        g_last_logged_settings = g_settings;
        g_settings_logged = TRUE;
    }
    return TRUE;
}

BOOL ReloadSettings() {
    if (!g_settings_path[0]) {
        return FALSE;
    }

    return LoadSettings();
}

// Get Explorer process ID
DWORD GetExplorerPID() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        LogLastError(L"GetExplorerPID: CreateToolhelp32Snapshot failed");
        return 0;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"explorer.exe") == 0) {
                CloseHandle(snap);
                return entry.th32ProcessID;
            }
        } while (Process32NextW(snap, &entry));
    }

    CloseHandle(snap);
    return 0;
}

BOOL PatchImportTable(HMODULE module_handle) {
    if (!module_handle) {
        return FALSE;
    }

    auto* dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(module_handle);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return FALSE;
    }

    auto* nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<BYTE*>(module_handle) + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        return FALSE;
    }

    const DWORD import_rva = nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!import_rva) {
        return FALSE;
    }

    auto* import_desc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(reinterpret_cast<BYTE*>(module_handle) + import_rva);
    BOOL any_patched = FALSE;
    for (; import_desc->Name; ++import_desc) {
        auto* original_thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(reinterpret_cast<BYTE*>(module_handle) + import_desc->OriginalFirstThunk);
        auto* first_thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(reinterpret_cast<BYTE*>(module_handle) + import_desc->FirstThunk);

        if (!original_thunk) {
            original_thunk = first_thunk;
        }

        for (; original_thunk && original_thunk->u1.AddressOfData; ++original_thunk, ++first_thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(original_thunk->u1.Ordinal)) {
                continue;
            }

            auto* import_by_name = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(reinterpret_cast<BYTE*>(module_handle) + original_thunk->u1.AddressOfData);
            const char* function_name = reinterpret_cast<char*>(import_by_name->Name);
            void* hook_function = nullptr;
            void** original_function = nullptr;

            if (strcmp(function_name, "AnimateWindow") == 0) {
                hook_function = reinterpret_cast<void*>(&HookedAnimateWindow);
                original_function = reinterpret_cast<void**>(&g_original_animate_window);
            } else if (strcmp(function_name, "SetWindowPos") == 0) {
                hook_function = reinterpret_cast<void*>(&HookedSetWindowPos);
                original_function = reinterpret_cast<void**>(&g_original_set_window_pos);
            } else if (strcmp(function_name, "MoveWindow") == 0) {
                hook_function = reinterpret_cast<void*>(&HookedMoveWindow);
                original_function = reinterpret_cast<void**>(&g_original_move_window);
            } else if (strcmp(function_name, "GetTickCount") == 0) {
                hook_function = reinterpret_cast<void*>(&HookedGetTickCount);
                original_function = reinterpret_cast<void**>(&g_original_get_tick_count);
            } else if (strcmp(function_name, "Sleep") == 0) {
                hook_function = reinterpret_cast<void*>(&HookedSleep);
                original_function = reinterpret_cast<void**>(&g_original_sleep);
            } else {
                continue;
            }

            if (first_thunk->u1.Function == reinterpret_cast<ULONGLONG>(hook_function)) {
                continue;
            }

            DWORD old_protect = 0;
            if (VirtualProtect(&first_thunk->u1.Function, sizeof(LPVOID), PAGE_EXECUTE_READWRITE, &old_protect)) {
                if (original_function && !*original_function) {
                    *original_function = reinterpret_cast<void*>(first_thunk->u1.Function);
                }

                first_thunk->u1.Function = reinterpret_cast<ULONGLONG>(hook_function);
                VirtualProtect(&first_thunk->u1.Function, sizeof(LPVOID), old_protect, &old_protect);
                FlushInstructionCache(GetCurrentProcess(), &first_thunk->u1.Function, sizeof(LPVOID));
                any_patched = TRUE;
            }
        }
    }

    return any_patched;
}

BOOL PatchProcessModules() {
    if (InterlockedCompareExchange(&g_patching_imports, 1, 0) != 0) {
        return FALSE;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        InterlockedExchange(&g_patching_imports, 0);
        return FALSE;
    }

    MODULEENTRY32W module_entry{};
    module_entry.dwSize = sizeof(module_entry);

    BOOL any_patched = FALSE;
    if (Module32FirstW(snapshot, &module_entry)) {
        do {
            any_patched |= PatchImportTable(module_entry.hModule);
        } while (Module32NextW(snapshot, &module_entry));
    }

    CloseHandle(snapshot);
    InterlockedExchange(&g_patching_imports, 0);
    return any_patched;
}

void LogTaskbarModules() {
    if (!g_trace_enabled) {
        return;
    }

    HMODULE taskbar_dll = GetModuleHandleW(L"taskbar.dll");
    HMODULE taskbar_view_dll = GetModuleHandleW(L"Taskbar.View.dll");
    HMODULE explorer_extensions_dll = GetModuleHandleW(L"ExplorerExtensions.dll");

    if (g_taskbar_modules_logged &&
        taskbar_dll == g_last_taskbar_dll &&
        taskbar_view_dll == g_last_taskbar_view_dll &&
        explorer_extensions_dll == g_last_explorer_extensions_dll) {
        return;
    }

    g_taskbar_modules_logged = TRUE;
    g_last_taskbar_dll = taskbar_dll;
    g_last_taskbar_view_dll = taskbar_view_dll;
    g_last_explorer_extensions_dll = explorer_extensions_dll;

    wchar_t buffer[256]{};
    wsprintfW(buffer,
        L"Taskbar modules: taskbar.dll=%p Taskbar.View.dll=%p ExplorerExtensions.dll=%p",
        taskbar_dll,
        taskbar_view_dll,
        explorer_extensions_dll);
    LogMessage(buffer);
}

// x64 inline hook: intercept Taskbar.View slide functions
PVOID WINAPI HookedTaskbarViewSlide(PVOID param1, PVOID param2, DWORD param3, DWORD param4, DWORD param5) {
    // Signal that a taskbar slide is occurring; let existing timing hooks handle acceleration.
    const LONG current_thread_id = static_cast<LONG>(GetCurrentThreadId());
    const DWORD now = GetOriginalTickCount();
    InterlockedExchange(&g_taskbar_slide_thread_id, current_thread_id);
    g_taskbar_slide_start_tick = now;
    g_taskbar_slide_last_frame_tick = now;
    g_taskbar_slide_speedup = static_cast<DWORD>(std::max<int>(1, g_settings.show_speedup));
    g_taskbar_slide_showing = TRUE;

    LogMessage(L"HookedTaskbarViewSlide: slide animation detected, accelerating");

    if (g_original_taskbar_view_slide) {
        return g_original_taskbar_view_slide(param1, param2, param3, param4, param5);
    }
    return nullptr;
}

BOOL InstallInlineHook(PVOID target_fn) {
    if (!target_fn || g_taskbar_slide_hook.target_fn == target_fn) {
        return FALSE;
    }

    g_taskbar_slide_hook.target_fn = target_fn;
    g_taskbar_slide_hook.hook_fn = reinterpret_cast<PVOID>(&HookedTaskbarViewSlide);
    g_taskbar_slide_hook.original_size = 14;
    g_original_taskbar_view_slide = reinterpret_cast<TaskbarViewSlideFn>(target_fn);

    DWORD old_protect = 0;
    if (!VirtualProtect(target_fn, 64, PAGE_EXECUTE_READWRITE, &old_protect)) {
        LogLastError(L"InstallInlineHook: VirtualProtect failed");
        return FALSE;
    }

    memcpy(g_taskbar_slide_hook.original_bytes, target_fn, g_taskbar_slide_hook.original_size);

    BYTE* code = reinterpret_cast<BYTE*>(target_fn);
    // x64 jmp absolute: 48 B8 <addr> FF E0 (mov rax, <hook_fn>; jmp rax)
    code[0] = 0x48;
    code[1] = 0xB8;
    *reinterpret_cast<PVOID*>(&code[2]) = g_taskbar_slide_hook.hook_fn;
    code[10] = 0xFF;
    code[11] = 0xE0;
    code[12] = 0x90;
    code[13] = 0x90;

    VirtualProtect(target_fn, 64, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), target_fn, 64);

    wchar_t buffer[256]{};
    wsprintfW(buffer, L"InstallInlineHook: hooked at %p", target_fn);
    LogMessage(buffer);
    return TRUE;
}

BOOL AttemptTaskbarViewHook(HMODULE module) {
    if (!module) {
        return FALSE;
    }

    const DWORD now = GetOriginalTickCount();
    LogMessage(L"AttemptTaskbarViewHook: checking module");
    
    if (g_taskbar_view_last_attempt_tick != 0 && now - g_taskbar_view_last_attempt_tick < 30000) {
        return g_taskbar_view_hooked;
    }

    g_taskbar_view_last_attempt_tick = now;
    LogMessage(L"AttemptTaskbarViewHook: attempting to locate exported symbols");

    if (g_taskbar_view_hooked && module == g_last_taskbar_view_dll) {
        LogMessage(L"AttemptTaskbarViewHook: already hooked");
        return TRUE;
    }

    const char* candidates[] = { "SlideWindow", "TrayUI_SlideWindow", "SlideWindowInternal" };
    for (const char* name : candidates) {
        FARPROC addr = GetProcAddress(module, name);
        if (addr) {
            g_taskbar_view_slide_fn = reinterpret_cast<PVOID>(addr);

            wchar_t buffer[256]{0};
            wsprintfW(buffer, L"AttemptTaskbarViewHook: found %S at %p", name, addr);
            LogMessage(buffer);

            if (InstallInlineHook(g_taskbar_view_slide_fn)) {
                g_taskbar_view_hooked = TRUE;
                return TRUE;
            }
        }
    }

    LogMessage(L"AttemptTaskbarViewHook: Taskbar.View present but no known exported symbols found");
    g_taskbar_view_hooked = FALSE;
    return FALSE;
}

BOOL InstallProcessHooks() {
    if (InterlockedCompareExchange(&g_hooks_installed, 1, 0) != 0) {
        return TRUE;
    }

    if (g_settings_path[0] == L'\0') {
        return FALSE;
    }

    if (!LoadSettings()) {
        LogMessage(L"InstallProcessHooks: failed to load settings");
        return FALSE;
    }

    // Patch the current process first, then every explorer module loaded later will inherit the hook.
    const BOOL patched = PatchProcessModules();
    LogTaskbarModules();
    LogMessage(patched ? L"InstallProcessHooks: patch success" : L"InstallProcessHooks: patch failed");
    return patched;
}

BOOL InjectSelfIntoExplorer() {
    const DWORD explorer_pid = GetExplorerPID();
    if (!explorer_pid) {
        LogMessage(L"InjectSelfIntoExplorer: explorer.exe not found");
        return FALSE;
    }

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, explorer_pid);
    if (!process) {
        LogLastError(L"InjectSelfIntoExplorer: OpenProcess failed");
        return FALSE;
    }

    const size_t bytes = (wcslen(g_dll_path) + 1) * sizeof(wchar_t);
    LPVOID remote_memory = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_memory) {
        LogLastError(L"InjectSelfIntoExplorer: VirtualAllocEx failed");
        CloseHandle(process);
        return FALSE;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote_memory, g_dll_path, bytes, &written) || written != bytes) {
        LogLastError(L"InjectSelfIntoExplorer: WriteProcessMemory failed");
        VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        CloseHandle(process);
        return FALSE;
    }

    HANDLE remote_thread = CreateRemoteThread(
        process,
        nullptr,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")),
        remote_memory,
        0,
        nullptr);

    if (!remote_thread) {
        LogLastError(L"InjectSelfIntoExplorer: CreateRemoteThread failed");
        VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        CloseHandle(process);
        return FALSE;
    }

    WaitForSingleObject(remote_thread, INFINITE);
    DWORD remote_result = 0;
    if (GetExitCodeThread(remote_thread, &remote_result) && remote_result == 0) {
        LogMessage(L"InjectSelfIntoExplorer: remote LoadLibraryW failed");
        CloseHandle(remote_thread);
        VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        CloseHandle(process);
        SetLastError(ERROR_DLL_INIT_FAILED);
        return FALSE;
    }

    CloseHandle(remote_thread);
    VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
    CloseHandle(process);
    LogMessage(L"InjectSelfIntoExplorer: success");
    return TRUE;
}

DWORD WINAPI HookWorkerThread(LPVOID) {
    LogMessage(L"HookWorkerThread started");
    for (;;) {
        LoadSettings();
        ClearExpiredTaskbarSlide();
        if (PatchProcessModules()) {
            LogMessage(L"HookWorkerThread: patch success");
        }
        LogTaskbarModules();
        // If Taskbar.View.dll is loaded, attempt to locate internal slide functions.
        HMODULE taskbar_view = GetModuleHandleW(L"Taskbar.View.dll");
        if (taskbar_view) {
            AttemptTaskbarViewHook(taskbar_view);
        }

        Sleep(8000);
    }
    return 0;
}

}  // namespace

extern "C" __declspec(dllexport) BOOL WINAPI TaskbarAutoHideHook_Initialize(const wchar_t* settings_path) {
    if (!settings_path || !settings_path[0]) {
        return FALSE;
    }

    wcsncpy_s(g_settings_path, settings_path, _TRUNCATE);
    GetModuleFileNameW(g_self_module, g_dll_path, ARRAYSIZE(g_dll_path));
    if (GetEnvironmentVariableW(L"APPDATA", g_log_path, ARRAYSIZE(g_log_path)) > 0) {
        wcscat_s(g_log_path, L"\\TaskbarAutoHideSpeed\\hook.log");
    }
    g_trace_enabled = (GetEnvironmentVariableW(L"TASKBAR_AUTOHIDE_TRACE", nullptr, 0) > 0);
    LogMessage(L"TaskbarAutoHideHook_Initialize called");
    if (!ReloadSettings()) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }

    if (!g_worker_thread) {
        g_worker_thread = CreateThread(nullptr, 0, HookWorkerThread, nullptr, 0, nullptr);
    }

    if (!InjectSelfIntoExplorer()) {
        return FALSE;
    }

    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI TaskbarAutoHideHook_ReloadSettings() {
    if (!ReloadSettings()) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }

    return TRUE;
}

extern "C" __declspec(dllexport) void WINAPI TaskbarAutoHideHook_Shutdown() {
    InterlockedExchange(&g_hooks_installed, 0);
    g_settings = {};
    g_settings_path[0] = L'\0';
    g_dll_path[0] = L'\0';
}

BOOL WINAPI DllMain(HINSTANCE module_handle, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self_module = module_handle;
        DisableThreadLibraryCalls(module_handle);

        wchar_t appdata_path[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"APPDATA", appdata_path, ARRAYSIZE(appdata_path)) > 0) {
            wcsncpy_s(g_settings_path, appdata_path, _TRUNCATE);
            wcscat_s(g_settings_path, L"\\TaskbarAutoHideSpeed\\settings.json");
            wcsncpy_s(g_log_path, appdata_path, _TRUNCATE);
            wcscat_s(g_log_path, L"\\TaskbarAutoHideSpeed\\hook.log");
        }

        g_trace_enabled = (GetEnvironmentVariableW(L"TASKBAR_AUTOHIDE_TRACE", nullptr, 0) > 0);

        GetModuleFileNameW(module_handle, g_dll_path, ARRAYSIZE(g_dll_path));
        LogMessage(L"DllMain PROCESS_ATTACH");
        if (!g_worker_thread) {
            g_worker_thread = CreateThread(nullptr, 0, HookWorkerThread, nullptr, 0, nullptr);
        }
    }

    return TRUE;
}
