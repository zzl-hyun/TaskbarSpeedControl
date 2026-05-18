namespace TaskbarAutoHideSpeed;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        var logPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "TaskbarAutoHideSpeed",
            "startup.log");

        void Log(string message)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(logPath)!);
            File.AppendAllText(logPath, $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] {message}{Environment.NewLine}");
        }

        try
        {
            Application.ThreadException += (_, exceptionArgs) => Log($"ThreadException: {exceptionArgs.Exception}");
            AppDomain.CurrentDomain.UnhandledException += (_, exceptionArgs) => Log($"UnhandledException: {exceptionArgs.ExceptionObject}");

            Log("Main entered");
            ApplicationConfiguration.Initialize();

            using var singleInstance = new SingleInstance("TaskbarAutoHideSpeed_2E7A90E9_6E6A_4A19_B3A1_7B0A7D3F4B8C");
            if (!singleInstance.TryEnter())
            {
                Log("Another instance is already running");
                MessageBox.Show(
                    "Taskbar Auto-Hide Speed is already running.",
                    "Taskbar Auto-Hide Speed",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information);
                return;
            }

            var settingsStore = new SettingsStore();
            var settings = settingsStore.Load();
            var startupManager = new StartupManager();
            var controller = new TaskbarSpeedController(settingsStore, startupManager);

            var startHidden = args.Any(arg => string.Equals(arg, "--autostart", StringComparison.OrdinalIgnoreCase));
            Log($"Starting message loop; startHidden={startHidden}");

            Application.Run(new MainForm(settingsStore, startupManager, controller, settings, startHidden));
            Log("Application.Run returned");
        }
        catch (Exception exception)
        {
            Log($"Fatal exception: {exception}");
            MessageBox.Show(exception.ToString(), "Taskbar Auto-Hide Speed startup failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }
}
