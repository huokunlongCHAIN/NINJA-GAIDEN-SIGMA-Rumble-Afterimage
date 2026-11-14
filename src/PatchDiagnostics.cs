using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.Eventing.Reader;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Xml.Linq;

namespace NgsRumblePatch {
internal static class PatchDiagnostics {
 const int LogLimit=256*1024;
 static readonly Encoding Utf8=new UTF8Encoding(false);
 static readonly Regex NativeLogName=new Regex(@"^(autoload|NGS1Afterimage|startup-trace|quit-trace|diagnostic-\d{8}-\d{6}-\d+)\.log$",
  RegexOptions.IgnoreCase|RegexOptions.CultureInvariant);
 static readonly string LogFolder=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
  "NGS1Enhancements","Diagnostics");

 static string GameRoot(string input){
  if(String.IsNullOrWhiteSpace(input))return null;
  string path=Path.GetFullPath(input.Trim().Trim('"'));
  if(String.Equals(Path.GetFileName(path),Payload.ExeName,StringComparison.OrdinalIgnoreCase))return Path.GetDirectoryName(path);
  return path==Path.GetPathRoot(path)?path:path.TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar);
 }

 static string LogName(string root){return "installer-"+(root==null?"general":RuntimeBridge.Key(root))+".log";}

 static string TraceName(string root){return "crash-trace-"+RuntimeBridge.Key(root)+".log";}

 internal static string Trace(string input){
  string root=PatchEngine.Root(input);
  GameCompatibility.Validate(PatchEngine.Child(root,Payload.ExeName));
  using(var guard=new Mutex(false,@"Local\NGS1CrashTrace-"+RuntimeBridge.Key(root))){
   bool acquired;
   try{acquired=guard.WaitOne(0);}catch(AbandonedMutexException){acquired=true;}
   if(!acquired)throw new InvalidOperationException(InstallerLanguage.T(
    "这个游戏目录已经在追踪中。","Crash tracing is already active for this game folder."));
   try{
    TraceGame(root);
   }finally{guard.ReleaseMutex();}
  }
  return InstallerLanguage.T("本次崩溃追踪已结束。请点击「导出诊断包」。",
   "Crash tracing ended. Choose Export Diagnostics.");
 }

 static void TraceGame(string root){
  var running=RuntimeBridge.Games(root);
  try{
   if(running.Length>1)throw new InvalidOperationException(InstallerLanguage.T(
    "同一目录有多个游戏进程，请先退出多余的实例。","Close extra game instances in this folder first."));
   Directory.CreateDirectory(LogFolder);
   string trace=PatchEngine.Child(LogFolder,TraceName(root));
   string helper=PatchEngine.Child(LogFolder,".trace-"+Guid.NewGuid().ToString("N")+".exe");
   try{
    File.WriteAllBytes(helper,Payload.LoadExtra("NGS1CrashTrace.exe").Bytes);
    int pid=running.Length==1?running[0].Id:new FeatureFacade().Launch(root);
    using(var process=Process.Start(new ProcessStartInfo{
     FileName=helper,Arguments=pid.ToString(CultureInfo.InvariantCulture)+" \""+trace+"\"",
     UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=LogFolder
    })){
     if(process==null)throw new InvalidOperationException("Crash tracing helper did not start.");
     process.WaitForExit();
     if(process.ExitCode!=0)throw new InvalidOperationException(InstallerLanguage.T(
      "无法记录崩溃调用栈，诊断包会包含错误信息。","Could not capture crash frames. Export diagnostics for the error details."));
    }
   }finally{
    try{File.Delete(helper);}
    catch(IOException error){Failure("trace-helper-cleanup",root,error);}
    catch(UnauthorizedAccessException error){Failure("trace-helper-cleanup",root,error);}
   }
  }finally{foreach(var game in running)game.Dispose();}
 }

 internal static T Run<T>(string operation,string input,Func<T> action){
  Log(operation,"BEGIN",input,"");
  try{
   T result=action();
   Log(operation,"OK",input,Convert.ToString(result,CultureInfo.InvariantCulture));
   return result;
  }catch(Exception error){
   Failure(operation,input,error);
   throw;
  }
 }

 internal static void Failure(string operation,string input,Exception error){
  Log(operation,"ERROR",input,"0x"+error.HResult.ToString("X8")+" "+error);
 }

 internal static void Log(string operation,string outcome,string input,string message){
  try{
   string root=null;
   try{root=GameRoot(input);}catch(ArgumentException){}catch(NotSupportedException){}catch(IOException){}
   string name=LogName(root);
   Directory.CreateDirectory(LogFolder);
   using(var mutex=new Mutex(false,@"Local\NGS1Diagnostics-"+name)){
    bool held;
    try{held=mutex.WaitOne(250);}catch(AbandonedMutexException){held=true;}
    if(!held)return;
    try{
     string path=PatchEngine.Child(LogFolder,name);
     if(File.Exists(path)&&new FileInfo(path).Length>=LogLimit){
      string previous=PatchEngine.Child(LogFolder,name+".previous");
      if(File.Exists(previous))File.Delete(previous);
      File.Move(path,previous);
     }
     string line=DateTime.UtcNow.ToString("o",CultureInfo.InvariantCulture)+" installer="+Payload.Version+" "+
      operation+" "+outcome+" "+Redact(message??"",root);
     line=line.Replace("\r","").Replace("\n"," | ");
     if(line.Length>8000)line=line.Substring(0,8000);
     File.AppendAllText(path,line+Environment.NewLine,Utf8);
    }finally{mutex.ReleaseMutex();}
   }
  }catch(Exception){} // Logging must not change the result of the installer operation.
 }

 static string Redact(string text,string root){
  foreach(var item in new[]{
    new {Path=root,Name="<GAME>"},
    new {Path=Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),Name="<USERPROFILE>"}
  }){
   if(String.IsNullOrWhiteSpace(item.Path)||item.Path==Path.GetPathRoot(item.Path))continue;
   string value=item.Path.TrimEnd('\\','/');
   text=Regex.Replace(text,Regex.Escape(value),m=>item.Name,RegexOptions.IgnoreCase|RegexOptions.CultureInvariant);
   text=Regex.Replace(text,Regex.Escape(value.Replace('\\','/')),m=>item.Name,RegexOptions.IgnoreCase|RegexOptions.CultureInvariant);
  }
  return Regex.Replace(text,@"(?i)([a-z]:[\\/]Users[\\/])[^\\/\r\n]+(?=[\\/])","<USERPROFILE>");
 }

 static void AddText(ZipArchive zip,string name,string text){
  using(var writer=new StreamWriter(zip.CreateEntry(name,CompressionLevel.Optimal).Open(),Utf8))writer.Write(text);
 }

 static string Tail(string path){
  using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete)){
   bool truncated=file.Length>LogLimit;
   if(truncated)file.Seek(-LogLimit,SeekOrigin.End);
   byte[] bytes=new byte[LogLimit];
   int count=0;
   while(count<bytes.Length){
    int read=file.Read(bytes,count,bytes.Length-count);
    if(read==0)break;
    count+=read;
   }
   string text=Utf8.GetString(bytes,0,count);
   if(!truncated)return text;
   int firstLine=text.IndexOf('\n');
   return "[Earlier lines omitted]\r\n"+(firstLine>=0?text.Substring(firstLine+1):text);
  }
 }

 static void AddLog(ZipArchive zip,string entry,string path,string root,StringBuilder report){
  try{if(File.Exists(path))AddText(zip,entry,Redact(Tail(path),root));}
  catch(Exception error){report.AppendLine(entry+" unavailable: "+error.GetType().Name+" 0x"+error.HResult.ToString("X8"));}
 }

 static string CrashEvents(string game){
  var output=new StringBuilder("Application Error events for Ninja Gaiden Sigma in the last 14 days.\r\n");
  if(game==null)return output.AppendLine("Select a game to include Windows crash events.").ToString();
  try{
   var query=new EventLogQuery("Application",PathType.LogName,
    "*[System[Provider[@Name='Application Error'] and EventID=1000 and TimeCreated[timediff(@SystemTime) <= 1209600000]]]")
    {ReverseDirection=true};
   int matched=0;
   using(var reader=new EventLogReader(query)){
    for(int scanned=0;scanned<600&&matched<8;scanned++){
     using(var entry=reader.ReadEvent(TimeSpan.FromMilliseconds(500))){
      if(entry==null)break;
      var data=XDocument.Parse(entry.ToXml()).Descendants().Where(x=>x.Name.LocalName=="Data").ToList();
      Func<string,string> field=name=>data.Where(x=>(string)x.Attribute("Name")==name).Select(x=>x.Value).FirstOrDefault()??"";
      string app=field("AppName");
      if(String.IsNullOrEmpty(app)&&data.Count>0)app=data[0].Value;
      if(!String.Equals(app,Payload.ExeName,StringComparison.OrdinalIgnoreCase))continue;
      string path=field("AppPath");
      if(path.Length>0&&!String.Equals(path,game,StringComparison.OrdinalIgnoreCase))continue;
      output.AppendLine(String.Format(CultureInfo.InvariantCulture,
       "{0:o} EventID=1000 module={1} exception={2} offset={3} pid={4} match={5}",
       entry.TimeCreated.HasValue?entry.TimeCreated.Value.ToUniversalTime():(DateTime?)null,
       Path.GetFileName(field("ModuleName")),field("ExceptionCode"),field("FaultingOffset"),
       field("ProcessId"),path.Length>0?"game-path":"exe-name"));
      matched++;
     }
    }
   }
   if(matched==0)output.AppendLine("No matching crash event was found among the latest application errors.");
  }catch(Exception error){
   output.AppendLine("Event log unavailable: "+error.GetType().Name+" 0x"+error.HResult.ToString("X8"));
  }
  return output.ToString();
 }

 internal static void Export(string input,string output){
  string root=GameRoot(input);
  string full=Path.GetFullPath(output);
  if(!String.Equals(Path.GetExtension(full),".zip",StringComparison.OrdinalIgnoreCase))
   throw new ArgumentException(InstallerLanguage.T("请选择 ZIP 文件。","Choose a ZIP file."));
  string temp=Path.Combine(Path.GetDirectoryName(full),".ngs1-report-"+Guid.NewGuid().ToString("N")+".tmp");
  try{
   using(var stream=new FileStream(temp,FileMode.CreateNew))
   using(var zip=new ZipArchive(stream,ZipArchiveMode.Create)){
    var report=new StringBuilder();
    report.AppendLine("NGS1 Enhancements diagnostics");
    report.AppendLine("Created UTC: "+DateTime.UtcNow.ToString("o"));
    report.AppendLine("Installer: "+Payload.Version+"; bundled core: "+Payload.Core);
    report.AppendLine("Windows: "+Environment.OSVersion.Version+"; 64-bit OS: "+Environment.Is64BitOperatingSystem);
    foreach(var scope in (root==null?new[]{(string)null}:new[]{(string)null,root})){
     string name=LogName(scope),label=scope==null?"installer-general.log":"installer-game.log";
     AddLog(zip,label,PatchEngine.Child(LogFolder,name),root,report);
     AddLog(zip,label+".previous",PatchEngine.Child(LogFolder,name+".previous"),root,report);
    }
    if(root!=null)AddLog(zip,"game-crash-stack.log",PatchEngine.Child(LogFolder,TraceName(root)),root,report);
    string game=null;
    if(root!=null){
     try{
      string verifiedRoot=PatchEngine.Root(root);
      game=PatchEngine.Child(verifiedRoot,Payload.ExeName);
      var info=new FeatureFacade().Inspect(root);
      var exe=GameCompatibility.Inspect(game);
      report.AppendLine("Compatibility: "+exe.Reason+" ["+exe.Family+"]");
      report.AppendLine("Installer status: "+info.Description);
      report.AppendLine("Enabled: rumble="+info.Rumble+", DualSense="+info.DualSense+", afterimages="+info.Afterimage);
      if(File.Exists(game)){
       report.AppendLine("EXE file version: "+FileVersionInfo.GetVersionInfo(game).FileVersion);
       report.AppendLine("EXE SHA-256: "+RuntimeBridge.Hash(game));
      }
      if(!String.IsNullOrEmpty(info.ConfigRoot)){
       string journal=PatchEngine.Child(info.ConfigRoot,Path.Combine(Payload.StateFolder,"state.json"));
       if(File.Exists(journal)&&new FileInfo(journal).Length<=65536){
        var state=new System.Web.Script.Serialization.JavaScriptSerializer()
         .Deserialize<PatchState>(File.ReadAllText(journal));
        if(state!=null)report.AppendLine("Installed patch: "+state.Version+"; phase: "+state.Phase);
       }
       string core=PatchEngine.Child(info.ConfigRoot,"dinput8.dll");
       if(File.Exists(core))report.AppendLine("Installed core SHA-256: "+RuntimeBridge.Hash(core));
      }
     }catch(Exception error){report.AppendLine("Game inspection unavailable: "+error.Message);}
     if(game!=null)foreach(string relative in new[]{Path.Combine(LaunchEngine.Folder,"NGS1Rumble"),"NGS1Rumble"}){
      try{
       string folder=PatchEngine.Child(root,relative);
       if(!Directory.Exists(folder))continue;
       string prefix=relative=="NGS1Rumble"?"game-runtime/":"compat-runtime/";
       var files=new DirectoryInfo(folder).EnumerateFiles("*.log")
        .Where(x=>NativeLogName.IsMatch(x.Name)&&(x.Attributes&FileAttributes.ReparsePoint)==0)
        .OrderByDescending(x=>x.LastWriteTimeUtc);
       foreach(var log in files.Where(x=>!x.Name.StartsWith("diagnostic-",StringComparison.OrdinalIgnoreCase))
        .Concat(files.Where(x=>x.Name.StartsWith("diagnostic-",StringComparison.OrdinalIgnoreCase)).Take(4)))
        AddLog(zip,prefix+log.Name,log.FullName,root,report);
      }catch(Exception error){report.AppendLine("Runtime logs unavailable: "+error.GetType().Name);}
     }
    }
    AddText(zip,"windows-crashes.txt",Redact(CrashEvents(game),root));
    AddText(zip,"report.txt",Redact(report.ToString(),root));
   }
   if(File.Exists(full))File.Replace(temp,full,null);else File.Move(temp,full);
  }finally{if(File.Exists(temp))File.Delete(temp);}
 }
}
}
