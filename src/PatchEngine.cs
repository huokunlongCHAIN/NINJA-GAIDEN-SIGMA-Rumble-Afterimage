using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Diagnostics;
using System.Collections.Generic;
using System.Web.Script.Serialization;
using System.Threading;

namespace NgsRumblePatch {
public sealed class FileRecord {
 public string Name,OriginalHash,InstalledHash,PreviousInstalledHash;
 public bool OriginalExists;
 public int Attributes;
 public long LastWriteUtc;
}
public sealed class PatchState {
 public string Product="NGS1RumblePatch",Version=Payload.Version,Phase,CreatedUtc;
 public int Schema=1;
 public List<FileRecord> Files=new List<FileRecord>();
}
public sealed class Inspection {
 public string Root,Description;public bool CanInstall,CanRestore;
}
public sealed class PatchEngine {
 readonly List<PatchFile> payload;
 readonly string validationGameRoot;
 readonly JavaScriptSerializer json=new JavaScriptSerializer();
 // Used only by the separate filesystem test executable, never exposed by the installer.
 internal Action<int> BeforeWrite=null;
 internal Action<int> AfterWrite=null;
 internal Action<int> BeforeBackup=null;
 public PatchEngine(string gameRoot=null,List<PatchFile> selectedPayload=null){payload=selectedPayload??Payload.Load();validationGameRoot=gameRoot;}
 public static string Root(string input){
  if(String.IsNullOrWhiteSpace(input))throw new Exception("请先选择游戏的 EXE。");
  string root=Path.GetFullPath(input.Trim().Trim('"'));
  if(File.Exists(root)){if(!Path.GetFileName(root).Equals(Payload.ExeName,StringComparison.OrdinalIgnoreCase))throw new Exception("请选择 "+Payload.ExeName);root=Path.GetDirectoryName(root);}
  if(!Directory.Exists(root))throw new Exception("游戏目录不存在。");
  string volume=Path.GetPathRoot(root);
  if(root.Length>volume.Length)root=root.TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar);
  RejectReparse(root);return root;
 }
 static void RejectReparse(string path){if((File.Exists(path)||Directory.Exists(path))&&(File.GetAttributes(path)&FileAttributes.ReparsePoint)!=0)throw new Exception("目标包含链接或重定向，未写入："+path);}
 internal static string Child(string root,string relative){
  string p=Path.GetFullPath(Path.Combine(root,relative));
  string prefix=root.TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar)+Path.DirectorySeparatorChar;
  if(!p.StartsWith(prefix,StringComparison.OrdinalIgnoreCase))throw new Exception("补丁路径越界，未写入。");
  string current=root;
  foreach(string part in relative.Split(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar)){
   current=Path.Combine(current,part);RejectReparse(current);
  }
  return p;
 }
 static void GameStopped(string root){
  foreach(var process in Process.GetProcessesByName("NINJA GAIDEN SIGMA"))using(process){
   string path;
   try{path=process.MainModule.FileName;}
   catch(InvalidOperationException){continue;}
   catch{throw new Exception("检测到游戏进程且无法核对目录，请退出游戏后重试。");}
   if(String.Equals(Path.GetFullPath(path),Path.Combine(root,Payload.ExeName),StringComparison.OrdinalIgnoreCase))throw new Exception("请正常保存并退出这个目录的游戏，再安装或还原。");
  }
 }
 void VersionSupported(string root){
  if(validationGameRoot!=null)root=validationGameRoot;
  string path=Child(root,Payload.ExeName);
  if(!File.Exists(path))throw new Exception("此目录没有 "+Payload.ExeName);
  GameCompatibility.Validate(path);
 }
 string StatePath(string root){return Child(root,Path.Combine(Payload.StateFolder,"state.json"));}
 PatchState ReadState(string root){
  string folder=Child(root,Payload.StateFolder),path=StatePath(root);
  if(!Directory.Exists(folder))return null;
  if(!File.Exists(path))throw new Exception("发现未完成或未知的备份目录，请保留它并检查："+folder);
  if(new FileInfo(path).Length>65536)throw new Exception("安装记录异常，未写入。");
  PatchState state;
  try{state=json.Deserialize<PatchState>(File.ReadAllText(path,Encoding.UTF8));}catch{throw new Exception("安装记录损坏，请保留备份目录。");}
  if(state==null||state.Schema!=1||state.Product!="NGS1RumblePatch"||state.Files==null||state.Files.Count!=Payload.Names.Length||
     !new[]{"Prepared","Installing","Installed","Upgrading","Restoring","Restored"}.Contains(state.Phase))throw new Exception("不支持的安装记录，未写入。");
  if(!state.Files.Select(f=>f.Name).OrderBy(s=>s).SequenceEqual(Payload.Names.OrderBy(s=>s)))throw new Exception("安装记录文件列表异常，未写入。");
  foreach(var f in state.Files){
   if(!HashValid(f.InstalledHash)||(f.OriginalExists&&!HashValid(f.OriginalHash)))throw new Exception("安装记录校验值异常。");
   if(f.PreviousInstalledHash!=null&&!HashValid(f.PreviousInstalledHash))throw new Exception("升级记录校验值异常。");
   if(f.Name=="dinput8.dll"&&(!Payload.KnownCores.Contains(f.InstalledHash)||(f.OriginalExists&&!Payload.KnownCores.Contains(f.OriginalHash))))throw new Exception("安装记录中的 DLL 不属于本补丁，未写入。");
   if(f.Name=="dinput8.dll"&&f.PreviousInstalledHash!=null&&!Payload.KnownCores.Contains(f.PreviousInstalledHash))throw new Exception("升级记录中的旧 DLL 不属于本补丁，未写入。");
  }
  return state;
 }
 static bool HashValid(string hash){return hash!=null&&hash.Length==64&&hash.All(c=>Uri.IsHexDigit(c));}
 void SaveState(string root,PatchState state){
  AtomicWrite(StatePath(root),new UTF8Encoding(false).GetBytes(json.Serialize(state)));
 }
 internal static void AtomicWrite(string path,byte[] bytes){
  string temp=Path.Combine(Path.GetDirectoryName(path),".ngsr-"+Guid.NewGuid().ToString("N").Substring(0,12)+".tmp");
  using(var stream=new FileStream(temp,FileMode.CreateNew,FileAccess.Write,FileShare.None))stream.Write(bytes,0,bytes.Length);
  try{if(File.Exists(path))File.Replace(temp,path,null);else File.Move(temp,path);}
  finally{if(File.Exists(temp))File.Delete(temp);}
 }
 static void ValidateTargets(string root){
  foreach(string name in Payload.Names){string path=Child(root,name);if(Directory.Exists(path))throw new Exception("目标文件被同名目录占用："+name);}
 }
 bool InstalledExact(string root,PatchState state){return state.Files.All(f=>File.Exists(Child(root,f.Name))&&Payload.FileHash(Child(root,f.Name))==f.InstalledHash);}
 void CheckCoreConflict(string root){
  string dll=Child(root,"dinput8.dll");
  if(File.Exists(dll)&&!Payload.KnownCores.Contains(Payload.FileHash(dll)))throw new Exception("检测到其它插件的 dinput8.dll，已停止安装，不覆盖该文件。请先解决插件冲突。");
 }
 public Inspection Inspect(string input){
  var result=new Inspection();
  try{
   result.Root=Root(input);var state=ReadState(result.Root);ValidateTargets(result.Root);
   result.CanRestore=state!=null&&state.Phase!="Restored";
   VersionSupported(result.Root);CheckCoreConflict(result.Root);
   result.CanInstall=state==null||state.Phase=="Restored"||state.Phase=="Installed";
   result.Description=state==null?"版本校验通过。安装后每次开游戏自动生效。":state.Phase=="Restored"?"已还原安装前状态，可重新安装。":state.Phase=="Installed"?"检测到已安装的震动补丁 "+state.Version+"。": "上次操作尚未完成，请点击还原恢复备份。";
  }catch(Exception e){result.Description=e.Message;}
  return result;
 }
 sealed class OperationLock:IDisposable {
  Mutex mutex;
  public OperationLock(string root){
   string key=Payload.Hash(Encoding.UTF8.GetBytes(root.ToUpperInvariant())).Substring(0,24);
   mutex=new Mutex(false,"Local\\NGS1RumblePatch-"+key);bool acquired;
   try{acquired=mutex.WaitOne(0);}catch(AbandonedMutexException){acquired=true;}
   if(!acquired){mutex.Dispose();mutex=null;throw new Exception("此目录已有另一个安装或还原操作，请等待它完成。");}
  }
  public void Dispose(){if(mutex!=null){mutex.ReleaseMutex();mutex.Dispose();mutex=null;}}
 }
 public string Install(string input){string root=Root(input);using(var guard=new OperationLock(root))return InstallRoot(root);}
 string InstallRoot(string root){
  GameStopped(root);VersionSupported(root);ValidateTargets(root);CheckCoreConflict(root);
  PatchState state=ReadState(root);
  if(state!=null&&state.Phase=="Installed"){
   bool samePayload=payload.All(p=>state.Files.Single(f=>f.Name==p.Name).InstalledHash==p.Hash);
   if(state.Version!=Payload.Version||!samePayload)return Upgrade(root,state);
   if(!InstalledExact(root,state))throw new Exception("现有补丁文件或参数已改变。此版本已安装，未覆盖自定义配置。更新版本时会先备份参数再更新。");
   return "已经安装此版本，无需重复注入。";
  }
  if(state!=null&&state.Phase!="Restored")throw new Exception("上次操作未完成，请先还原。");
  if(state!=null){
   string source=Child(root,Payload.StateFolder),destination=Child(root,Payload.StateFolder+".archive-"+Guid.NewGuid().ToString("N"));
   Directory.Move(source,destination);
  }
  // All originals are saved and verified before the first game-directory write.
  string folder=Child(root,Payload.StateFolder);Directory.CreateDirectory(folder);
  string originals=Child(root,Path.Combine(Payload.StateFolder,"original"));Directory.CreateDirectory(originals);
  state=new PatchState{Phase="Prepared",CreatedUtc=DateTime.UtcNow.ToString("o")};
  try{
  int backupIndex=0;
  foreach(var file in payload){
   if(BeforeBackup!=null)BeforeBackup(backupIndex++);
   string target=Child(root,file.Name);var record=new FileRecord{Name=file.Name,InstalledHash=file.Hash,OriginalExists=File.Exists(target)};
   if(record.OriginalExists){
    record.OriginalHash=Payload.FileHash(target);record.Attributes=(int)File.GetAttributes(target);record.LastWriteUtc=File.GetLastWriteTimeUtc(target).Ticks;
    string saved=Child(root,Path.Combine(Payload.StateFolder,"original",file.Name));File.Copy(target,saved,false);
    if(Payload.FileHash(saved)!=record.OriginalHash)throw new Exception("备份校验失败，未写入补丁。");
   }
   state.Files.Add(record);
  }
  SaveState(root,state);ValidateBackups(root,state);
  }catch(Exception preparation){
   // The four target files are still untouched. Keep incomplete backup material
   // outside the active journal directory so a later retry can prepare cleanly.
   string failed=Child(root,Payload.StateFolder+".backup-failed-"+Guid.NewGuid().ToString("N"));
   try{Directory.Move(folder,failed);}catch{}
   throw new Exception("备份未能完成，未写入补丁。备份材料已保留。\r\n"+preparation.Message);
  }
  try{
   state.Phase="Installing";SaveState(root,state);
   for(int i=0;i<payload.Count;++i){
    GameStopped(root);
    var file=payload[i];var original=state.Files.Single(f=>f.Name==file.Name);string target=Child(root,file.Name);
    if(File.Exists(target)!=original.OriginalExists||(original.OriginalExists&&Payload.FileHash(target)!=original.OriginalHash))throw new Exception("安装期间文件被其它程序修改："+file.Name);
    if(BeforeWrite!=null)BeforeWrite(i);
    AtomicWrite(target,file.Bytes);
    if(Payload.FileHash(target)!=file.Hash)throw new Exception("安装校验失败："+file.Name);
    if(AfterWrite!=null)AfterWrite(i);
   }
   state.Phase="Installed";SaveState(root,state);
   return "安装成功。以后每次启动游戏会自动加载全部震动规则，无需再次注入。\r\n安装前文件已备份，可随时使用本程序还原。";
  }catch(Exception failure){
   try{RestoreFiles(root,state);state.Phase="Restored";SaveState(root,state);}
   catch(Exception rollback){throw new Exception("安装未完成，自动还原也未完成。备份已保留，请再次点击还原。\r\n"+failure.Message+"\r\n"+rollback.Message);}
   throw new Exception("安装失败，已自动恢复安装前文件。\r\n"+failure.Message);
  }
 }
 string Upgrade(string root,PatchState previous){
  GameStopped(root);ValidateBackups(root,previous);
  var beforeHashes=new Dictionary<string,string>();
  foreach(var f in previous.Files){
   string target=Child(root,f.Name);if(!File.Exists(target))throw new Exception("升级前补丁文件缺失，请先还原再安装："+f.Name);
   string hash=Payload.FileHash(target);
   if(f.Name=="dinput8.dll"&&hash!=f.InstalledHash)throw new Exception("现有 DLL 已改变，未覆盖，请保留备份并处理冲突。");
   beforeHashes.Add(f.Name,hash);
  }
  bool edited=previous.Files.Any(f=>f.Name!="dinput8.dll"&&beforeHashes[f.Name]!=f.InstalledHash);
  string saved=Child(root,Path.Combine(Payload.StateFolder,"upgrade-"+Guid.NewGuid().ToString("N")));Directory.CreateDirectory(saved);
  foreach(var f in previous.Files){
   string target=Child(root,f.Name);if(!File.Exists(target)||Payload.FileHash(target)!=beforeHashes[f.Name])throw new Exception("升级前文件发生变化，未更新："+f.Name);
   string backup=Path.Combine(saved,f.Name);File.Copy(target,backup,false);
   if(Payload.FileHash(backup)!=beforeHashes[f.Name])throw new Exception("升级前备份校验失败，未更新："+f.Name);
  }
  AtomicWrite(Path.Combine(saved,"state-before-upgrade.json"),new UTF8Encoding(false).GetBytes(json.Serialize(previous)));
  var next=json.Deserialize<PatchState>(json.Serialize(previous));next.Version=Payload.Version;next.Phase="Upgrading";
  foreach(var f in next.Files){f.PreviousInstalledHash=beforeHashes[f.Name];f.InstalledHash=payload.Single(p=>p.Name==f.Name).Hash;}
  SaveState(root,next);
  try{
   for(int i=0;i<payload.Count;++i){
    GameStopped(root);var p=payload[i];string target=Child(root,p.Name);
    if(!File.Exists(target)||Payload.FileHash(target)!=beforeHashes[p.Name])throw new Exception("升级期间文件被其它程序修改："+p.Name);
    if(BeforeWrite!=null)BeforeWrite(i);AtomicWrite(target,p.Bytes);
    if(Payload.FileHash(target)!=p.Hash)throw new Exception("升级校验失败："+p.Name);
    if(AfterWrite!=null)AfterWrite(i);
   }
   foreach(var f in next.Files)f.PreviousInstalledHash=null;
   next.Phase="Installed";SaveState(root,next);
   return "已升级到 "+Payload.Version+"。最初安装前的备份完整保留，之后仍可一键还原。"+(edited?"\r\n自定义参数已另存至："+saved+"，当前已应用新版参数。":"");
  }catch(Exception failure){
   try{
    GameStopped(root);ValidateTargets(root);
    foreach(var f in previous.Files){
     string snapshot=Path.Combine(saved,f.Name);if(!File.Exists(snapshot)||Payload.FileHash(snapshot)!=beforeHashes[f.Name])throw new Exception("升级前快照已改变："+f.Name);
     string target=Child(root,f.Name);
     if(File.Exists(target)){string hash=Payload.FileHash(target);if(hash!=beforeHashes[f.Name]&&hash!=payload.Single(p=>p.Name==f.Name).Hash)throw new Exception("升级期间出现外部修改，请保留备份："+f.Name);}
    }
    foreach(var f in previous.Files)AtomicWrite(Child(root,f.Name),File.ReadAllBytes(Path.Combine(saved,f.Name)));
    SaveState(root,previous);
   }catch(Exception rollback){throw new Exception("升级未完成，自动回退也未完成。最初备份和升级前快照均已保留，请先退出游戏再还原。\r\n"+failure.Message+"\r\n"+rollback.Message);}
   throw new Exception("升级失败，已恢复升级前的补丁版本。最初安装前备份保留。\r\n"+failure.Message);
  }
 }
 void ValidateBackups(string root,PatchState state){
  foreach(var file in state.Files)if(file.OriginalExists){
   string saved=Child(root,Path.Combine(Payload.StateFolder,"original",file.Name));
   if(!File.Exists(saved)||Payload.FileHash(saved)!=file.OriginalHash)throw new Exception("原始备份缺失或已改变，未执行还原："+file.Name);
  }
 }
 void RestoreFiles(string root,PatchState state){
  GameStopped(root);ValidateBackups(root,state);ValidateTargets(root);
  // A later unrelated DLL must never be removed or overwritten by uninstall.
  var dll=state.Files.Single(f=>f.Name=="dinput8.dll");string dllPath=Child(root,dll.Name);
  if(File.Exists(dllPath)){
   string hash=Payload.FileHash(dllPath);
   if(hash!=dll.InstalledHash&&hash!=dll.PreviousInstalledHash&&(!dll.OriginalExists||hash!=dll.OriginalHash))throw new Exception("dinput8.dll 已被其它程序替换，未覆盖。请保留备份并先处理这个冲突。");
  }
  // Preserve later edits, including user-adjusted motor strengths.
  string changed=null;
  foreach(var file in state.Files){
   string target=Child(root,file.Name);if(!File.Exists(target))continue;string hash=Payload.FileHash(target);
   if(hash==file.InstalledHash||(file.OriginalExists&&hash==file.OriginalHash))continue;
   if(changed==null){changed=Child(root,Path.Combine(Payload.StateFolder,"changed-"+Guid.NewGuid().ToString("N")));Directory.CreateDirectory(changed);}
   File.Copy(target,Path.Combine(changed,file.Name),false);
  }
  foreach(var file in state.Files){
   GameStopped(root);
   string target=Child(root,file.Name);
   if(file.OriginalExists){
    if(!File.Exists(target)||Payload.FileHash(target)!=file.OriginalHash)AtomicWrite(target,File.ReadAllBytes(Child(root,Path.Combine(Payload.StateFolder,"original",file.Name))));
    File.SetAttributes(target,(FileAttributes)file.Attributes);File.SetLastWriteTimeUtc(target,new DateTime(file.LastWriteUtc,DateTimeKind.Utc));
   }else if(File.Exists(target)){
    // Configuration edits have already been archived above. DLL ownership was checked globally.
    File.Delete(target);
   }
  }
 }
 public string Restore(string input){string root=Root(input);using(var guard=new OperationLock(root))return RestoreRoot(root);}
 public string RestoreClean(string input){
  string root=Root(input);using(var guard=new OperationLock(root)){
   GameStopped(root);CleanUninstall.ValidateNative(root);RestoreRoot(root);CleanUninstall.DeleteNative(root);
   return "已还原安装前文件，并清理本补丁的备份和日志目录。";
  }
 }
 string RestoreRoot(string root){
  GameStopped(root);PatchState state=ReadState(root);
  if(state==null)return "此目录没有本安装器的安装记录，无需还原。";
  if(state.Phase=="Restored")return "已经还原，无需重复操作。";
  ValidateBackups(root,state);state.Phase="Restoring";SaveState(root,state);
  RestoreFiles(root,state);state.Phase="Restored";SaveState(root,state);
  return "已恢复安装前文件。原先不存在的补丁文件已移除。\r\n备份和之后修改过的配置仍保留在 "+Payload.StateFolder+" 中。";
 }
}
}
