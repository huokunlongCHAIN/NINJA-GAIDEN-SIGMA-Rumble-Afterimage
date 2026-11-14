using System;
using System.IO;
using System.Diagnostics;
using System.Threading;
using Microsoft.Win32;
using System.Runtime.InteropServices;
using System.Text;

namespace NgsRumblePatch {
public static class AutoLifecycle {
 public const string FileName="NGS1RumbleAuto.exe",RunPath=@"Software\Microsoft\Windows\CurrentVersion\Run";
 public static string Executable(string root){return Path.Combine(root,LaunchEngine.Folder,FileName);}
 public static string Entry(string root){return "NGS1Rumble_"+RuntimeBridge.Key(root);}
 public static string Command(string root){return "\""+Executable(root)+"\" --watch \""+root+"\"";}
 public static bool IsRegistered(string root){using(var run=Registry.CurrentUser.OpenSubKey(RunPath)){return run!=null&&String.Equals(run.GetValue(Entry(root)) as string,Command(root),StringComparison.OrdinalIgnoreCase);}}
 public static void RequireGameStopped(string root){var games=RuntimeBridge.Games(root);try{if(games.Length>0)throw new Exception(InstallerLanguage.T("请正常保存并退出这个目录的游戏，再安装、更新或还原补丁。", "Save and exit the game in this folder before installing, updating, or restoring the patch."));}finally{foreach(var game in games)game.Dispose();}}
 public static void ValidateRegistration(string root){using(var run=Registry.CurrentUser.OpenSubKey(RunPath)){if(run==null)return;object value=run.GetValue(Entry(root));if(value!=null&&(!(value is string)||!String.Equals((string)value,Command(root),StringComparison.OrdinalIgnoreCase)))throw new Exception(InstallerLanguage.T("同名登录启动项不属于本补丁，未覆盖。", "A login startup entry with the same name belongs to another application. It was left intact."));}}
 public static void Enable(string root){ValidateRegistration(root);using(var run=Registry.CurrentUser.CreateSubKey(RunPath))run.SetValue(Entry(root),Command(root),RegistryValueKind.String);Start(root);}
 public static void Disable(string root){using(var run=Registry.CurrentUser.OpenSubKey(RunPath,true)){if(run==null)return;object value=run.GetValue(Entry(root));if(value is string&&String.Equals((string)value,Command(root),StringComparison.OrdinalIgnoreCase))run.DeleteValue(Entry(root),false);}}
 public static void Stop(string root){
  try{using(var signal=EventWaitHandle.OpenExisting("Local\\NGS1RumbleAutoStop-"+RuntimeBridge.Key(root)))signal.Set();}catch(WaitHandleCannotBeOpenedException){}
  foreach(var process in Process.GetProcessesByName("NGS1RumbleAuto"))using(process){
   try{if(!String.Equals(ProcessImage(process),Executable(root),StringComparison.OrdinalIgnoreCase))continue;if(!process.WaitForExit(15000))throw new Exception(InstallerLanguage.T("自动加载器尚未停止，请稍后再更新或还原。", "The autoloader is still stopping. Try updating or restoring again shortly."));}
   catch(InvalidOperationException){}
  }
 }
 static string ProcessImage(Process process){
  IntPtr handle=OpenProcess(0x1000,false,process.Id);
  if(handle==IntPtr.Zero){if(process.HasExited)return null;throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());}
  try{var text=new StringBuilder(32768);int length=text.Capacity;if(QueryFullProcessImageNameW(handle,0,text,ref length))return text.ToString();if(process.HasExited)return null;throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());}finally{CloseHandle(handle);}
 }
 [DllImport("kernel32.dll",SetLastError=true)]static extern IntPtr OpenProcess(uint access,bool inherit,int pid);
 [DllImport("kernel32.dll",SetLastError=true,CharSet=CharSet.Unicode)]static extern bool QueryFullProcessImageNameW(IntPtr process,uint flags,StringBuilder path,ref int length);
 [DllImport("kernel32.dll")]static extern bool CloseHandle(IntPtr handle);
 public static void Start(string root,string expectedHash=null){
  if(!File.Exists(Executable(root))||RuntimeBridge.Hash(Executable(root))!=(expectedHash??PayloadHashes.Values[FileName]))throw new Exception(InstallerLanguage.T("自动加载器缺失或版本不匹配，请安装／更新补丁。", "The autoloader is missing or the version differs. Install or update the patch."));
  string eventName="Local\\NGS1RumbleAutoReady-"+RuntimeBridge.Key(root);
  try{using(var existing=EventWaitHandle.OpenExisting(eventName))if(existing.WaitOne(0))return;}catch(WaitHandleCannotBeOpenedException){}
  using(var process=Process.Start(new ProcessStartInfo{FileName=Executable(root),Arguments="--watch \""+root+"\"",WorkingDirectory=root,UseShellExecute=true,WindowStyle=ProcessWindowStyle.Hidden})){
   var timer=Stopwatch.StartNew();while(timer.ElapsedMilliseconds<7000){if(process.HasExited&&process.ExitCode!=0)throw new Exception(InstallerLanguage.T("自动加载器未能启动，退出码 ", "The autoloader failed to start. Exit code ")+process.ExitCode);try{using(var ready=EventWaitHandle.OpenExisting(eventName))if(ready.WaitOne(0))return;}catch(WaitHandleCannotBeOpenedException){}Thread.Sleep(50);}
  }
  throw new Exception(InstallerLanguage.T("自动加载器尚未就绪，请检查运行目录中的 autoload.log。", "The autoloader is still starting. Check autoload.log in the runtime folder."));
 }
 public static void WritePayload(string root,LaunchState state){
  string target=Executable(root);
  if(File.Exists(target)){string hash=RuntimeBridge.Hash(target);if(hash!=state.AutoLoaderHash&&hash!=state.PreviousAutoLoaderHash&&hash!=PayloadHashes.Values[FileName])throw new Exception(InstallerLanguage.T("自动加载器已被其它文件替换，未覆盖。", "The autoloader was replaced by another file. It was left intact."));}
  PatchEngine.AtomicWrite(target,Payload.LoadExtra(FileName).Bytes);
 }
}
}
