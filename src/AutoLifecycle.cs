using System;
using System.IO;
using System.Diagnostics;
using System.Threading;
using Microsoft.Win32;

namespace NgsRumblePatch {
// Legacy cleanup only. This build cannot register or start an autoloader.
public static class AutoLifecycle {
 public const string FileName="NGS1RumbleAuto.exe",RunPath=@"Software\Microsoft\Windows\CurrentVersion\Run";
 public static string Executable(string root){return Path.Combine(root,LaunchEngine.Folder,FileName);}
 public static string Entry(string root){return "NGS1Rumble_"+RuntimeBridge.Key(root);}
 public static string Command(string root){return "\""+Executable(root)+"\" --watch \""+root+"\"";}
 public static bool IsRegistered(string root){using(var run=Registry.CurrentUser.OpenSubKey(RunPath)){return run!=null&&String.Equals(run.GetValue(Entry(root)) as string,Command(root),StringComparison.OrdinalIgnoreCase);}}
 public static void RequireGameStopped(string root){var games=RuntimeBridge.Games(root);try{if(games.Length>0)throw new Exception(InstallerLanguage.T("请正常保存并退出这个目录的游戏，再安装、更新或还原补丁。","Save and exit this game before installing, updating, or restoring."));}finally{foreach(var game in games)game.Dispose();}}
 public static void Disable(string root){using(var run=Registry.CurrentUser.OpenSubKey(RunPath,true)){if(run==null)return;object value=run.GetValue(Entry(root));if(value is string&&String.Equals((string)value,Command(root),StringComparison.OrdinalIgnoreCase))run.DeleteValue(Entry(root),false);}}
 public static void Stop(string root){
  try{using(var signal=EventWaitHandle.OpenExisting("Local\\NGS1RumbleAutoStop-"+RuntimeBridge.Key(root)))signal.Set();}catch(WaitHandleCannotBeOpenedException){}
  foreach(var process in Process.GetProcessesByName("NGS1RumbleAuto"))using(process){
   try{if(!String.Equals(process.MainModule.FileName,Executable(root),StringComparison.OrdinalIgnoreCase))continue;if(!process.WaitForExit(15000))throw new Exception(InstallerLanguage.T("旧自动加载器尚未停止，未继续更新。","The legacy autoloader is still stopping. Update was stopped."));}
   catch(InvalidOperationException){}
  }
 }
}
}
