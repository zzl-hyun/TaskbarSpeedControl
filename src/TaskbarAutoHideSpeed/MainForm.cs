namespace TaskbarAutoHideSpeed;

public sealed class MainForm : Form
{
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern bool ShowWindowAsync(IntPtr hWnd, int nCmdShow);

    private const int SwRestore = 9;

    private readonly SettingsStore _settingsStore;
    private readonly StartupManager _startupManager;
    private readonly TaskbarSpeedController _controller;
    private readonly AppSettings _settings;
    private readonly bool _startHidden;

    private readonly NumericUpDown _showSpeedupBox = new();
    private readonly NumericUpDown _hideSpeedupBox = new();
    private readonly NumericUpDown _frameRateBox = new();
    private readonly CheckBox _runAtStartupBox = new();
    private readonly CheckBox _startMinimizedBox = new();
    private readonly Label _statusLabel = new();
    private readonly NotifyIcon _trayIcon = new();

    private bool _allowClose;

    public MainForm(
        SettingsStore settingsStore,
        StartupManager startupManager,
        TaskbarSpeedController controller,
        AppSettings settings,
        bool startHidden)
    {
        _settingsStore = settingsStore;
        _startupManager = startupManager;
        _controller = controller;
        _settings = settings;
        _startHidden = startHidden;

        Text = "Taskbar Auto-Hide Speed";
        StartPosition = FormStartPosition.CenterScreen;
        MinimumSize = new Size(560, 360);
        Font = SystemFonts.MessageBoxFont;

        InitializeLayout();
        Load += OnLoad;
        Shown += OnShown;
        FormClosing += OnFormClosing;

        _trayIcon.Icon = SystemIcons.Application;
        _trayIcon.Text = "Taskbar Auto-Hide Speed";
        _trayIcon.Visible = true;
        _trayIcon.DoubleClick += (_, _) => RestoreFromTray();

        var menu = new ContextMenuStrip();
        menu.Items.Add("Open", null, (_, _) => RestoreFromTray());
        menu.Items.Add("Exit", null, (_, _) => ExitApplication());
        _trayIcon.ContextMenuStrip = menu;
    }

