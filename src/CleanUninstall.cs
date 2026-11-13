using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace NgsRumblePatch {
// Only directories owned by this product are eligible. Every file is checked
// before any deletion; links and unrelated files are never traversed or removed.
public static class CleanUninstall {
 sealed class Entry {public string Path,Relative,Hash;public bool Directory;}
 static readonly Regex BackupName=new Regex(@"^\.NGS1RumblePatch(?:\.(?:archive|backup-failed)-[0-9a-f]{32})?$",RegexOptions.IgnoreCase);
 static readonly Regex SnapshotName=new Regex(@"^(?:changed|upgrade)-[0-9a-f]{32}$",RegexOptions.IgnoreCase);
 static readonly Regex LogName=new Regex(@"^(?:diagnostic-\d{8}-\d{6}-\d+\.log|startup-trace\.log|quit-trace\.log|autoload\.log|NGS1Afterimage\.log)$",RegexOptions.IgnoreCase);
 static bool Hash(string value){return value!=null&&value.Length==64&&value.All(Uri.IsHexDigit);}
 static void NoLink(string path){if((File.GetAttributes(path)&FileAttributes.ReparsePoint)!=0)throw new Exception(InstallerLanguage.T("清理路径包含链接，未删除：", "Cleanup path contains a link. Skipped: ")+path);}
 static string Target(string root,string name){
  root=System.IO.Path.GetFullPath(root);if(root.Length>System.IO.Path.GetPathRoot(root).Length)root=root.TrimEnd(System.IO.Path.DirectorySeparatorChar);
  string target=System.IO.Path.GetFullPath(System.IO.Path.Combine(root,name));
  if(!target.StartsWith(root.TrimEnd(System.IO.Path.DirectorySeparatorChar)+System.IO.Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)||!String.Equals(System.IO.Path.GetDirectoryName(target),root,StringComparison.OrdinalIgnoreCase))throw new Exception(InstallerLanguage.T("清理目标越界，未删除。", "The cleanup target is outside the permitted folder. No files were removed."));
  if(Directory.Exists(root))NoLink(root);return target;
 }
 static List<Entry> Tree(string path){
  var entries=new List<Entry>();if(!Directory.Exists(path))return entries;NoLink(path);
  var pending=new Stack<string>();pending.Push(path);
  while(pending.Count>0){string directory=pending.Pop();NoLink(directory);
   foreach(string child in Directory.GetFileSystemEntries(directory)){
    NoLink(child);string full=System.IO.Path.GetFullPath(child);
    if(!full.StartsWith(path+System.IO.Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))throw new Exception(InstallerLanguage.T("清理路径越界，未删除。", "A cleanup path is outside the permitted folder. No files were removed."));
    bool folder=Directory.Exists(child);entries.Add(new Entry{Path=full,Relative=full.Substring(path.Length+1),Directory=folder,Hash=folder?null:Payload.FileHash(full)});
    if(folder)pending.Push(full);
   }
  }return entries;
 }
 static void Journal(string path){
  if(new FileInfo(path).Length>65536)throw new Exception(InstallerLanguage.T("备份记录异常，未清理。", "The backup record is invalid. No files were removed."));
  PatchState state;try{state=new JavaScriptSerializer().Deserialize<PatchState>(File.ReadAllText(path));}catch{throw new Exception(InstallerLanguage.T("备份记录无法读取，未清理。", "The backup record could not be read. No files were removed."));}
  if(state==null||state.Product!="NGS1RumblePatch"||state.Schema!=1||state.Files==null||state.Files.Count!=Payload.Names.Length||
   !state.Files.Select(f=>f.Name).OrderBy(s=>s).SequenceEqual(Payload.Names.OrderBy(s=>s)))throw new Exception(InstallerLanguage.T("目录不属于本补丁，未清理：", "Folder belongs to another application. Skipped: ")+path);
  foreach(var file in state.Files){
   if(!Hash(file.InstalledHash)||(file.OriginalExists&&!Hash(file.OriginalHash))||(file.PreviousInstalledHash!=null&&!Hash(file.PreviousInstalledHash)))throw new Exception(InstallerLanguage.T("备份记录校验值异常，未清理。", "The backup record contains an invalid checksum. No files were removed."));
   if(file.Name=="dinput8.dll"&&(!Payload.KnownCores.Contains(file.InstalledHash)||(file.OriginalExists&&!Payload.KnownCores.Contains(file.OriginalHash))||(file.PreviousInstalledHash!=null&&!Payload.KnownCores.Contains(file.PreviousInstalledHash))))throw new Exception(InstallerLanguage.T("备份 DLL 记录不属于本项目，未清理。", "The backup refers to an unrecognized DLL. No files were removed."));
  }
 }
 static bool BackupEntry(Entry entry,string relative){
  string[] parts=relative.Split(System.IO.Path.DirectorySeparatorChar);
  if(parts.Length==1){if(entry.Directory)return parts[0]=="original"||SnapshotName.IsMatch(parts[0]);if(parts[0]=="state.json"){Journal(entry.Path);return true;}return false;}
  if(parts.Length!=2||entry.Directory)return false;
  if(parts[0]!="original"&&!SnapshotName.IsMatch(parts[0]))return false;
  if(parts[1]=="state-before-upgrade.json"&&parts[0].StartsWith("upgrade-",StringComparison.OrdinalIgnoreCase)){Journal(entry.Path);return true;}
  if(!Payload.Names.Contains(parts[1]))return false;
  return parts[1]!="dinput8.dll"||Payload.KnownCores.Contains(entry.Hash);
 }
 static List<Entry> ValidateBackup(string path){
  var entries=Tree(path);string state=System.IO.Path.Combine(path,"state.json");
  if(File.Exists(state))Journal(state);else if(!System.IO.Path.GetFileName(path).Contains(".backup-failed-"))throw new Exception(InstallerLanguage.T("备份目录缺少归属记录，未清理：", "The backup folder is missing its ownership record. Skipped: ")+path);
  foreach(var entry in entries)if(!BackupEntry(entry,entry.Relative))throw new Exception(InstallerLanguage.T("备份目录中有不属于补丁的文件，未删除：", "The backup folder contains an unrelated file. Skipped: ")+entry.Path);
  return entries;
 }
 static List<Entry> ValidateLogs(string path){
  var entries=Tree(path);foreach(var entry in entries)if(entry.Directory||!LogName.IsMatch(entry.Relative))throw new Exception(InstallerLanguage.T("日志目录中有其它文件，未删除：", "The log folder contains an unrelated file. Skipped: ")+entry.Path);return entries;
 }
 static void Delete(string path,List<Entry> entries){
  if(!Directory.Exists(path))return;NoLink(path);
  // Recheck the entire snapshot before removing the first file.
  var current=Tree(path);if(current.Count!=entries.Count||entries.Any(e=>!current.Any(c=>c.Relative==e.Relative&&c.Directory==e.Directory&&c.Hash==e.Hash)))throw new Exception(InstallerLanguage.T("清理期间文件发生变化，未删除：", "A file changed during cleanup. Skipped: ")+path);
  // Keep each directory's ownership record until its children have gone. A
  // temporary file lock therefore leaves enough information for a safe retry.
  var directories=entries.Where(e=>e.Directory).Select(e=>e.Path).OrderByDescending(p=>p.Length).Concat(new[]{path});
  foreach(string directory in directories){
   NoLink(directory);
   foreach(var entry in entries.Where(e=>!e.Directory&&System.IO.Path.GetDirectoryName(e.Path)==directory).OrderBy(e=>e.Path.EndsWith("state.json",StringComparison.OrdinalIgnoreCase)?1:0)){
    NoLink(entry.Path);if(Payload.FileHash(entry.Path)!=entry.Hash)throw new Exception(InstallerLanguage.T("文件已改变，停止清理：", "Cleanup stopped because a file changed: ")+entry.Path);File.SetAttributes(entry.Path,FileAttributes.Normal);File.Delete(entry.Path);
   }
   Directory.Delete(directory,false);
  }
 }
 static string[] Backups(string root){return Directory.GetDirectories(root,".NGS1RumblePatch*").Where(p=>BackupName.IsMatch(System.IO.Path.GetFileName(p))).OrderBy(p=>System.IO.Path.GetFileName(p)==Payload.StateFolder?1:0).ToArray();}
 public static bool HasNativeData(string root){return Backups(root).Length>0||Directory.Exists(Target(root,"NGS1Rumble"));}
 public static void ValidateNative(string root){foreach(string path in Backups(root))ValidateBackup(Target(root,System.IO.Path.GetFileName(path)));string logs=Target(root,"NGS1Rumble");if(Directory.Exists(logs))ValidateLogs(logs);}
 public static void DeleteNative(string root){
  ValidateNative(root);string logs=Target(root,"NGS1Rumble");if(Directory.Exists(logs))Delete(logs,ValidateLogs(logs));
  foreach(string directory in Backups(root)){string path=Target(root,System.IO.Path.GetFileName(directory));Delete(path,ValidateBackup(path));}
 }
 static List<Entry> RuntimeEntries(string root,string launcherHash,string autoHash,string previousAutoHash){
  string path=Target(root,LaunchEngine.Folder);var entries=Tree(path);
  foreach(var entry in entries){string[] parts=entry.Relative.Split(System.IO.Path.DirectorySeparatorChar);bool valid=false;
   if(parts.Length==1){
    if(entry.Directory)valid=BackupName.IsMatch(parts[0])||parts[0]=="NGS1Rumble";
    else if(parts[0]=="launch-state.json")valid=true;
    else if(parts[0]=="NGS1RumblePatch.exe")valid=entry.Hash.Equals(launcherHash,StringComparison.OrdinalIgnoreCase)||entry.Hash==Payload.FileHash(System.Reflection.Assembly.GetExecutingAssembly().Location);
    else if(parts[0]==AutoLifecycle.FileName)valid=entry.Hash==autoHash||entry.Hash==previousAutoHash||entry.Hash==PayloadHashes.Values[AutoLifecycle.FileName];
    else if(Payload.Names.Contains(parts[0]))valid=parts[0]!="dinput8.dll"||Payload.KnownCores.Contains(entry.Hash);
   }else if(BackupName.IsMatch(parts[0]))valid=BackupEntry(entry,entry.Relative.Substring(parts[0].Length+1));
   else if(parts[0]=="NGS1Rumble")valid=parts.Length==2&&!entry.Directory&&LogName.IsMatch(parts[1]);
   if(!valid)throw new Exception(InstallerLanguage.T("运行目录中有不属于补丁的文件，未删除：", "The runtime folder contains an unrelated file. Skipped: ")+entry.Path);
  }
  foreach(string directory in Directory.GetDirectories(path).Where(p=>BackupName.IsMatch(System.IO.Path.GetFileName(p))))ValidateBackup(directory);
  return entries;
 }
 public static void ValidateRuntime(string root,string launcherHash,string autoHash=null,string previousAutoHash=null){RuntimeEntries(root,launcherHash,autoHash,previousAutoHash);}
 public static void DeleteRuntime(string root,string launcherHash,string autoHash=null,string previousAutoHash=null){string path=Target(root,LaunchEngine.Folder);Delete(path,RuntimeEntries(root,launcherHash,autoHash,previousAutoHash));}
}
}
