using System;
using System.IO;
using System.Linq;
using System.Diagnostics;
using System.Threading;
using System.Collections.Generic;
using Microsoft.Win32;
using System.Web.Script.Serialization;
using NgsRumblePatch;

static class AutoLifecycleTests {
 static List<string> checks=new List<string>();
 static void Assert(bool value,string message){if(!value)throw new Exception(message);}
 static void Pass(string message){checks.Add(message);Console.WriteLine("PASS: "+message);}
 static int Count(string root){int count=0;foreach(var p in Process.GetProcessesByName("NGS1RumbleAuto"))using(p){try{if(String.Equals(p.MainModule.FileName,AutoLifecycle.Executable(root),StringComparison.OrdinalIgnoreCase))++count;}catch(InvalidOperationException){}}return count;}
 static void Reject(Action action){try{action();}catch(Exception){return;}throw new Exception("Expected rejection");}
 static int Main(string[] args){
  string home=Path.Combine(Path.GetFullPath(args[1]),"auto-life-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(home);var roots=new List<string>();
  try{
   var facade=new PatchFacade();string root=Path.Combine(home,"normal");Directory.CreateDirectory(root);roots.Add(root);File.Copy(args[0],Path.Combine(root,Payload.ExeName));string hash=Payload.FileHash(Path.Combine(root,Payload.ExeName));
   facade.Install(root);Assert(AutoLifecycle.IsRegistered(root),"Login startup entry absent");Assert(Count(root)==1,"Autoloader was not started independently");Assert(!Directory.GetFiles(root,"*.lnk").Any(),"Shortcut generated");Pass("install registers only its per-user startup entry and starts one independent autoloader");
   facade.Install(root);Assert(Count(root)==1&&AutoLifecycle.IsRegistered(root),"Repeated install duplicated or disabled loader");Pass("repeated install keeps exactly one autoloader and startup entry");
   AutoLifecycle.Stop(root);Assert(Count(root)==0&&AutoLifecycle.IsRegistered(root),"Stopping loader removed startup entry");
   using(var p=Process.Start(new ProcessStartInfo{FileName=AutoLifecycle.Executable(root),Arguments="--watch \""+root+"\"",UseShellExecute=false,CreateNoWindow=true,WindowStyle=ProcessWindowStyle.Hidden})){
    Thread.Sleep(700);Assert(!p.HasExited&&Count(root)==1,"Simulated login startup failed");
   }
   Pass("saved login-start command can restart the loader without the installer");
   facade.Restore(root);Assert(!Directory.Exists(Path.Combine(root,LaunchEngine.Folder))&&!AutoLifecycle.IsRegistered(root)&&Count(root)==0,"Complete restore left loader, Run entry, or runtime folder");Assert(Payload.FileHash(Path.Combine(root,Payload.ExeName))==hash,"Game EXE changed");Pass("complete restore stops loader, removes its startup entry and runtime folder, and preserves EXE");
   using(var run=Registry.CurrentUser.CreateSubKey(AutoLifecycle.RunPath))run.SetValue(AutoLifecycle.Entry(root),AutoLifecycle.Command(root));
   Assert(facade.Inspect(root).CanRestore,"Orphan entry not detected");facade.Restore(root);Assert(!AutoLifecycle.IsRegistered(root),"Orphan Run entry retained");Pass("restore also removes an owned orphan startup entry after runtime files are missing");
   root=Path.Combine(home,"foreign-startup");Directory.CreateDirectory(root);roots.Add(root);File.Copy(args[0],Path.Combine(root,Payload.ExeName));
   using(var run=Registry.CurrentUser.CreateSubKey(AutoLifecycle.RunPath))run.SetValue(AutoLifecycle.Entry(root),"FOREIGN STARTUP VALUE");
   Reject(()=>facade.Install(root));using(var run=Registry.CurrentUser.OpenSubKey(AutoLifecycle.RunPath))Assert((string)run.GetValue(AutoLifecycle.Entry(root))=="FOREIGN STARTUP VALUE","Foreign startup entry overwritten");
   facade.Restore(root);using(var run=Registry.CurrentUser.OpenSubKey(AutoLifecycle.RunPath))Assert((string)run.GetValue(AutoLifecycle.Entry(root))=="FOREIGN STARTUP VALUE","Foreign startup entry removed");Pass("foreign same-name startup entry is preserved during both install refusal and restore");
   File.WriteAllText(Path.Combine(home,"test-results.json"),new JavaScriptSerializer().Serialize(new{passed=checks.Count,checks=checks}));Console.WriteLine("ALL PASSED: "+checks.Count+" autoloader lifecycle scenarios");return 0;
  }catch(Exception e){Console.Error.WriteLine(e);return 1;}
  finally{foreach(string root in roots){try{AutoLifecycle.Stop(root);AutoLifecycle.Disable(root);using(var run=Registry.CurrentUser.OpenSubKey(AutoLifecycle.RunPath,true))if(run!=null&&(string)run.GetValue(AutoLifecycle.Entry(root))=="FOREIGN STARTUP VALUE")run.DeleteValue(AutoLifecycle.Entry(root),false);}catch{}}}
 }
}