    private void InitializeLayout()
    {
        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 2,
            RowCount = 7,
            Padding = new Padding(16),
            AutoSize = true
        };

        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 55));
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 45));

        AddRow(root, 0, "Show animation speedup (%)", _showSpeedupBox, 1, 5000);
        AddRow(root, 1, "Hide animation speedup (%)", _hideSpeedupBox, 1, 5000);
        AddRow(root, 2, "Animation frame rate", _frameRateBox, 1, 240);

        _runAtStartupBox.Text = "Run at startup";
        _startMinimizedBox.Text = "Start minimized to tray";

        root.Controls.Add(_runAtStartupBox, 0, 3);
        root.SetColumnSpan(_runAtStartupBox, 2);
        root.Controls.Add(_startMinimizedBox, 0, 4);
        root.SetColumnSpan(_startMinimizedBox, 2);

        var buttonPanel = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.LeftToRight,
            AutoSize = true,
            WrapContents = false
        };

        var applyButton = new Button { Text = "Apply", AutoSize = true };
        applyButton.Click += (_, _) => ApplySettings();

        var defaultsButton = new Button { Text = "Restore defaults", AutoSize = true };
        defaultsButton.Click += (_, _) => RestoreDefaults();

        buttonPanel.Controls.Add(applyButton);
        buttonPanel.Controls.Add(defaultsButton);

        root.Controls.Add(buttonPanel, 0, 5);
        root.SetColumnSpan(buttonPanel, 2);

        _statusLabel.AutoSize = true;
        _statusLabel.Dock = DockStyle.Fill;
        _statusLabel.Text = "Settings are saved locally and applied at startup.";
        root.Controls.Add(_statusLabel, 0, 6);
        root.SetColumnSpan(_statusLabel, 2);

        Controls.Add(root);
    }

    private static void AddRow(TableLayoutPanel root, int rowIndex, string labelText, NumericUpDown box, decimal minimum, decimal maximum)
    {
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));

        var label = new Label
        {
            Text = labelText,
            AutoSize = true,
            Dock = DockStyle.Fill,
            Padding = new Padding(0, 6, 12, 0)
        };

        box.Minimum = minimum;
        box.Maximum = maximum;
        box.DecimalPlaces = 0;
        box.Width = 120;
        box.Anchor = AnchorStyles.Left;

        root.Controls.Add(label, 0, rowIndex);
        root.Controls.Add(box, 1, rowIndex);
    }

    private void OnLoad(object? sender, EventArgs e)
    {
        SyncUiFromSettings();

        if (_startHidden)
        {
            BeginInvoke(HideToTray);
            return;
        }

        ShowInTaskbar = true;
        WindowState = FormWindowState.Normal;
        StartPosition = FormStartPosition.CenterScreen;
        Show();
        BringToFront();
        Activate();
        TopMost = true;
        TopMost = false;
        CenterToScreen();

        if (_settings.StartMinimized)
        {
            _statusLabel.Text = "Start minimized is enabled, but manual launches keep the window visible. Use the tray icon when you want the app hidden at startup.";
        }
    }

    private void OnShown(object? sender, EventArgs e)
    {
        if (_startHidden)
        {
            return;
        }

        if (WindowState == FormWindowState.Minimized)
        {
            WindowState = FormWindowState.Normal;
        }

        if (!IsOnAnyVisibleScreen(Bounds))
        {
            var workingArea = Screen.PrimaryScreen?.WorkingArea ?? SystemInformation.WorkingArea;
            Location = new Point(
                Math.Max(workingArea.Left, workingArea.Left + (workingArea.Width - Width) / 2),
                Math.Max(workingArea.Top, workingArea.Top + (workingArea.Height - Height) / 2));
        }

        ShowInTaskbar = true;
        Show();
        BringToFront();
        Activate();
        SetForegroundWindow(Handle);
        ShowWindowAsync(Handle, SwRestore);
    }

    private static bool IsOnAnyVisibleScreen(Rectangle bounds)
    {
        foreach (var screen in Screen.AllScreens)
        {
            if (screen.WorkingArea.IntersectsWith(bounds))
            {
                return true;
            }
        }

        return false;
    }

    private void SyncUiFromSettings()
    {
        _showSpeedupBox.Value = _settings.ShowSpeedup;
        _hideSpeedupBox.Value = _settings.HideSpeedup;
        _frameRateBox.Value = _settings.FrameRate;
        _runAtStartupBox.Checked = _startupManager.GetIsEnabled();
        _startMinimizedBox.Checked = _settings.StartMinimized;
    }

    private void ApplySettings()
    {
        _settings.ShowSpeedup = (int)_showSpeedupBox.Value;
        _settings.HideSpeedup = (int)_hideSpeedupBox.Value;
        _settings.FrameRate = (int)_frameRateBox.Value;
        _settings.RunAtStartup = _runAtStartupBox.Checked;
        _settings.StartMinimized = _startMinimizedBox.Checked;

        var result = _controller.Apply(_settings);
        _statusLabel.Text = result.Success
            ? "Settings saved and native hook initialized. If the speed still does not change, this Windows taskbar path is not using the fallback animation hook."
            : result.Message;
    }

    private void RestoreDefaults()
    {
        var result = _controller.RestoreDefaults(_settings);
        SyncUiFromSettings();
        _statusLabel.Text = result.Success
            ? "Defaults restored and native hook initialized."
            : $"Defaults restored, but native hook failed: {result.Message}";
    }

    private void OnFormClosing(object? sender, FormClosingEventArgs e)
    {
        if (_allowClose)
        {
            _trayIcon.Visible = false;
            return;
        }

        e.Cancel = true;
        HideToTray();
    }

    private void HideToTray()
    {
        ShowInTaskbar = false;
        Hide();
        _trayIcon.Visible = true;
    }

    private void RestoreFromTray()
    {
        ShowInTaskbar = true;
        Show();
        WindowState = FormWindowState.Normal;
        Activate();
    }

    private void ExitApplication()
    {
        _allowClose = true;
        Close();
        Application.Exit();
    }
}
