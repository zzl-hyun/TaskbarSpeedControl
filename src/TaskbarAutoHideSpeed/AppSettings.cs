namespace TaskbarAutoHideSpeed;

public sealed class AppSettings
{
    public int ShowSpeedup { get; set; } = 250;
    public int HideSpeedup { get; set; } = 250;
    public int FrameRate { get; set; } = 90;
    public bool RunAtStartup { get; set; } = true;
    public bool StartMinimized { get; set; }
}
