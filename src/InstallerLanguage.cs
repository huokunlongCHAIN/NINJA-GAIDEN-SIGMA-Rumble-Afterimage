using System;
using System.Globalization;
using System.IO;
using System.Security;
using Microsoft.Win32;

namespace NgsRumblePatch {
internal static class InstallerLanguage {
    const string SettingsKey = @"Software\NGS1Enhancements\Installer";
    public static bool English { get; private set; }

    public static string T(string chinese, string english) {
        return English ? english : chinese;
    }

    public static void Load() {
        English = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName != "zh";
        try {
            using (var key = Registry.CurrentUser.OpenSubKey(SettingsKey)) {
                string saved = key == null ? null : key.GetValue("Language") as string;
                if (saved == "en" || saved == "zh") English = saved == "en";
            }
        } catch (UnauthorizedAccessException) {
        } catch (SecurityException) {
        } catch (IOException) {
        }
    }

    public static bool Select(bool english) {
        English = english;
        try {
            using (var key = Registry.CurrentUser.CreateSubKey(SettingsKey)) {
                key.SetValue("Language", english ? "en" : "zh", RegistryValueKind.String);
            }
            return true;
        } catch (UnauthorizedAccessException) {
            return false;
        } catch (SecurityException) {
            return false;
        } catch (IOException) {
            return false;
        }
    }
}
}
