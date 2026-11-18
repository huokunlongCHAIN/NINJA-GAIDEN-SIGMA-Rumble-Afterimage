using System;
using System.IO;
using System.Text;
using System.Linq;
using System.Diagnostics;
using System.Threading;
using System.Web.Script.Serialization;

namespace NgsRumblePatch {
public sealed class LaunchState {
 public string Product="NGS1RumbleLauncher",GameRoot,Phase,LauncherHash,Version,AutoLoaderHash,PreviousAutoLoaderHash;
 public bool AutoEnabled;
 public int Schema=1;
}
public sealed class PatchFacade {
 public Inspection Inspect(string input){
  var result=new PatchEngine().Inspect(input);
  if(String.IsNullOrEmpty(result.Root))return result;
  try{
   result.CanRestore=result.CanRestore||LaunchEngine.HasState(result.Root)||CleanUninstall.HasNativeData(result.Root)||AutoLifecycle.IsRegistered(result.Root);
   if(result.CanInstall)result.Description+=InstallerLanguage.T(" 纯插件测试版：由游戏自身加载，不使用常驻注入器。"," Native plugin test: loaded by the game; no resident injector.");
  }catch(Exception error){result.CanInstall=false;result.Description=error.Message;}
  return result;
 }
 public string Install(string input){return LaunchEngine.Install(PatchEngine.Root(input));}
 public string Prepare(string input){return Install(input);}
 public int Launch(string input){return LaunchEngine.Start(PatchEngine.Root(input));}
 public string Restore(string input){
  string root=PatchEngine.Root(input);AutoLifecycle.RequireGameStopped(root);
  AutoLifecycle.Stop(root);AutoLifecycle.Disable(root);
  if(LaunchEngine.HasState(root))LaunchEngine.RestoreLegacy(root);
  return new PatchEngine().RestoreClean(root);
 }
}
public static class LaunchEngine {
 public const string Folder="NGS1RumbleRuntime",Shortcut="NINJA GAIDEN SIGMA - 震动版.lnk";
 static readonly JavaScriptSerializer json=new JavaScriptSerializer();
 static string Runtime(string root){return PatchEngine.Child(root,Folder);}
 static string Marker(string root){return PatchEngine.Child(root,Path.Combine(Folder,"launch-state.json"));}
 static bool ValidHash(string value){return value!=null&&value.Length==64&&value.All(Uri.IsHexDigit);}
 static LaunchState Read(string root){
  string directory=Runtime(root);if(!Directory.Exists(directory))return null;
  string marker=Marker(root);
  if(!File.Exists(marker)||new FileInfo(marker).Length>16384)throw new IOException(InstallerLanguage.T("旧运行目录缺少有效记录，未覆盖。","The legacy runtime has no valid record and was left intact."));
  var state=json.Deserialize<LaunchState>(File.ReadAllText(marker,Encoding.UTF8));
  if(state==null||state.Product!="NGS1RumbleLauncher"||state.Schema!=1||!String.Equals(state.GameRoot,root,StringComparison.OrdinalIgnoreCase)||
   !new[]{"Preparing","Installed","Restored"}.Contains(state.Phase)||!ValidHash(state.LauncherHash)||
   (state.AutoLoaderHash!=null&&!ValidHash(state.AutoLoaderHash))||(state.PreviousAutoLoaderHash!=null&&!ValidHash(state.PreviousAutoLoaderHash)))
   throw new IOException(InstallerLanguage.T("旧运行库归属记录无效，未改动。","The legacy runtime ownership record is invalid and was left intact."));
  return state;
 }
 static bool NativeJournal(string root){
  string file=PatchEngine.Child(root,Path.Combine(Payload.StateFolder,"state.json"));
  if(!File.Exists(file))return false;
  var state=json.Deserialize<PatchState>(File.ReadAllText(file));
  return state!=null&&state.Product=="NGS1RumblePatch"&&state.Schema==1&&state.Phase=="Installed";
 }
 public static bool HasState(string root){return Read(root)!=null;}
 public static string ConfigRoot(string root){return NativeJournal(root)||!HasState(root)?root:Runtime(root);}
 public static void RequirePlugin(string root){
  string dll=PatchEngine.Child(root,"dinput8.dll");
  if(!NativeJournal(root)||!File.Exists(dll)||!Payload.KnownCores.Contains(Payload.FileHash(dll)))
   throw new InvalidOperationException(InstallerLanguage.T("请先安装／更新纯插件版，再启动游戏。","Install or update the native plugin before starting the game."));
 }
 public static string Install(string input,System.Collections.Generic.List<PatchFile> selectedPayload=null){
  string root=PatchEngine.Root(input);AutoLifecycle.RequireGameStopped(root);Payload.ValidateAll();
  var engine=new PatchEngine(null,selectedPayload);var inspection=engine.Inspect(root);
  if(!inspection.CanInstall)throw new InvalidOperationException(inspection.Description);
  var legacy=Read(root);
  if(legacy!=null)CleanUninstall.ValidateRuntime(root,legacy.LauncherHash,legacy.AutoLoaderHash,legacy.PreviousAutoLoaderHash);
  // Stop and remove only an owned legacy registration; never create a new one.
  AutoLifecycle.Stop(root);AutoLifecycle.Disable(root);
  string message=engine.Install(root);
  if(legacy!=null)RestoreLegacy(root);
  return InstallerLanguage.T("纯插件已安装。直接打开游戏 EXE 即可测试；未注册常驻自动加载器。\r\n","Native plugin installed. Open the game EXE to test it. No resident autoloader was registered.\r\n")+message;
 }
 public static void RestoreLegacy(string root){
  var state=Read(root);if(state==null)return;
  AutoLifecycle.RequireGameStopped(root);
  CleanUninstall.ValidateRuntime(root,state.LauncherHash,state.AutoLoaderHash,state.PreviousAutoLoaderHash);
  AutoLifecycle.Stop(root);AutoLifecycle.Disable(root);
  new PatchEngine(root).Restore(Runtime(root));
  CleanUninstall.DeleteRuntime(root,state.LauncherHash,state.AutoLoaderHash,state.PreviousAutoLoaderHash);
 }
 public static int Start(string root){
  GameCompatibility.Validate(Path.Combine(root,Payload.ExeName));RequirePlugin(root);
  var games=RuntimeBridge.Games(root);try{if(games.Length>0)return games[0].Id;}finally{foreach(var game in games)game.Dispose();}
  using(var process=Process.Start(new ProcessStartInfo{FileName=Path.Combine(root,Payload.ExeName),WorkingDirectory=root,UseShellExecute=true}))return process.Id;
 }
}
}
