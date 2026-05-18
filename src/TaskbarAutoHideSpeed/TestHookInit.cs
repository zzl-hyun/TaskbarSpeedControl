// Quick test to initialize the native hook
using System;
using System.Runtime.InteropServices;
using System.IO;
using System.Text.Json;

class Program
{
    static void Main()
    {
        var appDataPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "TaskbarAutoHideSpeed");
        var settingsPath = Path.Combine(appDataPath, "settings.json");
        
        Directory.CreateDirectory(appDataPath);
        
        // Write default settings
        var settings = new { ShowSpeedup = 500, HideSpeedup = 500, FrameRate = 144, RunAtStartup = true, StartMinimized = false };
        File.WriteAllText(settingsPath, JsonSerializer.Serialize(settings, new JsonSerializerOptions { WriteIndented = true }));
        
        Console.WriteLine($"Settings written to: {settingsPath}");
        
        // Load and call native DLL
        var dllPath = "TaskbarAutoHideHook.dll";
        var hModule = LoadLibrary(dllPath);
        if (hModule == IntPtr.Zero)
        {
            Console.WriteLine($"Failed to load {dllPath}");
            return;
        }
        
        Console.WriteLine($"Loaded {dllPath} at {hModule:X}");
        
        // Get Initialize function
        var initProc = GetProcAddress(hModule, "TaskbarAutoHideHook_Initialize");
        if (initProc == IntPtr.Zero)
        {
            Console.WriteLine("Failed to find Initialize export");
            return;
        }
        
        // Call Initialize
        var initialize = Marshal.GetDelegateForFunctionPointer<InitializeDelegate>(initProc);
        var result = initialize(settingsPath);
        
        Console.WriteLine($"Initialize result: {result}");
        Console.WriteLine("Hook should be initializing...");
        
        System.Threading.Thread.Sleep(2000);
    }
    
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr LoadLibrary(string lpDllName);
    
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);
    
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    delegate bool InitializeDelegate(string settingsPath);
}
