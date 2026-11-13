using System;
using System.IO;
using System.Diagnostics;
using System.Threading;
using System.Reflection;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace NgsRumblePatch {
static class AutoLoader {
 sealed class Attempt:IDisposable {
  public long StartTicks;
  public DateTime Next;
  public bool Done,CoreReady;
  public int Count;
  public SafeWaitHandle Handle;
  public void Dispose(){if(Handle!=null)Handle.Dispose();}
 }
 static string log;
 static void Log(string message){
  try{
   if(File.Exists(log)&&new FileInfo(log).Length>=512*1024)File.WriteAllText(log,"[Earlier lines omitted]\r\n");
   File.AppendAllText(log,DateTime.UtcNow.ToString("o")+" "+message+"\r\n");
  }catch{}
 }
 static void LogExit(int pid,Attempt attempt){
  uint code;
  string status=attempt.Handle!=null&&!attempt.Handle.IsInvalid&&
   WaitForSingleObject(attempt.Handle,0)==0&&GetExitCodeProcess(attempt.Handle,out code)
   ?"0x"+code.ToString("X8"):"unavailable";
  long uptime=Math.Max(0,(DateTime.UtcNow.Ticks-attempt.StartTicks)/TimeSpan.TicksPerMillisecond);
  Log("GAME_EXITED pid="+pid+" exit_code="+status+" elapsed_ms="+uptime+
   " core_ready="+(attempt.CoreReady?1:0)+" load_attempts="+attempt.Count);
 }
 [STAThread]static int Main(string[] args){
  InstallerLanguage.Load();
  try{
   if(args.Length!=2||args[0]!="--watch")return 2;
   string root=Path.GetFullPath(args[1]).TrimEnd(Path.DirectorySeparatorChar),folder=Path.Combine(root,"NGS1RumbleRuntime"),self=Assembly.GetExecutingAssembly().Location;
   if(!String.Equals(Path.GetDirectoryName(self),folder,StringComparison.OrdinalIgnoreCase)||(File.GetAttributes(root)&FileAttributes.ReparsePoint)!=0||(File.GetAttributes(folder)&FileAttributes.ReparsePoint)!=0)return 3;
   string key=RuntimeBridge.Key(root);bool created;
   using(var mutex=new Mutex(true,"Local\\NGS1RumbleAuto-"+key,out created)){
    if(!created)return 0;
    using(var stop=new EventWaitHandle(false,EventResetMode.ManualReset,"Local\\NGS1RumbleAutoStop-"+key))using(var ready=new EventWaitHandle(false,EventResetMode.ManualReset,"Local\\NGS1RumbleAutoReady-"+key)){
     stop.Reset();ready.Reset();
     string logs=Path.Combine(folder,"NGS1Rumble");
     Directory.CreateDirectory(logs);
     if((File.GetAttributes(logs)&FileAttributes.ReparsePoint)!=0)return 3;
     log=Path.Combine(logs,"autoload.log");
     if(File.Exists(log)&&(File.GetAttributes(log)&FileAttributes.ReparsePoint)!=0){log=null;return 3;}
     Log("AUTOLOADER_START version="+AutoLoaderConstants.Version+" pid="+Process.GetCurrentProcess().Id);
     GameCompatibility.Validate(Path.Combine(root,"NINJA GAIDEN SIGMA.exe"));
     string core=Path.Combine(folder,"dinput8.dll");
     if((File.GetAttributes(core)&FileAttributes.ReparsePoint)!=0||RuntimeBridge.Hash(core)!=AutoLoaderConstants.CoreHash){
      Log("CORE_HASH_MISMATCH");return 4;
     }
     Log("AUTOLOADER_READY");
     ready.Set();
     var attempts=new Dictionary<int,Attempt>();
     try{
     while(!stop.WaitOne(500)){
      var seen=new HashSet<int>();
      foreach(var game in RuntimeBridge.Games(root))using(game){
       try{
        int pid=game.Id;seen.Add(pid);long ticks=game.StartTime.ToUniversalTime().Ticks;Attempt attempt;
        if(!attempts.TryGetValue(pid,out attempt)||attempt.StartTicks!=ticks){
         if(attempt!=null){LogExit(pid,attempt);attempt.Dispose();}
         attempt=new Attempt{StartTicks=ticks,Next=new DateTime(ticks,DateTimeKind.Utc).AddSeconds(8),
          Handle=OpenProcess(0x101000,false,pid)}; // SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION
         attempts[pid]=attempt;Log("GAME_DETECTED pid="+pid);
        }
        if(attempt.Done||DateTime.UtcNow<attempt.Next)continue;
        if(RuntimeBridge.Ready(pid)){attempt.Done=attempt.CoreReady=true;Log("CORE_ALREADY_READY pid="+pid);continue;}
        if(attempt.Count>=3){attempt.Done=true;Log("LOAD_RETRIES_STOPPED pid="+pid);continue;}
        ++attempt.Count;
        GameCompatibility.Validate(Path.Combine(root,"NINJA GAIDEN SIGMA.exe"));if(RuntimeBridge.Hash(core)!=AutoLoaderConstants.CoreHash)throw new Exception("Core changed; restart the matching autoloader after update.");
        RuntimeBridge.Inject(game,core);var timer=Stopwatch.StartNew();
        while(timer.ElapsedMilliseconds<10000&&!game.HasExited&&!RuntimeBridge.Ready(pid))if(stop.WaitOne(100))break;
        if(RuntimeBridge.Ready(pid)){attempt.Done=attempt.CoreReady=true;Log("AUTO_INJECT_READY pid="+pid);}else{attempt.Next=DateTime.UtcNow.AddSeconds(5);Log("CORE_NOT_READY pid="+pid);}
       }catch(Exception error){Log("LOAD_ERROR pid="+game.Id+" hr=0x"+error.HResult.ToString("X8")+" "+error.Message);Attempt failed;if(attempts.TryGetValue(game.Id,out failed))failed.Next=DateTime.UtcNow.AddSeconds(5);}
      }
      foreach(int pid in new List<int>(attempts.Keys))if(!seen.Contains(pid)){
       var attempt=attempts[pid];
       if(attempt.Handle!=null&&!attempt.Handle.IsInvalid&&WaitForSingleObject(attempt.Handle,0)==258)continue;
       LogExit(pid,attempt);attempt.Dispose();attempts.Remove(pid);
      }
     }
     }finally{
      foreach(var attempt in attempts.Values)attempt.Dispose();
      ready.Reset();Log("AUTOLOADER_STOPPED");
     }
    }
    mutex.ReleaseMutex();
   }
   return 0;
  }catch(Exception error){Log("AUTOLOADER_ERROR "+error.Message);return 1;}
 }
 [DllImport("kernel32.dll")]static extern SafeWaitHandle OpenProcess(uint access,bool inherit,int pid);
 [DllImport("kernel32.dll")]static extern uint WaitForSingleObject(SafeWaitHandle handle,uint milliseconds);
 [DllImport("kernel32.dll")]static extern bool GetExitCodeProcess(SafeWaitHandle process,out uint code);
}
}
