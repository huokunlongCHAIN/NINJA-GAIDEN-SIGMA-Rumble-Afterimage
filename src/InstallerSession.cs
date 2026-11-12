using System;
using System.IO;
using System.Text;
using System.Linq;
using System.Diagnostics;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace NgsRumblePatch {
// If somebody runs this installer from the folder being uninstalled, relocate
// this session first so Windows does not keep that folder's EXE mapped/locked.
static class InstallerSession {
 static string Quote(string value){var text=new StringBuilder("\"");int slashes=0;foreach(char c in value){if(c=='\\'){++slashes;continue;}if(c=='\"'){text.Append('\\',slashes*2+1);text.Append(c);}else{text.Append('\\',slashes);text.Append(c);}slashes=0;}text.Append('\\',slashes*2);return text.Append('"').ToString();}
 public static bool Initialize(ref string[] args){
  string self=Assembly.GetExecutingAssembly().Location;
  if(args.Length>=4&&args[0]=="--session-parent"&&args[2]=="--session-source"){
   int parent;if(!Int32.TryParse(args[1],out parent))throw new Exception("临时安装会话参数无效。");
   try{using(var process=Process.GetProcessById(parent))if(!process.WaitForExit(15000))throw new Exception("旧安装窗口尚未退出，未清理运行目录。");}catch(ArgumentException){}
   string source=Path.GetFullPath(args[3]),folder=Path.GetDirectoryName(source),root=Path.GetDirectoryName(folder);
   if(!String.Equals(Path.GetFileName(folder),LaunchEngine.Folder,StringComparison.OrdinalIgnoreCase)||!File.Exists(Path.Combine(root,Payload.ExeName))||!File.Exists(Path.Combine(folder,"launch-state.json"))||
      (File.GetAttributes(folder)&FileAttributes.ReparsePoint)!=0||(File.Exists(source)&&((File.GetAttributes(source)&FileAttributes.ReparsePoint)!=0||Payload.FileHash(source)!=Payload.FileHash(self))))throw new Exception("临时安装会话归属检查失败，未删除文件。");
   if(File.Exists(source))File.Delete(source);args=args.Skip(4).ToArray();ScheduleTemporaryRemoval(self);return false;
  }
  string directory=Path.GetDirectoryName(self),gameRoot=Path.GetDirectoryName(directory);
  if(!String.Equals(Path.GetFileName(directory),LaunchEngine.Folder,StringComparison.OrdinalIgnoreCase)||!File.Exists(Path.Combine(gameRoot,Payload.ExeName)))return false;
  string tempRoot=Path.GetFullPath(Path.GetTempPath()),copy=Path.Combine(tempRoot,"NGS1RumbleInstaller_"+Guid.NewGuid().ToString("N")+".exe");
  if(!Path.GetFullPath(copy).StartsWith(tempRoot.TrimEnd(Path.DirectorySeparatorChar)+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))throw new Exception("临时安装路径越界。");
  File.Copy(self,copy,false);
  string[] forward=args.Length==0?new[]{"--ui",gameRoot}:args;
  var parameters=new[]{"--session-parent",Process.GetCurrentProcess().Id.ToString(),"--session-source",self}.Concat(forward);
  Process.Start(new ProcessStartInfo{FileName=copy,Arguments=String.Join(" ",parameters.Select(Quote)),UseShellExecute=false,CreateNoWindow=true}).Dispose();return true;
 }
 static void ScheduleTemporaryRemoval(string self){
  string temp=Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar)+Path.DirectorySeparatorChar;
  if(!Path.GetFullPath(self).StartsWith(temp,StringComparison.OrdinalIgnoreCase)||!Regex.IsMatch(Path.GetFileName(self),@"^NGS1RumbleInstaller_[0-9a-f]{32}\.exe$",RegexOptions.IgnoreCase))throw new Exception("临时安装会话路径异常。");
  string pathData=Convert.ToBase64String(Encoding.UTF8.GetBytes(self)),prefixData=Convert.ToBase64String(Encoding.UTF8.GetBytes(temp));
  // Use framework APIs only. A parent PowerShell 7 environment may otherwise
  // make Windows PowerShell's module autoloading pick incompatible cmdlets.
  string script="$removalPath=[IO.Path]::GetFullPath([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('"+pathData+"')));$removalPrefix=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('"+prefixData+"'));try{$sessionProcess=[Diagnostics.Process]::GetProcessById("+Process.GetCurrentProcess().Id+");$sessionProcess.WaitForExit();$sessionProcess.Dispose()}catch{};if($removalPath.StartsWith($removalPrefix,[StringComparison]::OrdinalIgnoreCase)){for($removalAttempt=0;$removalAttempt -lt 100;$removalAttempt++){if(-not [IO.File]::Exists($removalPath)){break};try{if(([IO.File]::GetAttributes($removalPath) -band 1024) -ne 0){break};$removalSha=[Security.Cryptography.SHA256]::Create();try{$removalDigest=[BitConverter]::ToString($removalSha.ComputeHash([IO.File]::ReadAllBytes($removalPath))).Replace('-','')}finally{$removalSha.Dispose()};if($removalDigest -ne '"+Payload.FileHash(self)+"'){break};[IO.File]::Delete($removalPath)}catch{};[Threading.Thread]::Sleep(100)}}";
  Process.Start(new ProcessStartInfo{FileName=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),@"WindowsPowerShell\v1.0\powershell.exe"),Arguments="-NoProfile -NonInteractive -EncodedCommand "+Convert.ToBase64String(Encoding.Unicode.GetBytes(script)),UseShellExecute=false,CreateNoWindow=true,WindowStyle=ProcessWindowStyle.Hidden}).Dispose();
 }
}
}
