using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using NgsRumblePatch;

static class PluginInstallerTests {
 static void Check(bool value,string message){if(!value)throw new Exception(message);}
 static int Main(string[] args){
  try{
   var installer=Assembly.LoadFrom(args[0]);
   string[] forbidden={"CreateRemoteThread","WriteProcessMemory","VirtualAllocEx","QueueUserAPC","SetWindowsHookEx","OpenProcess"};
   foreach(var type in installer.GetTypes())foreach(var method in type.GetMethods(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance)){
    var import=(DllImportAttribute)Attribute.GetCustomAttribute(method,typeof(DllImportAttribute));
    if(import!=null)Check(!forbidden.Contains(method.Name)&&!forbidden.Contains(import.EntryPoint),"Forbidden external-process API: "+method.Name);
   }
   Check(installer.GetType("NgsRumblePatch.AutoLoader")==null,"Resident autoloader compiled into installer.");
   Check(installer.GetType("NgsRumblePatch.RuntimeBridge").GetMethod("Inject")==null,"Runtime injection entry remains.");
   Check(installer.GetType("NgsRumblePatch.AutoLifecycle").GetMethod("Enable")==null,"Autostart registration entry remains.");
   Check(!PayloadHashes.Values.ContainsKey("NGS1RumbleAuto.exe")&&Payload.ExtraNames.SequenceEqual(new[]{"NGS1CrashTrace.exe"}),"Autoloader still packaged.");
   Console.WriteLine("PASS: installer exposes no external-process injection APIs or autostart creation, and packages no autoloader.");
   string home=Path.Combine(args[1],"plugin-clean-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(home);
   File.Copy(args[2],Path.Combine(home,Payload.ExeName));
   string hash=Payload.FileHash(Path.Combine(home,Payload.ExeName));
   var features=new FeatureFacade();
   try{
    features.InstallRumble(home);features.InstallDualSense(home);features.InstallAfterimage(home);
    var info=features.Inspect(home);
    Check(info.Rumble&&info.DualSense&&info.Afterimage,"Independent feature installation failed.");
    Check(info.ConfigRoot==home&&File.Exists(Path.Combine(home,"dinput8.dll")),"Plugin not installed beside game EXE.");
    Check(!Directory.Exists(Path.Combine(home,LaunchEngine.Folder))&&!AutoLifecycle.IsRegistered(home),"New install created a runtime or startup entry.");
    features.RestoreDualSense(home);Check(!features.Inspect(home).DualSense&&features.Inspect(home).Rumble&&features.Inspect(home).Afterimage,"Independent restore changed other features.");
    features.RestoreAll(home);Check(!File.Exists(Path.Combine(home,"dinput8.dll"))&&!Directory.Exists(Path.Combine(home,Payload.StateFolder)),"Full restore left owned data.");
    Check(Payload.FileHash(Path.Combine(home,Payload.ExeName))==hash,"Game EXE modified.");
   }finally{features.RestoreAll(home);}
   Console.WriteLine("PASS: clean native install, three independent features, no Run entry, and complete restore preserve the game EXE.");
   return 0;
  }catch(Exception error){Console.Error.WriteLine(error);return 1;}
 }
}
