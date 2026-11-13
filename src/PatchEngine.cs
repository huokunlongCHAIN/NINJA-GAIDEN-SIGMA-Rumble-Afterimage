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
  if(String.IsNullOrWhiteSpace(input))throw new Exception(InstallerLanguage.T("请先选择游戏的 EXE。", "Select the game executable first."));
  string root=Path.GetFullPath(input.Trim().Trim('"'));
  if(File.Exists(root)){if(!Path.GetFileName(root).Equals(Payload.ExeName,StringComparison.OrdinalIgnoreCase))throw new Exception(InstallerLanguage.T("请选择 ", "Select ")+Payload.ExeName);root=Path.GetDirectoryName(root);}
  if(!Directory.Exists(root))throw new Exception(InstallerLanguage.T("游戏目录不存在。", "The game folder does not exist."));
  string volume=Path.GetPathRoot(root);
  if(root.Length>volume.Length)root=root.TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar);
  RejectReparse(root);return root;
 }
 static void RejectReparse(string path){if((File.Exists(path)||Directory.Exists(path))&&(File.GetAttributes(path)&FileAttributes.ReparsePoint)!=0)throw new Exception(InstallerLanguage.T("目标包含链接或重定向，未写入：", "The target contains a link or redirect. No files were written at: ")+path);}
 internal static string Child(string root,string relative){
  string p=Path.GetFullPath(Path.Combine(root,relative));
  string prefix=root.TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar)+Path.DirectorySeparatorChar;
  if(!p.StartsWith(prefix,StringComparison.OrdinalIgnoreCase))throw new Exception(InstallerLanguage.T("补丁路径越界，未写入。", "The patch path is outside the permitted folder. No files were written."));
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
   catch{throw new Exception(InstallerLanguage.T("检测到游戏进程且无法核对目录，请退出游戏后重试。", "A game process is running from an unverified folder. Exit the game and try again."));}
   if(String.Equals(Path.GetFullPath(path),Path.Combine(root,Payload.ExeName),StringComparison.OrdinalIgnoreCase))throw new Exception(InstallerLanguage.T("请正常保存并退出这个目录的游戏，再安装或还原。", "Save and exit the game in this folder before installing or restoring the patch."));
  }
 }
 void VersionSupported(string root){
  if(validationGameRoot!=null)root=validationGameRoot;
  string path=Child(root,Payload.ExeName);
  if(!File.Exists(path))throw new Exception(InstallerLanguage.T("此目录没有 ", "This folder does not contain ")+Payload.ExeName);
  GameCompatibility.Validate(path);
 }
 string StatePath(string root){return Child(root,Path.Combine(Payload.StateFolder,"state.json"));}
 PatchState ReadState(string root){
  string folder=Child(root,Payload.StateFolder),path=StatePath(root);
  if(!Directory.Exists(folder))return null;
  if(!File.Exists(path))throw new Exception(InstallerLanguage.T("发现未完成或未知的备份目录，请保留它并检查：", "An incomplete or unrecognized backup folder was found. Keep and inspect it: ")+folder);
  if(new FileInfo(path).Length>65536)throw new Exception(InstallerLanguage.T("安装记录异常，未写入。", "The installation record is invalid. No files were written."));
  PatchState state;
  try{state=json.Deserialize<PatchState>(File.ReadAllText(path,Encoding.UTF8));}catch{throw new Exception(InstallerLanguage.T("安装记录损坏，请保留备份目录。", "The installation record is corrupt. Keep the backup folder."));}
  if(state==null||state.Schema!=1||state.Product!="NGS1RumblePatch"||state.Files==null||state.Files.Count!=Payload.Names.Length||
     !new[]{"Prepared","Installing","Installed","Upgrading","Restoring","Restored"}.Contains(state.Phase))throw new Exception(InstallerLanguage.T("不支持的安装记录，未写入。", "Unsupported installation record. No files were written."));
  if(!state.Files.Select(f=>f.Name).OrderBy(s=>s).SequenceEqual(Payload.Names.OrderBy(s=>s)))throw new Exception(InstallerLanguage.T("安装记录文件列表异常，未写入。", "The file list in the installation record is invalid. No files were written."));
  foreach(var f in state.Files){
   if(!HashValid(f.InstalledHash)||(f.OriginalExists&&!HashValid(f.OriginalHash)))throw new Exception(InstallerLanguage.T("安装记录校验值异常。", "The installation record contains an invalid checksum."));
   if(f.PreviousInstalledHash!=null&&!HashValid(f.PreviousInstalledHash))throw new Exception(InstallerLanguage.T("升级记录校验值异常。", "The upgrade record contains an invalid checksum."));
   if(f.Name=="dinput8.dll"&&(!Payload.KnownCores.Contains(f.InstalledHash)||(f.OriginalExists&&!Payload.KnownCores.Contains(f.OriginalHash))))throw new Exception(InstallerLanguage.T("安装记录中的 DLL 不属于本补丁，未写入。", "The installation record refers to an unrecognized DLL. No files were written."));
   if(f.Name=="dinput8.dll"&&f.PreviousInstalledHash!=null&&!Payload.KnownCores.Contains(f.PreviousInstalledHash))throw new Exception(InstallerLanguage.T("升级记录中的旧 DLL 不属于本补丁，未写入。", "The upgrade record refers to an unrecognized previous DLL. No files were written."));
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
  foreach(string name in Payload.Names){string path=Child(root,name);if(Directory.Exists(path))throw new Exception(InstallerLanguage.T("目标文件被同名目录占用：", "A folder has the same name as the target file: ")+name);}
 }
 bool InstalledExact(string root,PatchState state){return state.Files.All(f=>File.Exists(Child(root,f.Name))&&Payload.FileHash(Child(root,f.Name))==f.InstalledHash);}
 void CheckCoreConflict(string root){
  string dll=Child(root,"dinput8.dll");
  if(File.Exists(dll)&&!Payload.KnownCores.Contains(Payload.FileHash(dll)))throw new Exception(InstallerLanguage.T("检测到其它插件的 dinput8.dll，已停止安装，不覆盖该文件。请先解决插件冲突。", "Another plugin owns dinput8.dll. The file was left intact. Resolve the plugin conflict before installing."));
 }
 public Inspection Inspect(string input){
  var result=new Inspection();
  try{
   result.Root=Root(input);var state=ReadState(result.Root);ValidateTargets(result.Root);
   result.CanRestore=state!=null&&state.Phase!="Restored";
   VersionSupported(result.Root);CheckCoreConflict(result.Root);
   result.CanInstall=state==null||state.Phase=="Restored"||state.Phase=="Installed";
   result.Description=state==null?InstallerLanguage.T("版本校验通过。安装后每次开游戏自动生效。", "Game build verified. Installed features load automatically when the game starts."):state.Phase=="Restored"?InstallerLanguage.T("已还原安装前状态，可重新安装。", "The original files were restored. You can install the patch again."):state.Phase=="Installed"?String.Format(InstallerLanguage.T("检测到已安装的功能补丁 {0}。","Patch {0} is installed."),state.Version): InstallerLanguage.T("上次操作尚未完成，请点击还原恢复备份。", "The previous operation did not finish. Choose Restore to recover the backup.");
  }catch(Exception e){result.Description=e.Message;}
  return result;
 }
 sealed class OperationLock:IDisposable {
  Mutex mutex;
  public OperationLock(string root){
   string key=Payload.Hash(Encoding.UTF8.GetBytes(root.ToUpperInvariant())).Substring(0,24);
   mutex=new Mutex(false,"Local\\NGS1RumblePatch-"+key);bool acquired;
   try{acquired=mutex.WaitOne(0);}catch(AbandonedMutexException){acquired=true;}
   if(!acquired){mutex.Dispose();mutex=null;throw new Exception(InstallerLanguage.T("此目录已有另一个安装或还原操作，请等待它完成。", "Another install or restore operation is using this folder. Wait for it to finish."));}
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
   if(!InstalledExact(root,state))throw new Exception(InstallerLanguage.T("现有补丁文件或参数已改变。此版本已安装，未覆盖自定义配置。更新版本时会先备份参数再更新。", "This version is already installed and some patch files or settings have changed. Your changes were kept. An upgrade backs up edited settings."));
   return InstallerLanguage.T("已经安装此版本，无需重复注入。", "This version is already installed.");
  }
  if(state!=null&&state.Phase!="Restored")throw new Exception(InstallerLanguage.T("上次操作未完成，请先还原。", "The previous operation did not finish. Restore the patch first."));
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
    if(Payload.FileHash(saved)!=record.OriginalHash)throw new Exception(InstallerLanguage.T("备份校验失败，未写入补丁。", "Backup verification failed. No patch files were written."));
   }
   state.Files.Add(record);
  }
  SaveState(root,state);ValidateBackups(root,state);
  }catch(Exception preparation){
   // The four target files are still untouched. Keep incomplete backup material
   // outside the active journal directory so a later retry can prepare cleanly.
   string failed=Child(root,Payload.StateFolder+".backup-failed-"+Guid.NewGuid().ToString("N"));
   try{Directory.Move(folder,failed);}catch{}
   throw new Exception(InstallerLanguage.T("备份未能完成，未写入补丁。备份材料已保留。\r\n", "Backup creation failed. No patch files were written. The backup materials were kept.\r\n")+preparation.Message);
  }
  try{
   state.Phase="Installing";SaveState(root,state);
   for(int i=0;i<payload.Count;++i){
    GameStopped(root);
    var file=payload[i];var original=state.Files.Single(f=>f.Name==file.Name);string target=Child(root,file.Name);
    if(File.Exists(target)!=original.OriginalExists||(original.OriginalExists&&Payload.FileHash(target)!=original.OriginalHash))throw new Exception(InstallerLanguage.T("安装期间文件被其它程序修改：", "Another application changed a file during installation: ")+file.Name);
    if(BeforeWrite!=null)BeforeWrite(i);
    AtomicWrite(target,file.Bytes);
    if(Payload.FileHash(target)!=file.Hash)throw new Exception(InstallerLanguage.T("安装校验失败：", "Installation verification failed for: ")+file.Name);
    if(AfterWrite!=null)AfterWrite(i);
   }
   state.Phase="Installed";SaveState(root,state);
   return InstallerLanguage.T("安装成功。以后每次启动游戏会自动加载全部震动规则，无需再次注入。\r\n安装前文件已备份，可随时使用本程序还原。", "Installation complete. Installed features will load automatically when you launch the game.\r\nThe original files were backed up and can be restored from this installer.");
  }catch(Exception failure){
   try{RestoreFiles(root,state);state.Phase="Restored";SaveState(root,state);}
   catch(Exception rollback){throw new Exception(InstallerLanguage.T("安装未完成，自动还原也未完成。备份已保留，请再次点击还原。\r\n", "Installation and automatic rollback did not finish. The backup was kept. Choose Restore again.\r\n")+failure.Message+"\r\n"+rollback.Message);}
   throw new Exception(InstallerLanguage.T("安装失败，已自动恢复安装前文件。\r\n", "Installation failed. The original files were restored automatically.\r\n")+failure.Message);
  }
 }
 string Upgrade(string root,PatchState previous){
  GameStopped(root);ValidateBackups(root,previous);
  var beforeHashes=new Dictionary<string,string>();
  foreach(var f in previous.Files){
   string target=Child(root,f.Name);if(!File.Exists(target))throw new Exception(InstallerLanguage.T("升级前补丁文件缺失，请先还原再安装：", "A patch file is missing. Restore the previous installation first: ")+f.Name);
   string hash=Payload.FileHash(target);
   if(f.Name=="dinput8.dll"&&hash!=f.InstalledHash)throw new Exception(InstallerLanguage.T("现有 DLL 已改变，未覆盖，请保留备份并处理冲突。", "The installed DLL has changed. It was left intact. Keep the backup and resolve the conflict."));
   beforeHashes.Add(f.Name,hash);
  }
  bool edited=previous.Files.Any(f=>f.Name!="dinput8.dll"&&beforeHashes[f.Name]!=f.InstalledHash);
  string saved=Child(root,Path.Combine(Payload.StateFolder,"upgrade-"+Guid.NewGuid().ToString("N")));Directory.CreateDirectory(saved);
  foreach(var f in previous.Files){
   string target=Child(root,f.Name);if(!File.Exists(target)||Payload.FileHash(target)!=beforeHashes[f.Name])throw new Exception(InstallerLanguage.T("升级前文件发生变化，未更新：", "A file changed before the upgrade. Skipped: ")+f.Name);
   string backup=Path.Combine(saved,f.Name);File.Copy(target,backup,false);
   if(Payload.FileHash(backup)!=beforeHashes[f.Name])throw new Exception(InstallerLanguage.T("升级前备份校验失败，未更新：", "The pre-upgrade backup failed verification. Skipped: ")+f.Name);
  }
  AtomicWrite(Path.Combine(saved,"state-before-upgrade.json"),new UTF8Encoding(false).GetBytes(json.Serialize(previous)));
  var next=json.Deserialize<PatchState>(json.Serialize(previous));next.Version=Payload.Version;next.Phase="Upgrading";
  foreach(var f in next.Files){f.PreviousInstalledHash=beforeHashes[f.Name];f.InstalledHash=payload.Single(p=>p.Name==f.Name).Hash;}
  SaveState(root,next);
  try{
   for(int i=0;i<payload.Count;++i){
    GameStopped(root);var p=payload[i];string target=Child(root,p.Name);
    if(!File.Exists(target)||Payload.FileHash(target)!=beforeHashes[p.Name])throw new Exception(InstallerLanguage.T("升级期间文件被其它程序修改：", "Another application changed a file during the upgrade: ")+p.Name);
    if(BeforeWrite!=null)BeforeWrite(i);AtomicWrite(target,p.Bytes);
    if(Payload.FileHash(target)!=p.Hash)throw new Exception(InstallerLanguage.T("升级校验失败：", "Upgrade verification failed for: ")+p.Name);
    if(AfterWrite!=null)AfterWrite(i);
   }
   foreach(var f in next.Files)f.PreviousInstalledHash=null;
   next.Phase="Installed";SaveState(root,next);
   return String.Format(InstallerLanguage.T("已升级到 {0}。最初安装前的备份完整保留，之后仍可一键还原。",
    "Updated to {0}. The original backup is still available for restore."),Payload.Version)+
    (edited?String.Format(InstallerLanguage.T("\r\n自定义参数已另存至 {0}，当前已应用新版参数。",
     "\r\nCustom settings were backed up to {0}. The new configuration has been applied."),saved):"");
  }catch(Exception failure){
   try{
    GameStopped(root);ValidateTargets(root);
    foreach(var f in previous.Files){
     string snapshot=Path.Combine(saved,f.Name);if(!File.Exists(snapshot)||Payload.FileHash(snapshot)!=beforeHashes[f.Name])throw new Exception(InstallerLanguage.T("升级前快照已改变：", "The pre-upgrade snapshot has changed: ")+f.Name);
     string target=Child(root,f.Name);
     if(File.Exists(target)){string hash=Payload.FileHash(target);if(hash!=beforeHashes[f.Name]&&hash!=payload.Single(p=>p.Name==f.Name).Hash)throw new Exception(InstallerLanguage.T("升级期间出现外部修改，请保留备份：", "Another application changed a file during the upgrade. Keep this backup: ")+f.Name);}
    }
    foreach(var f in previous.Files)AtomicWrite(Child(root,f.Name),File.ReadAllBytes(Path.Combine(saved,f.Name)));
    SaveState(root,previous);
   }catch(Exception rollback){throw new Exception(InstallerLanguage.T("升级未完成，自动回退也未完成。最初备份和升级前快照均已保留，请先退出游戏再还原。\r\n", "The upgrade and automatic rollback did not finish. The original backup and pre-upgrade snapshot were kept. Exit the game before restoring.\r\n")+failure.Message+"\r\n"+rollback.Message);}
   throw new Exception(InstallerLanguage.T("升级失败，已恢复升级前的补丁版本。最初安装前备份保留。\r\n", "The upgrade failed. The previous patch version was restored and the original backup was kept.\r\n")+failure.Message);
  }
 }
 void ValidateBackups(string root,PatchState state){
  foreach(var file in state.Files)if(file.OriginalExists){
   string saved=Child(root,Path.Combine(Payload.StateFolder,"original",file.Name));
   if(!File.Exists(saved)||Payload.FileHash(saved)!=file.OriginalHash)throw new Exception(InstallerLanguage.T("原始备份缺失或已改变，未执行还原：", "The original backup is missing or has changed. Restore was stopped at: ")+file.Name);
  }
 }
 void RestoreFiles(string root,PatchState state){
  GameStopped(root);ValidateBackups(root,state);ValidateTargets(root);
  // A later unrelated DLL must never be removed or overwritten by uninstall.
  var dll=state.Files.Single(f=>f.Name=="dinput8.dll");string dllPath=Child(root,dll.Name);
  if(File.Exists(dllPath)){
   string hash=Payload.FileHash(dllPath);
   if(hash!=dll.InstalledHash&&hash!=dll.PreviousInstalledHash&&(!dll.OriginalExists||hash!=dll.OriginalHash))throw new Exception(InstallerLanguage.T("dinput8.dll 已被其它程序替换，未覆盖。请保留备份并先处理这个冲突。", "Another application replaced dinput8.dll. It was left intact. Keep the backup and resolve the conflict first."));
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
   return InstallerLanguage.T("已还原安装前文件，并清理本补丁的备份和日志目录。", "The original files were restored and the patch backup and log folders were removed.");
  }
 }
 string RestoreRoot(string root){
  GameStopped(root);PatchState state=ReadState(root);
  if(state==null)return InstallerLanguage.T("此目录没有本安装器的安装记录，无需还原。", "This folder has no installation record for this patch.");
  if(state.Phase=="Restored")return InstallerLanguage.T("已经还原，无需重复操作。", "This patch has already been restored.");
  ValidateBackups(root,state);state.Phase="Restoring";SaveState(root,state);
  RestoreFiles(root,state);state.Phase="Restored";SaveState(root,state);
  return String.Format(InstallerLanguage.T(
   "已恢复安装前文件。原先不存在的补丁文件已移除。\r\n备份和之后修改过的配置仍保留在 {0} 中。",
   "The original files were restored and added patch files were removed.\r\nBackups and edited settings are still in {0}."),Payload.StateFolder);
 }
}
}
