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
   InstallPrevious(args[1],root,"--install-afterimage");
   var before=features.Inspect(root);
   Check(before.Rumble&&before.DualSense&&before.Afterimage&&before.MaxClassicGlowStrength==100,
    "Previous feature flags or Classic limit not loaded correctly.");
   var customClassic=new AfterimagePreset{Brightness=73,Opacity=29,Red=77,Green=88,Blue=99,MuscleHighlight=30};
   features.SavePreset(root,0,customClassic);
   string rules=Path.Combine(before.ConfigRoot,"NGS1RumbleRules.ini");
   File.AppendAllText(rules,"\r\n; preserved custom rules\r\n");
   string ruleHash=Payload.FileHash(rules);
   features.InstallAfterimage(root);
   var installed=features.Inspect(root);
   Check(installed.Rumble&&installed.DualSense&&installed.Afterimage&&installed.AfterimageMaterials&&
    installed.MaxSurfaceBrightness==200&&installed.MaxGlowStrength==200&&installed.MaxClassicGlowStrength==200,
    "Afterimage upgrade changed controller features or did not apply the new limits.");
   Check(FeatureIni.Get(File.ReadAllText(Path.Combine(installed.ConfigRoot,"NGS1Rumble.ini")),
    "Features","AutoIcons","0")=="1","The public installer did not enable built-in DualSense icons.");
   Check(Payload.FileHash(rules)==ruleHash,"Afterimage upgrade changed rumble rules.");
   Check(features.LoadMaterial(root)==AfterimageMaterial.Classic&&features.LoadPreset(root,0).Opacity==29,
    "Upgrade changed the active material or classic settings.");
   Check(installed.HasRenderProfiles&&installed.RenderProfile==AfterimageRenderProfile.Auto,
    "Upgrade did not default to automatic rendering profile selection.");
   Console.WriteLine("PASS: in-place upgrade preserves controller flags, rumble rules and classic afterimages.");

   for(int i=0;i<5;++i){
    Check(features.LoadPreset(root,i,AfterimageMaterial.Sigma2).Opacity==40,"Sigma2 default preset differs.");
    features.SavePreset(root,i,new AfterimagePreset{Brightness=200-i*20,Opacity=100-i*17,
     MuscleHighlight=20+i*15,Red=245-i,Green=30+i,Blue=15+i});
    features.SavePreset(root,i,new AfterimagePreset{Brightness=120+i*20,Opacity=35+i*7,
     SurfaceBrightness=120+i*20,MuscleHighlight=44-i*5,Red=132+i,Green=150+i,Blue=210+i},AfterimageMaterial.Sigma2);
   }
   features.InstallAfterimage(root);
   Check(features.LoadMaterial(root)==AfterimageMaterial.Sigma2,"The selected material was lost during upgrade.");
   for(int i=0;i<5;++i){
    var preset=features.LoadPreset(root,i);
    Check(preset.Brightness==200-i*20&&preset.Opacity==100-i*17&&preset.Red==245-i&&preset.Green==30+i&&preset.Blue==15+i,
     "Classic preset lost during save or upgrade.");
    bool exposed=i==0||i==2||i==4;
    Check(AfterimagePreset.ShowsMuscleHighlight(i)==exposed&&preset.MuscleHighlight==(exposed?20+i*15:0),"Costume-specific highlight lost or applied incorrectly.");
    var sigma2=features.LoadPreset(root,i,AfterimageMaterial.Sigma2);
    Check(sigma2.Brightness==120+i*20&&sigma2.SurfaceBrightness==120+i*20&&sigma2.Opacity==35+i*7&&
     sigma2.Red==132+i&&sigma2.Green==150+i&&sigma2.Blue==210+i&&sigma2.MuscleHighlight==(exposed?44-i*5:0),
     "Sigma2 settings were not kept separately for each costume.");
   }
   features.SavePreset(root,0,features.LoadPreset(root,0));
   Check(features.LoadMaterial(root)==AfterimageMaterial.Classic&&
    features.LoadPreset(root,0,AfterimageMaterial.Sigma2).SurfaceBrightness==120,"Changing material overwrote the Sigma2 preset.");
   Console.WriteLine("PASS: both materials keep independent presets for all five costumes.");
   features.SaveRenderProfile(root,AfterimageRenderProfile.Integrated);
   features.InstallRumble(root);
   Check(features.Inspect(root).RenderProfile==AfterimageRenderProfile.Integrated&&
    features.LoadPreset(root,0,AfterimageMaterial.Sigma2).SurfaceBrightness==120,
    "Updating a feature lost the manual profile or the existing visual settings.");
   features.InstallAfterimage(root,AfterimageRenderProfile.Standard);
   Check(features.Inspect(root).RenderProfile==AfterimageRenderProfile.Standard,
    "Install did not honor the selected profile.");
   features.SaveRenderProfile(root,AfterimageRenderProfile.Auto);
   Check(features.Inspect(root).RenderProfile==AfterimageRenderProfile.Auto,"Automatic mode was not persisted.");
   Console.WriteLine("PASS: the selected render profile survives updates and can be changed independently of materials.");

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
