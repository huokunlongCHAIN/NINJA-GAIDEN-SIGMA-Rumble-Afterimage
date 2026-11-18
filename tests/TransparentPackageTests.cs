using System;
using System.IO;
using System.Linq;
using System.Diagnostics;
using System.Reflection;
using NgsRumblePatch;

static class TransparentPackageTests {
 static void Check(bool value,string message){if(!value)throw new Exception(message);}
 static void Reject(Action action,string expected){
  try{action();}catch(IOException error){Check(error.Message.Contains(expected),"Unexpected failure: "+error.Message);return;}
  throw new Exception("Expected missing/tampered package rejection.");
 }
 static void Previous(string installer,string root,string operation){
  string output=Path.Combine(root,"previous-result.json");
  using(var p=Process.Start(new ProcessStartInfo{FileName=installer,Arguments=operation+" \""+root+"\" \""+output+"\"",UseShellExecute=false,CreateNoWindow=true}))
   Check(p.WaitForExit(20000)&&p.ExitCode==0&&File.ReadAllText(output).Contains("\"ok\":true"),"Previous installer failed.");
 }
 static int Main(string[] args){
  string home=Path.Combine(Path.GetFullPath(args[0]),"transparent-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(home);
  var features=new FeatureFacade();
  try{
   Payload.ValidateAll();
   Check(Assembly.GetExecutingAssembly().GetManifestResourceNames().Length==0,"Test executable unexpectedly embeds payloads.");
   var installer=Assembly.ReflectionOnlyLoadFrom(args[1]);
   Check(installer.GetManifestResourceNames().Length==0,"Installer still embeds executable components.");
   Console.WriteLine("PASS: installer contains zero managed embedded resources; all packaged components verify.");
   string probe=Path.Combine(home,"refused");Directory.CreateDirectory(probe);File.Copy(args[3],Path.Combine(probe,Payload.ExeName));
   foreach(string name in Payload.Names.Concat(Payload.ExtraNames)){
    string path=Path.Combine(Payload.DirectoryPath,name),saved=path+".test-backup";
    File.Move(path,saved);
    try{
     Reject(()=>features.InstallAfterimage(probe),name);
     Check(!Directory.Exists(Path.Combine(probe,LaunchEngine.Folder))&&!Directory.Exists(Path.Combine(probe,Payload.StateFolder)),"Incomplete package wrote installation state.");
    }finally{File.Move(saved,path);}
   }
   string core=Path.Combine(Payload.DirectoryPath,"dinput8.dll");byte[] original=File.ReadAllBytes(core);
   try{var changed=(byte[])original.Clone();changed[changed.Length-1]^=1;File.WriteAllBytes(core,changed);Reject(()=>features.InstallRumble(probe),"dinput8.dll");}
   finally{File.WriteAllBytes(core,original);}
   Check(!Directory.Exists(Path.Combine(probe,LaunchEngine.Folder))&&!Directory.Exists(Path.Combine(probe,Payload.StateFolder)),"Tampered package wrote installation state.");
   Console.WriteLine("PASS: each missing component and a tampered core fail before installation writes.");
   int index=0;
   foreach(string game in args.Skip(3)){
    string root=Path.Combine(home,"upgrade-"+(++index));Directory.CreateDirectory(root);File.Copy(game,Path.Combine(root,Payload.ExeName));
    string gameHash=Payload.FileHash(Path.Combine(root,Payload.ExeName));
    try{
     Previous(args[2],root,"--install-rumble");Previous(args[2],root,"--install-ps5");Previous(args[2],root,"--install-afterimage");
     features.SavePreset(root,2,new AfterimagePreset{Brightness=164,SurfaceBrightness=188,Opacity=61,MuscleHighlight=32,Red=130,Green=160,Blue=240},AfterimageMaterial.NG2);
     string oldConfig=features.Inspect(root).ConfigRoot;
     string rule=Path.Combine(oldConfig,"NGS1RumbleRules.ini");File.AppendAllText(rule,"\r\n; transparent upgrade preservation\r\n");string rulesHash=Payload.FileHash(rule);
     features.InstallAfterimage(root);var info=features.Inspect(root);
     rule=Path.Combine(info.ConfigRoot,"NGS1RumbleRules.ini");
     Check(info.ConfigRoot==root&&!Directory.Exists(Path.Combine(root,LaunchEngine.Folder))&&!AutoLifecycle.IsRegistered(root),"Migration retained the old runtime or startup entry.");
     Check(info.Rumble&&info.DualSense&&info.Afterimage,"Upgrade lost feature flags.");
     var preset=features.LoadPreset(root,2,AfterimageMaterial.NG2);
     Check(preset.Brightness==164&&preset.SurfaceBrightness==188&&preset.Opacity==61&&preset.MuscleHighlight==32,"Upgrade lost outfit settings.");
     Check(Payload.FileHash(rule)==rulesHash,"Upgrade changed custom rumble rules.");
     features.RestoreRumble(root);Check(!features.Inspect(root).Rumble&&features.Inspect(root).Afterimage,"Independent restore failed.");
     features.RestoreAll(root);
     Check(!Directory.Exists(Path.Combine(root,LaunchEngine.Folder))&&!Directory.Exists(Path.Combine(root,Payload.StateFolder))&&!File.Exists(Path.Combine(root,"dinput8.dll"))&&!AutoLifecycle.IsRegistered(root),"Restore left patch-owned files or registration.");
     Check(Payload.FileHash(Path.Combine(root,Payload.ExeName))==gameHash,"Game executable was modified.");
     Console.WriteLine("PASS: 0.6.25 upgrade, presets, rules, independent restore and complete restore: "+game);
    }finally{try{features.RestoreAll(root);}catch{}AutoLifecycle.Stop(root);AutoLifecycle.Disable(root);}
   }
   return 0;
  }catch(Exception error){Console.Error.WriteLine(error);return 1;}
 }
}
