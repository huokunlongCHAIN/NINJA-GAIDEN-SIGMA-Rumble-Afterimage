using System;
using System.IO;
using System.Diagnostics;
using System.Threading;
using System.Reflection;
using System.Collections.Generic;

namespace NgsRumblePatch {
static class AutoLoader {
 sealed class Attempt {public long StartTicks;public DateTime Next;public bool Done;public int Count;}
 static string log;
 static void Log(string message){try{File.AppendAllText(log,DateTime.UtcNow.ToString("o")+" "+message+"\r\n");}catch{}}
 [STAThread]static int Main(string[] args){
  try{
   if(args.Length!=2||args[0]!="--watch")return 2;
   string root=Path.GetFullPath(args[1]).TrimEnd(Path.DirectorySeparatorChar),folder=Path.Combine(root,"NGS1RumbleRuntime"),self=Assembly.GetExecutingAssembly().Location;
   if(!String.Equals(Path.GetDirectoryName(self),folder,StringComparison.OrdinalIgnoreCase)||(File.GetAttributes(root)&FileAttributes.ReparsePoint)!=0||(File.GetAttributes(folder)&FileAttributes.ReparsePoint)!=0)return 3;
   GameCompatibility.Validate(Path.Combine(root,"NINJA GAIDEN SIGMA.exe"));string core=Path.Combine(folder,"dinput8.dll");
   if((File.GetAttributes(core)&FileAttributes.ReparsePoint)!=0||RuntimeBridge.Hash(core)!=AutoLoaderConstants.CoreHash)return 4;
   string key=RuntimeBridge.Key(root);bool created;
   using(var mutex=new Mutex(true,"Local\\NGS1RumbleAuto-"+key,out created)){
    if(!created)return 0;
    using(var stop=new EventWaitHandle(false,EventResetMode.ManualReset,"Local\\NGS1RumbleAutoStop-"+key))using(var ready=new EventWaitHandle(false,EventResetMode.ManualReset,"Local\\NGS1RumbleAutoReady-"+key)){
     stop.Reset();ready.Reset();string logs=Path.Combine(folder,"NGS1Rumble");Directory.CreateDirectory(logs);log=Path.Combine(logs,"autoload.log");Log("AUTOLOADER_READY version="+AutoLoaderConstants.Version+" pid="+Process.GetCurrentProcess().Id);ready.Set();
     var attempts=new Dictionary<int,Attempt>();
     while(!stop.WaitOne(500)){
      var seen=new HashSet<int>();
      foreach(var game in RuntimeBridge.Games(root))using(game){
       try{
        int pid=game.Id;seen.Add(pid);long ticks=game.StartTime.ToUniversalTime().Ticks;Attempt attempt;
        if(!attempts.TryGetValue(pid,out attempt)||attempt.StartTicks!=ticks){attempt=new Attempt{StartTicks=ticks,Next=new DateTime(ticks,DateTimeKind.Utc).AddSeconds(8)};attempts[pid]=attempt;Log("GAME_DETECTED pid="+pid);}
        if(attempt.Done||DateTime.UtcNow<attempt.Next)continue;
        if(RuntimeBridge.Ready(pid)){attempt.Done=true;Log("CORE_ALREADY_READY pid="+pid);continue;}
        if(attempt.Count++>=3){attempt.Done=true;Log("LOAD_RETRIES_STOPPED pid="+pid);continue;}
        GameCompatibility.Validate(Path.Combine(root,"NINJA GAIDEN SIGMA.exe"));if(RuntimeBridge.Hash(core)!=AutoLoaderConstants.CoreHash)throw new Exception("Core changed; restart the matching autoloader after update.");
        RuntimeBridge.Inject(game,core);var timer=Stopwatch.StartNew();
        while(timer.ElapsedMilliseconds<10000&&!game.HasExited&&!RuntimeBridge.Ready(pid))if(stop.WaitOne(100))break;
        if(RuntimeBridge.Ready(pid)){attempt.Done=true;Log("AUTO_INJECT_READY pid="+pid);}else{attempt.Next=DateTime.UtcNow.AddSeconds(5);Log("CORE_NOT_READY pid="+pid);}
       }catch(Exception error){Log("LOAD_ERROR "+error.Message);Attempt failed;if(attempts.TryGetValue(game.Id,out failed))failed.Next=DateTime.UtcNow.AddSeconds(5);}
      }
      foreach(int pid in new List<int>(attempts.Keys))if(!seen.Contains(pid)){attempts.Remove(pid);Log("GAME_EXITED pid="+pid);}
     }
     ready.Reset();Log("AUTOLOADER_STOPPED");
    }
    mutex.ReleaseMutex();
   }
   return 0;
  }catch(Exception error){Log("AUTOLOADER_ERROR "+error.Message);return 1;}
 }
}
}
