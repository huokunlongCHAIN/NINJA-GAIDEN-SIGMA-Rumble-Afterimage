using System;
using System.IO;
using System.Text;
using System.Linq;
using System.Diagnostics;
using System.Threading;
using System.Reflection;
using System.Runtime.InteropServices;
using System.IO.MemoryMappedFiles;
using System.Web.Script.Serialization;

namespace NgsRumblePatch {
public sealed class LaunchState {
 public string Product="NGS1RumbleLauncher",GameRoot,Phase,LauncherHash,Version,AutoLoaderHash,PreviousAutoLoaderHash;
 public bool AutoEnabled;
 public int Schema=1;
}
public sealed class PatchFacade {
 readonly PatchEngine native=new PatchEngine();
 public Inspection Inspect(string input){
  Inspection result=native.Inspect(input);
  if(String.IsNullOrEmpty(result.Root))return result;
  bool runtime=false;
  try{runtime=LaunchEngine.HasState(result.Root);result.CanRestore=result.CanRestore||runtime||CleanUninstall.HasNativeData(result.Root)||AutoLifecycle.IsRegistered(result.Root);}catch(Exception e){result.CanInstall=false;result.Description=e.Message;return result;}
  var compatibility=GameCompatibility.Inspect(Path.Combine(result.Root,Payload.ExeName));
  if(compatibility.Compatible&&compatibility.Family=="original-code"){
   try{
    LaunchEngine.ValidateRoot(result.Root);
    result.CanInstall=true;
    result.Description=compatibility.Reason+(runtime?" 运行库已准备，可安装或更新功能。":" 安装后直接打开游戏 EXE 即可加载。");
   }catch(Exception e){result.CanInstall=false;result.Description=e.Message;}
  }
  return result;
 }
 public string Install(string input){
  string root=PatchEngine.Root(input);var compatibility=GameCompatibility.Inspect(Path.Combine(root,Payload.ExeName));
  if(compatibility.Compatible&&compatibility.Family=="original-code")return LaunchEngine.Install(root);
  return native.Install(root);
 }
 public string Restore(string input){
  string root=PatchEngine.Root(input);AutoLifecycle.RequireGameStopped(root);string runtime=LaunchEngine.HasState(root)?LaunchEngine.Restore(root):null;
  if(runtime==null){AutoLifecycle.Stop(root);AutoLifecycle.Disable(root);}
  string normal=native.RestoreClean(root);return runtime==null?normal:runtime+"\r\n"+normal;
 }
 public int Launch(string input){return LaunchEngine.Start(PatchEngine.Root(input));}
 public string Prepare(string input){string root=PatchEngine.Root(input);var compatibility=GameCompatibility.Inspect(Path.Combine(root,Payload.ExeName));return compatibility.Compatible&&compatibility.Family=="original-code"?LaunchEngine.Install(root,false):native.Install(root);}
}
public static class LaunchEngine {
 public const string Folder="NGS1RumbleRuntime",Shortcut="NINJA GAIDEN SIGMA - 震动版.lnk";
 static readonly JavaScriptSerializer json=new JavaScriptSerializer();
 sealed class Guard:IDisposable {
  readonly Mutex mutex;public Guard(string root,string kind){mutex=new Mutex(false,"Local\\NGS1Rumble-"+kind+"-"+Payload.Hash(Encoding.UTF8.GetBytes(root.ToUpperInvariant())).Substring(0,24));bool acquired;try{acquired=mutex.WaitOne(0);}catch(AbandonedMutexException){acquired=true;}if(!acquired){mutex.Dispose();throw new Exception("此目录已有另一个兼容启动操作，请等待完成。");}}
  public void Dispose(){mutex.ReleaseMutex();mutex.Dispose();}
 }
 static string Runtime(string root){return Path.Combine(root,Folder);}
 static string Marker(string root){return Path.Combine(Runtime(root),"launch-state.json");}
 static string Launcher(string root){return Path.Combine(Runtime(root),"NGS1RumblePatch.exe");}
 static string Command(string root){return "--launch \""+root+"\"";}
 static void NoLink(string path){if((File.Exists(path)||Directory.Exists(path))&&(File.GetAttributes(path)&FileAttributes.ReparsePoint)!=0)throw new Exception("兼容启动路径包含链接，未写入："+path);}
 static bool ValidHash(string value){return value!=null&&value.Length==64&&value.All(Uri.IsHexDigit);}
 static LaunchState Read(string root){
  NoLink(Runtime(root));NoLink(Marker(root));if(!Directory.Exists(Runtime(root)))return null;
  if(!File.Exists(Marker(root))||new FileInfo(Marker(root)).Length>16384)throw new Exception("兼容运行目录已存在但缺少有效记录，未覆盖："+Runtime(root));
  LaunchState state;
  try{state=json.Deserialize<LaunchState>(File.ReadAllText(Marker(root),Encoding.UTF8));}catch{throw new Exception("兼容启动记录损坏，请保留目录。 ");}
  if(state==null||state.Schema!=1||state.Product!="NGS1RumbleLauncher"||!String.Equals(state.GameRoot,root,StringComparison.OrdinalIgnoreCase)||
   !new[]{"Preparing","Installed","Restored"}.Contains(state.Phase)||!ValidHash(state.LauncherHash))throw new Exception("兼容启动记录异常，未写入。");
  if((state.AutoLoaderHash!=null&&!ValidHash(state.AutoLoaderHash))||(state.PreviousAutoLoaderHash!=null&&!ValidHash(state.PreviousAutoLoaderHash)))throw new Exception("自动加载器记录校验异常。");
  NoLink(Launcher(root));NoLink(AutoLifecycle.Executable(root));return state;
 }
 static void Save(string root,LaunchState state){
  string path=Marker(root),temp=Path.Combine(Runtime(root),".launch-"+Guid.NewGuid().ToString("N")+".tmp");
  File.WriteAllText(temp,json.Serialize(state),new UTF8Encoding(false));try{if(File.Exists(path))File.Replace(temp,path,null);else File.Move(temp,path);}finally{if(File.Exists(temp))File.Delete(temp);}
 }
 public static bool HasState(string root){return Read(root)!=null;}
 public static void StartInstalledWatcher(string root){var state=Read(root);if(state!=null&&state.Phase=="Installed"&&state.AutoLoaderHash!=null)AutoLifecycle.Start(root,state.AutoLoaderHash);}
 public static void ValidateRoot(string root){
  GameCompatibility.Validate(Path.Combine(root,Payload.ExeName));NoLink(Runtime(root));NoLink(Path.Combine(root,Shortcut));
  if(File.Exists(Path.Combine(root,"dinput8.dll")))throw new Exception("此原版目录已有 dinput8.dll。请先还原旧补丁或处理现有插件，再安装兼容启动器。");
  var state=Read(root);
  if(state!=null&&File.Exists(Launcher(root))&&Payload.FileHash(Launcher(root))!=state.LauncherHash&&Payload.FileHash(Launcher(root))!=Payload.FileHash(Assembly.GetExecutingAssembly().Location))throw new Exception("旧版启动程序被其它文件替换，未覆盖。");
 }
 static object ShortcutObject(string path){var type=Type.GetTypeFromProgID("WScript.Shell");object shell=Activator.CreateInstance(type);try{return type.InvokeMember("CreateShortcut",BindingFlags.InvokeMethod,null,shell,new object[]{path});}finally{Marshal.FinalReleaseComObject(shell);}}
 static object Get(object value,string property){return value.GetType().InvokeMember(property,BindingFlags.GetProperty,null,value,null);}
 static bool ShortcutOwned(string root){
  object link=null;try{link=ShortcutObject(Path.Combine(root,Shortcut));return String.Equals((string)Get(link,"TargetPath"),Launcher(root),StringComparison.OrdinalIgnoreCase)&&String.Equals((string)Get(link,"Arguments"),Command(root),StringComparison.Ordinal);}
  catch{return false;}finally{if(link!=null)Marshal.FinalReleaseComObject(link);}
 }
 static void RemoveLegacyShortcut(string root){string path=Path.Combine(root,Shortcut);NoLink(path);if(File.Exists(path)&&ShortcutOwned(root))File.Delete(path);}
 static Process Game(string root){
  Process found=null;foreach(var p in Process.GetProcessesByName("NINJA GAIDEN SIGMA")){
   try{if(String.Equals(p.MainModule.FileName,Path.Combine(root,Payload.ExeName),StringComparison.OrdinalIgnoreCase)){if(found!=null){p.Dispose();found.Dispose();throw new Exception("同一目录运行了多个游戏进程，请只保留一个。");}found=p;continue;}}
   catch(InvalidOperationException){}catch(System.ComponentModel.Win32Exception){}p.Dispose();
  }return found;
 }
 static bool Ready(int pid){
  try{using(var map=MemoryMappedFile.OpenExisting("Local\\NGS1RumbleMonitor-"+pid,MemoryMappedFileRights.Read))using(var view=map.CreateViewAccessor(0,648,MemoryMappedFileAccess.Read)){
   return view.ReadUInt32(0)==0x4d52474e&&view.ReadUInt32(4)==1&&view.ReadUInt32(8)==648&&view.ReadUInt32(16)==pid&&view.ReadUInt32(20)==1;
  }}catch(FileNotFoundException){return false;}
 }
 static void RuntimeStopped(string root){using(var game=Game(root)){if(game==null)return;foreach(ProcessModule module in game.Modules)if(String.Equals(module.FileName,Path.Combine(Runtime(root),"dinput8.dll"),StringComparison.OrdinalIgnoreCase))throw new Exception("请正常退出这个目录的游戏，再更新或还原兼容运行库。");}}
 public static string Install(string input,bool enableAuto=true,System.Collections.Generic.List<PatchFile> selectedPayload=null){
  string root=PatchEngine.Root(input);using(var guard=new Guard(root,"operation"))return InstallRoot(root,enableAuto,selectedPayload);
 }
 static string InstallRoot(string root,bool enableAuto,System.Collections.Generic.List<PatchFile> selectedPayload=null){
  ValidateRoot(root);RuntimeStopped(root);
  string self=Assembly.GetExecutingAssembly().Location,hash=Payload.FileHash(self);var state=Read(root);
  if(state==null){Directory.CreateDirectory(Runtime(root));state=new LaunchState{GameRoot=root,Phase="Preparing",LauncherHash=hash};Save(root,state);}
  bool persistent=enableAuto||state.AutoEnabled;if(persistent)AutoLifecycle.ValidateRegistration(root);
  AutoLifecycle.Stop(root);var engine=new PatchEngine(root,selectedPayload);string runtimeResult;
  try{runtimeResult=engine.Install(Runtime(root));}catch{if(state.AutoEnabled&&state.AutoLoaderHash!=null)AutoLifecycle.Start(root,state.AutoLoaderHash);throw;}
  state.PreviousAutoLoaderHash=state.AutoLoaderHash;state.AutoLoaderHash=PayloadHashes.Values[AutoLifecycle.FileName];state.Phase="Preparing";Save(root,state);
  AutoLifecycle.WritePayload(root,state);
  RemoveLegacyShortcut(root);
  if(File.Exists(Launcher(root)))File.Delete(Launcher(root));
  state.LauncherHash=hash;state.Version=Payload.Version;state.PreviousAutoLoaderHash=null;state.Phase="Installed";state.AutoEnabled=persistent;Save(root,state);
  if(persistent)AutoLifecycle.Enable(root);
  return "兼容运行库已安装。"+(persistent?" 已启用当前 Windows 用户的自动加载。":" 诊断准备模式，自动加载尚未启用。")+(runtimeResult.Contains("自定义参数已另存")?"\r\n"+runtimeResult:"");
 }
 public static string Restore(string input){
  string root=PatchEngine.Root(input);using(var guard=new Guard(root,"operation"))return RestoreRoot(root);
 }
 static string RestoreRoot(string root){
  var state=Read(root);if(state==null)return "兼容模式无需还原。";RuntimeStopped(root);
  CleanUninstall.ValidateRuntime(root,state.LauncherHash,state.AutoLoaderHash,state.PreviousAutoLoaderHash);AutoLifecycle.Stop(root);AutoLifecycle.Disable(root);state.AutoEnabled=false;Save(root,state);
  var engine=new PatchEngine(root);engine.Restore(Runtime(root));
  RemoveLegacyShortcut(root);state.Phase="Restored";Save(root,state);
  CleanUninstall.DeleteRuntime(root,state.LauncherHash,state.AutoLoaderHash,state.PreviousAutoLoaderHash);
  return "兼容模式已完整还原：自动加载器已停止，登录启动项已移除，运行库、备份、日志、旧快捷方式及 "+Folder+" 文件夹均已清理。";
 }
 public static int Start(string input){
  string root=PatchEngine.Root(input);using(var guard=new Guard(root,"operation"))return StartRoot(root);
 }
 static int StartRoot(string root){
  GameCompatibility.Validate(Path.Combine(root,Payload.ExeName));
  Process process=Game(root);
  try{
  // Localized native mode can launch directly; it already loads the same bridge.
  bool native=File.Exists(Path.Combine(root,"dinput8.dll"))&&Payload.KnownCores.Contains(Payload.FileHash(Path.Combine(root,"dinput8.dll")));
  if(!native){var state=Read(root);if(state==null||state.Phase!="Installed"||state.AutoLoaderHash!=PayloadHashes.Values[AutoLifecycle.FileName])Install(root);else{
   ValidateRoot(root);var inspection=new PatchEngine(root).Inspect(Runtime(root));if(!inspection.CanRestore)throw new Exception("兼容运行库没有有效安装记录，请先安装。");
   string core=Path.Combine(Runtime(root),"dinput8.dll");if(!File.Exists(core)||Payload.FileHash(core)!=PayloadHashes.Values["dinput8.dll"])throw new Exception("兼容运行库缺失或版本已改变，请退出游戏后安装／更新。");
   AutoLifecycle.Enable(root);state.AutoEnabled=true;Save(root,state);
  }}
   if(process!=null&&Ready(process.Id))return process.Id;
   if(process==null)process=Process.Start(new ProcessStartInfo{FileName=Path.Combine(root,Payload.ExeName),WorkingDirectory=root,UseShellExecute=true});
   int pid=process.Id;try{process.WaitForInputIdle(10000);}catch(InvalidOperationException){}
   var timer=Stopwatch.StartNew();while(timer.ElapsedMilliseconds<8000){if(process.HasExited)throw new Exception("游戏在加载震动前已退出。");if(Ready(pid))return pid;Thread.Sleep(100);}
   // Original mode is injected by the persistent autoloader, including when the
   // installer is closed and the user opens the game EXE on a later run.
   timer.Restart();while(timer.ElapsedMilliseconds<10000){if(process.HasExited)throw new Exception("游戏启动后退出，请查看 NGS1Rumble 诊断日志。");if(Ready(pid))return pid;Thread.Sleep(50);}
   throw new Exception("震动核心尚未就绪，请查看诊断日志；未重复注入。");
  }finally{if(process!=null)process.Dispose();}
 }
 static void Inject(Process process,string path){
  if(process.Modules.Cast<ProcessModule>().Any(m=>String.Equals(m.FileName,path,StringComparison.OrdinalIgnoreCase)))return;
  IntPtr local=GetProcAddress(GetModuleHandle("kernel32.dll"),"LoadLibraryW"),owner;
  if(local==IntPtr.Zero||!GetModuleHandleEx(6,local,out owner))throw new Exception("无法定位系统加载器。");
  var filename=new StringBuilder(32768);GetModuleFileName(owner,filename,filename.Capacity);string name=Path.GetFileName(filename.ToString());
  ProcessModule remote=process.Modules.Cast<ProcessModule>().SingleOrDefault(m=>String.Equals(Path.GetFileName(m.FileName),name,StringComparison.OrdinalIgnoreCase));
  if(remote==null)throw new Exception("目标进程缺少匹配的系统加载器。");
  IntPtr entry=new IntPtr(remote.BaseAddress.ToInt64()+local.ToInt64()-owner.ToInt64());byte[] bytes=Encoding.Unicode.GetBytes(path+"\0");
  IntPtr handle=OpenProcess(0x43a,false,process.Id),buffer=IntPtr.Zero,thread=IntPtr.Zero;bool finished=false;
  if(handle==IntPtr.Zero)throw new Exception("无法访问游戏进程，错误 "+Marshal.GetLastWin32Error());
  try{
   buffer=VirtualAllocEx(handle,IntPtr.Zero,(UIntPtr)bytes.Length,0x3000,4);UIntPtr written;
   if(buffer==IntPtr.Zero||!WriteProcessMemory(handle,buffer,bytes,(UIntPtr)bytes.Length,out written)||written.ToUInt64()!=(ulong)bytes.Length)throw new Exception("无法传递运行库路径，错误 "+Marshal.GetLastWin32Error());
   thread=CreateRemoteThread(handle,IntPtr.Zero,UIntPtr.Zero,entry,buffer,0,IntPtr.Zero);if(thread==IntPtr.Zero)throw new Exception("无法加载震动运行库，错误 "+Marshal.GetLastWin32Error());
   finished=WaitForSingleObject(thread,10000)==0;if(!finished)throw new Exception("运行库加载超时，未重试；请退出游戏后重开。");
   process.Refresh();if(!process.Modules.Cast<ProcessModule>().Any(m=>String.Equals(m.FileName,path,StringComparison.OrdinalIgnoreCase)))throw new Exception("系统未能加载震动运行库。");
  }finally{if(thread!=IntPtr.Zero)CloseHandle(thread);if(buffer!=IntPtr.Zero&&(thread==IntPtr.Zero||finished))VirtualFreeEx(handle,buffer,UIntPtr.Zero,0x8000);CloseHandle(handle);}
 }
 [DllImport("kernel32.dll",SetLastError=true)]static extern IntPtr OpenProcess(uint access,bool inherit,int pid);
 [DllImport("kernel32.dll",SetLastError=true)]static extern IntPtr VirtualAllocEx(IntPtr process,IntPtr at,UIntPtr size,uint allocation,uint protect);
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool WriteProcessMemory(IntPtr process,IntPtr at,byte[] data,UIntPtr count,out UIntPtr written);
 [DllImport("kernel32.dll",SetLastError=true)]static extern IntPtr CreateRemoteThread(IntPtr process,IntPtr attributes,UIntPtr stack,IntPtr entry,IntPtr parameter,uint flags,IntPtr threadId);
 [DllImport("kernel32.dll")]static extern uint WaitForSingleObject(IntPtr handle,uint milliseconds);
 [DllImport("kernel32.dll")]static extern bool VirtualFreeEx(IntPtr process,IntPtr at,UIntPtr size,uint freeType);
 [DllImport("kernel32.dll")]static extern bool CloseHandle(IntPtr handle);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]static extern IntPtr GetModuleHandle(string name);
 [DllImport("kernel32.dll",CharSet=CharSet.Ansi)]static extern IntPtr GetProcAddress(IntPtr module,string name);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]static extern bool GetModuleHandleEx(uint flags,IntPtr address,out IntPtr module);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]static extern uint GetModuleFileName(IntPtr module,StringBuilder path,int count);
}
}
