using System;
using System.Collections.Generic;
using System.Linq;
using System.Management;
using Microsoft.Win32;

namespace NgsRumblePatch {
internal sealed class HardwareInfo {
    public string Cpu { get; private set; }
    public string[] Gpus { get; private set; }

    public static HardwareInfo Detect() {
        var result = new HardwareInfo { Gpus = new string[0] };
        try {
            using (var key = Registry.LocalMachine.OpenSubKey(@"HARDWARE\DESCRIPTION\System\CentralProcessor\0"))
                if (key != null) result.Cpu = (key.GetValue("ProcessorNameString") as string ?? "").Trim();
        } catch (System.Security.SecurityException) {
        } catch (UnauthorizedAccessException) {
        } catch (System.IO.IOException) {
        }
        try {
            var names = new List<string>();
            using (var search = new ManagementObjectSearcher("SELECT Name FROM Win32_VideoController")) {
                search.Options.Timeout = TimeSpan.FromSeconds(3);
                search.Options.ReturnImmediately = true;
                using (var adapters = search.Get())
                    foreach (ManagementObject adapter in adapters)
                        using (adapter) {
                            var name = adapter["Name"] as string;
                            if (!String.IsNullOrWhiteSpace(name)) names.Add(name.Trim());
                        }
            }
            result.Gpus = names.Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        } catch (ManagementException) {
        } catch (System.Runtime.InteropServices.COMException) {
        } catch (UnauthorizedAccessException) {
        }
        return result;
    }
}
}
