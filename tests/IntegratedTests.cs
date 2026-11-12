using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Web.Script.Serialization;
using NgsRumblePatch;

static class IntegratedTests {
 static void Check(bool result,string message){if(!result)throw new Exception(message);}

 static void InstallPrevious(string executable,string root,string operation){
  string output=Path.Combine(root,"previous-result.json");
  var start=new ProcessStartInfo{
   FileName=executable,Arguments=operation+" \""+root+"\" \""+output+"\"",
   UseShellExecute=false,CreateNoWindow=true
  };
  using(var process=Process.Start(start)){
   Check(process.WaitForExit(15000)&&process.ExitCode==0,"Previous installer failed: "+operation);
  }
  Check(File.ReadAllText(output).Contains("\"ok\":true"),"Previous package returned failure.");
 }

 static int Main(string[] args){
  string home=Path.Combine(args[0],"integrated-test-"+Guid.NewGuid().ToString("N"));
  string root=Path.Combine(home,"[A test] Σ");
  Directory.CreateDirectory(root);
  var features=new FeatureFacade();
  try{
   foreach(var executable in args.Skip(2)){
    var compatibility=GameCompatibility.Inspect(executable);
    Check(compatibility.Compatible&&compatibility.AfterimageCompatible,"Version failed: "+executable);
    Console.WriteLine("PASS: verified both feature fingerprints for "+executable);
   }
   string game=Path.Combine(root,Payload.ExeName);
   File.Copy(args[2],game);
   string originalHash=Payload.FileHash(game);
   string foreign=Path.Combine(root,"d3d11.dll");
   File.WriteAllText(foreign,"localization renderer");
   InstallPrevious(args[1],root,"--install-rumble");
   InstallPrevious(args[1],root,"--install-ps5");
   var before=features.Inspect(root);
   Check(before.Rumble&&before.DualSense&&!before.Afterimage,"0.6.0 flags not loaded correctly.");
   string rules=Path.Combine(before.ConfigRoot,"NGS1RumbleRules.ini");
   File.AppendAllText(rules,"\r\n; preserved custom rules\r\n");
   string ruleHash=Payload.FileHash(rules);
   features.InstallAfterimage(root);
   var installed=features.Inspect(root);
   Check(installed.Rumble&&installed.DualSense&&installed.Afterimage,"Afterimage upgrade changed controller features.");
   Check(Payload.FileHash(rules)==ruleHash,"Afterimage upgrade changed rumble rules.");
   Console.WriteLine("PASS: 0.6.0 in-place upgrade preserves independent controller flags and custom rules.");

   for(int i=0;i<5;++i){
    Check(features.LoadPreset(root,i).Opacity==40,"Default preset differs.");
    features.SavePreset(root,i,new AfterimagePreset{Brightness=i*25,Opacity=100-i*17,
     MuscleHighlight=20+i*15,Red=245-i,Green=30+i,Blue=15+i});
   }
   features.InstallAfterimage(root);
   for(int i=0;i<5;++i){
    var preset=features.LoadPreset(root,i);
    Check(preset.Brightness==i*25&&preset.Opacity==100-i*17&&preset.Red==245-i&&preset.Green==30+i&&preset.Blue==15+i,"Preset lost during save or upgrade.");
    bool exposed=i==0||i==2||i==4;
    Check(AfterimagePreset.ShowsMuscleHighlight(i)==exposed&&preset.MuscleHighlight==(exposed?20+i*15:0),"Costume-specific highlight lost or applied incorrectly.");
   }
   Console.WriteLine("PASS: all five independent appearance settings persist through an update.");

   features.RestoreRumble(root);
   installed=features.Inspect(root);
   Check(!installed.Rumble&&installed.DualSense&&installed.Afterimage,"Rumble restore changed other features.");
   features.RestoreDualSense(root);
   installed=features.Inspect(root);
   Check(!installed.Rumble&&!installed.DualSense&&installed.Afterimage,"DualSense restore changed other features.");
   features.RestoreAfterimage(root);
   Check(!Directory.Exists(Path.Combine(root,LaunchEngine.Folder))&&!AutoLifecycle.IsRegistered(root),"Last feature did not clean runtime.");
   Check(Payload.FileHash(game)==originalHash&&File.ReadAllText(foreign)=="localization renderer","Game or localization modified.");
   Console.WriteLine("PASS: independent restores and complete cleanup preserve game files.");

   var unsupported=File.ReadAllBytes(game);
   uint pe=BitConverter.ToUInt32(unsupported,60);
   int textRaw=(int)BitConverter.ToUInt32(unsupported,(int)pe+264+20);
   unsupported[textRaw+500]^=1;
   File.WriteAllBytes(game,unsupported);
   Check(!features.Inspect(root).CanInstallAfterimage,"Unknown executable was accepted.");
   try{features.InstallAfterimage(root);throw new InvalidOperationException("Installer accepted unsupported executable.");}
   catch(Exception error){
    Check(error.Message.Contains("请确认未安装其他"),"Missing compatibility guidance: "+error.Message);
   }
   Check(!Directory.Exists(Path.Combine(root,LaunchEngine.Folder)),"Unknown EXE left runtime files.");
   Console.WriteLine("PASS: unknown builds are refused before any files are written.");
   return 0;
  }catch(Exception error){Console.Error.WriteLine(error);return 1;}
  finally{try{AutoLifecycle.Stop(root);AutoLifecycle.Disable(root);}catch{}}
 }
}
