using System.Runtime.InteropServices;

namespace TaskbarAutoHideSpeed;

internal static class NativeBridge
{
    private static readonly string NativeDllPath = Path.Combine(AppContext.BaseDirectory, "TaskbarAutoHideHook.dll");

    [UnmanagedFunctionPointer(CallingConvention.Winapi, CharSet = CharSet.Unicode, SetLastError = true)]
    private delegate bool InitializeDelegate(string settingsPath);

    [UnmanagedFunctionPointer(CallingConvention.Winapi, SetLastError = true)]
    private delegate bool ReloadSettingsDelegate();

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    private delegate void ShutdownDelegate();

    private static IntPtr _moduleHandle = IntPtr.Zero;
    private static InitializeDelegate? _initialize;
    private static ReloadSettingsDelegate? _reloadSettings;
    private static ShutdownDelegate? _shutdown;
    private static string _lastError = "";

    public static string LastError => _lastError;

    public static NativeApplyResult PushSettings(string settingsPath)
    {
        if (!EnsureLoaded())
        {
            return NativeApplyResult.Failed($"Native hook DLL load failed: {_lastError}");
        }

        try
        {
            if (_initialize is null || _reloadSettings is null)
            {
                return NativeApplyResult.Failed("Native hook exports were not loaded.");
            }

            if (!_initialize.Invoke(settingsPath))
            {
                return NativeApplyResult.Failed($"Native hook initialization failed. Win32 error: {Marshal.GetLastWin32Error()}");
            }

            if (!_reloadSettings.Invoke())
            {
                return NativeApplyResult.Failed($"Native hook settings reload failed. Win32 error: {Marshal.GetLastWin32Error()}");
            }

            return NativeApplyResult.Applied();
        }
        catch (Exception ex)
        {
            return NativeApplyResult.Failed($"Native hook call failed: {ex.Message}");
        }
    }

    public static void Shutdown()
    {
        _shutdown?.Invoke();
    }

    private static bool EnsureLoaded()
    {
        if (_moduleHandle != IntPtr.Zero)
        {
            return true;
        }

        if (!File.Exists(NativeDllPath))
        {
            _lastError = $"DLL not found at: {NativeDllPath}";
            return false;
        }

        try
        {
            _moduleHandle = NativeLibrary.Load(NativeDllPath);
            _initialize = LoadDelegate<InitializeDelegate>("TaskbarAutoHideHook_Initialize");
            _reloadSettings = LoadDelegate<ReloadSettingsDelegate>("TaskbarAutoHideHook_ReloadSettings");
            _shutdown = LoadDelegate<ShutdownDelegate>("TaskbarAutoHideHook_Shutdown");
            
            if (_initialize is null || _reloadSettings is null || _shutdown is null)
            {
                _lastError = "Failed to load one or more native function exports";
                _moduleHandle = IntPtr.Zero;
                return false;
            }
            
            return true;
        }
        catch (Exception ex)
        {
            _lastError = ex.Message;
            _moduleHandle = IntPtr.Zero;
            return false;
        }
    }

    private static T? LoadDelegate<T>(string exportName) where T : Delegate
    {
        if (_moduleHandle == IntPtr.Zero)
        {
            return null;
        }

        if (!NativeLibrary.TryGetExport(_moduleHandle, exportName, out var address))
        {
            return null;
        }

        return Marshal.GetDelegateForFunctionPointer<T>(address);
    }
}

public sealed record NativeApplyResult(bool Success, string Message)
{
    public static NativeApplyResult Applied() => new(true, "Settings saved and native hook initialized.");

    public static NativeApplyResult Failed(string message) => new(false, message);
}
