namespace TaskbarAutoHideSpeed;

public sealed class TaskbarSpeedController
{
    private readonly SettingsStore _settingsStore;
    private readonly StartupManager _startupManager;

    public TaskbarSpeedController(SettingsStore settingsStore, StartupManager startupManager)
    {
        _settingsStore = settingsStore;
        _startupManager = startupManager;
    }

    public NativeApplyResult Apply(AppSettings settings)
    {
        settings.ShowSpeedup = Math.Max(1, settings.ShowSpeedup);
        settings.HideSpeedup = Math.Max(1, settings.HideSpeedup);
        settings.FrameRate = Math.Clamp(settings.FrameRate, 1, 240);

        _settingsStore.Save(settings);
        _startupManager.SetEnabled(settings.RunAtStartup);
        return NativeBridge.PushSettings(_settingsStore.SettingsPath);
    }

    public bool IsNativeHookReady()
    {
        return File.Exists(Path.Combine(AppContext.BaseDirectory, "TaskbarAutoHideHook.dll"));
    }

    public NativeApplyResult RestoreDefaults(AppSettings settings)
    {
        settings.ShowSpeedup = 250;
        settings.HideSpeedup = 250;
        settings.FrameRate = 90;
        settings.RunAtStartup = true;
        settings.StartMinimized = false;
        return Apply(settings);
    }
}
