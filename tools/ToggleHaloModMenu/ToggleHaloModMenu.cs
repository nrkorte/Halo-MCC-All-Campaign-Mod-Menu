// Toggles HaloModMenu (version.dll) on/off for Halo MCC.
// OFF: renames version.dll to version.dll.disabled so the game ignores it (safe to launch with anti-cheat).
// ON:  copies the latest build from the HaloModMenu project (BuildInfo.ProjectDir). If that build
//      is missing, renames version.dll.disabled back instead.
// The MCC folder is found through Steam's library list.
using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Forms;
using Microsoft.Win32;

static class ToggleHaloModMenu
{
    const string MccRelative = @"steamapps\common\Halo The Master Chief Collection\mcc\binaries\win64";
    const string Title       = "HaloModMenu Toggle";

    [STAThread]
    static int Main()
    {
        string gameDir = FindGameDir();
        if (gameDir == null)
            return Fail("Could not find Halo MCC in any Steam library.");

        string active   = Path.Combine(gameDir, "version.dll");
        string disabled = Path.Combine(gameDir, "version.dll.disabled");
        string buildDll = Path.Combine(BuildInfo.ProjectDir, @"build\Release\version.dll");

        if (Process.GetProcessesByName("MCC-Win64-Shipping").Length > 0)
            return Fail("Halo MCC is running. Close it first, then run this again.");

        try
        {
            if (File.Exists(active))
            {
                if (!IsHaloModMenu(active))
                    return Fail("The version.dll in the game folder isn't HaloModMenu, so it was left alone:\n" + active);

                if (File.Exists(disabled)) File.Delete(disabled);
                File.Move(active, disabled);
                return Info("HaloModMenu DISABLED\n\nLaunch MCC normally (anti-cheat OK).");
            }
            if (File.Exists(buildDll))
            {
                File.Copy(buildDll, active);
                if (File.Exists(disabled)) File.Delete(disabled);
                return Info("HaloModMenu ENABLED (latest build, " + File.GetLastWriteTime(buildDll).ToString("g") + ")\n\n" +
                            "Launch MCC with anti-cheat OFF.\nOpen the menu with F6 or LT + right stick click.");
            }
            if (File.Exists(disabled))
            {
                File.Move(disabled, active);
                return Info("HaloModMenu ENABLED (previous install; no build found at " + buildDll + ")\n\n" +
                            "Launch MCC with anti-cheat OFF.\nOpen the menu with F6 or LT + right stick click.");
            }
            return Fail("HaloModMenu is not installed and no build was found at:\n" + buildDll + "\n\nRun build.bat first.");
        }
        catch (Exception e)
        {
            return Fail("Failed: " + e.Message);
        }
    }

    // Our DLL contains the UTF-16 name of its log file; anything else named version.dll isn't ours.
    static bool IsHaloModMenu(string path)
    {
        byte[] marker = Encoding.Unicode.GetBytes("HaloModMenu.log");
        byte[] data = File.ReadAllBytes(path);
        for (int i = 0; i <= data.Length - marker.Length; i++)
        {
            int j = 0;
            while (j < marker.Length && data[i + j] == marker[j]) j++;
            if (j == marker.Length) return true;
        }
        return false;
    }

    // Reads Steam's install path from the registry, then checks every library in libraryfolders.vdf.
    static string FindGameDir()
    {
        string steam = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath", null) as string;
        if (steam == null) return null;

        string vdf = Path.Combine(steam, @"steamapps\libraryfolders.vdf");
        string[] libraries = { steam };
        if (File.Exists(vdf))
        {
            MatchCollection matches = Regex.Matches(File.ReadAllText(vdf), "\"path\"\\s+\"([^\"]+)\"");
            libraries = new string[matches.Count + 1];
            libraries[0] = steam;
            for (int i = 0; i < matches.Count; i++)
                libraries[i + 1] = matches[i].Groups[1].Value.Replace(@"\\", @"\");
        }

        foreach (string library in libraries)
        {
            string dir = Path.Combine(library, MccRelative);
            if (Directory.Exists(dir)) return dir;
        }
        return null;
    }

    static int Info(string msg)
    {
        MessageBox.Show(msg, Title, MessageBoxButtons.OK, MessageBoxIcon.Information);
        return 0;
    }

    static int Fail(string msg)
    {
        MessageBox.Show(msg, Title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
        return 1;
    }
}
