using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Collections.Generic;
using System.Web.Script.Serialization;
using System.Threading;

namespace NgsRumblePatch {
public sealed class FeatureInspection {
 public string Root,Description,ConfigRoot;public bool CanInstall,CanInstallAfterimage,CanRestore,Rumble,DualSense,Afterimage,AfterimageMaterials;
 public int MaxSurfaceBrightness=100,MaxGlowStrength=100;
}
public enum AfterimageMaterial { Classic, Sigma2 }
public sealed class AfterimagePreset {
 public const int MaxSurfaceBrightness=200;
 public const int MaxSigma2GlowStrength=200;
 public int Brightness=68,SurfaceBrightness=100,Opacity=40,Red=144,Green=163,Blue=253;
 public int MuscleHighlight=45;
 public static readonly string[] Sections={"Afterimage.Black","Afterimage.Purple","Afterimage.Blue","Afterimage.Ashtar","Afterimage.Grip"};
 public static string[] Labels {
  get {
   return InstallerLanguage.English?
    new[]{"Legendary Black Falcon","Purple Ninja Outfit","Legendary Ninja (Blue)","Ashtar Armor","Grip of Murder"}:
    new[]{"默认黑衣","初期紫色忍者服","蓝色传说忍者服","Ashtar 盔甲","杀戮凶手"};
  }
 }
 public static bool ShowsMuscleHighlight(int costume){return costume==0||costume==2||costume==4;}
 public static string MaterialLabel(AfterimageMaterial material){
  return material==AfterimageMaterial.Sigma2?InstallerLanguage.T("sigma2 材质","sigma2 Material"):InstallerLanguage.T("原版材质","Classic Material");
 }
 public static AfterimageMaterial ReadMaterial(string ini){
  return String.Equals(FeatureIni.Get(ini,"Afterimage","Material",""),"Sigma2",StringComparison.OrdinalIgnoreCase)
   ?AfterimageMaterial.Sigma2:AfterimageMaterial.Classic;
 }
 static string Section(int costume,AfterimageMaterial material){
  string section=Sections[costume];
  return material==AfterimageMaterial.Sigma2?section.Insert("Afterimage".Length,".Sigma2"):section;
 }
 static int Number(string ini,string section,string key,int fallback,int max){
  int value;
  return Int32.TryParse(FeatureIni.Get(ini,section,key,""),out value)?Math.Min(max,Math.Max(0,value)):fallback;
 }
 public static AfterimagePreset Read(string ini,int costume,AfterimageMaterial material=AfterimageMaterial.Classic){
  string section=Section(costume,material);
  return new AfterimagePreset{
   Brightness=Number(ini,section,"GlowStrengthPercent",68,material==AfterimageMaterial.Sigma2?MaxSigma2GlowStrength:100),
   SurfaceBrightness=Number(ini,section,"SurfaceBrightnessPercent",100,MaxSurfaceBrightness),
   Opacity=Number(ini,section,"OpacityPercent",40,100),
   MuscleHighlight=ShowsMuscleHighlight(costume)?Number(ini,section,"MuscleHighlightPercent",45,100):0,
   Red=Number(ini,section,"TintR",144,255),Green=Number(ini,section,"TintG",163,255),Blue=Number(ini,section,"TintB",253,255)
  };
 }
 public string Write(string ini,int costume,AfterimageMaterial material=AfterimageMaterial.Classic){
  string section=Section(costume,material);
  int maxGlow=material==AfterimageMaterial.Sigma2?MaxSigma2GlowStrength:100;
  foreach(var setting in new[]{new{Key="GlowStrengthPercent",Value=Brightness,Max=maxGlow},new{Key="OpacityPercent",Value=Opacity,Max=100},
   new{Key="TintR",Value=Red,Max=255},new{Key="TintG",Value=Green,Max=255},new{Key="TintB",Value=Blue,Max=255}}){
   if(setting.Value<0||setting.Value>setting.Max)throw new ArgumentOutOfRangeException(setting.Key);
   ini=FeatureIni.Set(ini,section,setting.Key,setting.Value.ToString(System.Globalization.CultureInfo.InvariantCulture));
  }
  if(material==AfterimageMaterial.Sigma2){
   if(SurfaceBrightness<0||SurfaceBrightness>MaxSurfaceBrightness)throw new ArgumentOutOfRangeException("SurfaceBrightness");
   ini=FeatureIni.Set(ini,section,"SurfaceBrightnessPercent",SurfaceBrightness.ToString(System.Globalization.CultureInfo.InvariantCulture));
  }
  if(ShowsMuscleHighlight(costume)){
   if(MuscleHighlight<0||MuscleHighlight>100)throw new ArgumentOutOfRangeException("MuscleHighlight");
   ini=FeatureIni.Set(ini,section,"MuscleHighlightPercent",MuscleHighlight.ToString(System.Globalization.CultureInfo.InvariantCulture));
  }
  return ini;
 }
}
public static class FeatureIni {
 public static string Get(string text,string section,string key,string fallback){bool inside=false;foreach(string line in text.Replace("\r","").Split('\n')){string t=line.Trim();if(t.StartsWith("[")&&t.EndsWith("]")){inside=t.Equals("["+section+"]",StringComparison.OrdinalIgnoreCase);continue;}int equal=t.IndexOf('=');if(inside&&equal>0&&t.Substring(0,equal).Trim().Equals(key,StringComparison.OrdinalIgnoreCase))return t.Substring(equal+1).Trim();}return fallback;}
 public static string Set(string text,string section,string key,string value){var lines=text.Replace("\r","").Split('\n').ToList();bool inside=false,found=false;int insertion=-1;bool seen=false;
  for(int i=0;i<lines.Count;++i){string t=lines[i].Trim();if(t.StartsWith("[")&&t.EndsWith("]")){if(inside&&insertion<0)insertion=i;inside=t.Equals("["+section+"]",StringComparison.OrdinalIgnoreCase);seen|=inside;continue;}int equal=t.IndexOf('=');if(inside&&equal>0&&t.Substring(0,equal).Trim().Equals(key,StringComparison.OrdinalIgnoreCase)){lines[i]=key+"="+value;found=true;}}
  if(seen&&insertion<0)insertion=lines.Count;
  if(!found){if(insertion<0){while(lines.Count>0&&String.IsNullOrWhiteSpace(lines.Last()))lines.RemoveAt(lines.Count-1);lines.Add("");lines.Add("["+section+"]");lines.Add(key+"="+value);}else lines.Insert(insertion,key+"="+value);}
  return String.Join("\r\n",lines).TrimEnd('\r','\n')+"\r\n";
 }
}
public sealed class FeatureFacade {
 readonly PatchFacade core=new PatchFacade();
 static string DataRoot(string root){return LaunchEngine.HasState(root)?PatchEngine.Child(root,LaunchEngine.Folder):root;}
 sealed class FeatureLock:IDisposable {
  readonly Mutex mutex;
  public FeatureLock(string root){
   mutex=new Mutex(false,"Local\\NGS1FeatureSettings-"+RuntimeBridge.Key(root));
   bool acquired;
   try{acquired=mutex.WaitOne(0);}catch(AbandonedMutexException){acquired=true;}
   if(!acquired){mutex.Dispose();throw new Exception(InstallerLanguage.T("另一个补丁操作正在修改此目录。", "Another patch operation is modifying this folder."));}
  }
  public void Dispose(){mutex.ReleaseMutex();mutex.Dispose();}
 }
 static PatchState Journal(string folder){string file=Path.Combine(folder,Payload.StateFolder,"state.json");if(!File.Exists(file))return null;var state=new JavaScriptSerializer().Deserialize<PatchState>(File.ReadAllText(file));return state!=null&&state.Product=="NGS1RumblePatch"&&state.Schema==1&&state.Phase=="Installed"?state:null;}
 public FeatureInspection Inspect(string input){
  var basic=core.Inspect(input);var info=new FeatureInspection{Root=basic.Root,Description=basic.Description,CanInstall=basic.CanInstall,CanRestore=basic.CanRestore};
  if(String.IsNullOrEmpty(info.Root))return info;
  try{
   info.CanInstallAfterimage=info.CanInstall&&GameCompatibility.Inspect(Path.Combine(info.Root,Payload.ExeName)).AfterimageCompatible;
   info.ConfigRoot=DataRoot(info.Root);var journal=Journal(info.ConfigRoot);if(journal==null)return info;string file=PatchEngine.Child(info.ConfigRoot,"NGS1Rumble.ini");string ini=File.Exists(file)?File.ReadAllText(file):"";
   info.Rumble=FeatureIni.Get(ini,"Features","Rumble",FeatureIni.Get(ini,"Rumble","Enabled","1"))!="0";
   info.DualSense=FeatureIni.Get(ini,"Features","DualSense",journal.Version=="0.5.20"?"1":"0")!="0";
   info.Afterimage=FeatureIni.Get(ini,"Features","Afterimage","0")!="0";
   Version version;
   info.AfterimageMaterials=Version.TryParse(journal.Version,out version)&&version>=new Version(0,6,7);
   info.MaxSurfaceBrightness=info.AfterimageMaterials&&version>=new Version(0,6,8)?AfterimagePreset.MaxSurfaceBrightness:100;
   info.MaxGlowStrength=info.AfterimageMaterials&&version>=new Version(0,6,9)?AfterimagePreset.MaxSigma2GlowStrength:100;
  }catch(Exception e){info.CanInstall=info.CanInstallAfterimage=false;info.Description=e.Message;}
  return info;
 }
 List<PatchFile> SelectPayload(string root,bool rumble,bool ds,bool afterimage){
  var files=Payload.Load();string folder=DataRoot(root);var journal=Journal(folder);bool preserve=journal!=null;
  foreach(var file in files.Where(f=>f.Name.EndsWith(".ini",StringComparison.OrdinalIgnoreCase))){string installed=Path.Combine(folder,file.Name);if(preserve&&File.Exists(installed))file.Bytes=File.ReadAllBytes(installed);
   if(file.Name=="NGS1Rumble.ini"){
    string text=Encoding.UTF8.GetString(file.Bytes).TrimStart('\uFEFF');text=FeatureIni.Set(text,"Rumble","Enabled",rumble?"1":"0");text=FeatureIni.Set(text,"DualSense","Enabled",ds?"1":"0");
    text=FeatureIni.Set(text,"Features","Rumble",rumble?"1":"0");text=FeatureIni.Set(text,"Features","DualSense",ds?"1":"0");text=FeatureIni.Set(text,"Features","AutoIcons",ds?"1":"0");
    text=FeatureIni.Set(text,"Features","Afterimage",afterimage?"1":"0");
    if(preserve&&journal.Version=="0.6.6-visual-test.1"){
     for(int i=0;i<AfterimagePreset.Sections.Length;i++)
      text=AfterimagePreset.Read(text,i).Write(text,i,AfterimageMaterial.Sigma2);
     text=FeatureIni.Set(text,"Afterimage","Material","Sigma2");
    }
    file.Bytes=new UTF8Encoding(false).GetBytes(text);
   }
   file.Hash=Payload.Hash(file.Bytes);
  }
  return files;
 }
 public string Change(string input,bool? rumble,bool? ds,bool? afterimage=null){
  string root=PatchEngine.Root(input);
  using(var guard=new FeatureLock(root))return ChangeLocked(root,rumble,ds,afterimage);
 }
 string ChangeLocked(string root,bool? rumble,bool? ds,bool? afterimage){
  AutoLifecycle.RequireGameStopped(root);var current=Inspect(root);
  bool nextRumble=rumble??current.Rumble,nextDS=ds??current.DualSense,nextAfterimage=afterimage??current.Afterimage;
  if(!nextRumble&&!nextDS&&!nextAfterimage)return core.Restore(root);
  GameCompatibility.Validate(Path.Combine(root,Payload.ExeName));if(!current.CanInstall)throw new Exception(current.Description);
  if(nextAfterimage&&!current.CanInstallAfterimage)throw new Exception(InstallerLanguage.T("此游戏构建的渲染入口尚未验证。请确认未安装其他会修改游戏程序的补丁。", "The rendering entry point for this game build has not been verified. Check whether another patch modified the executable."));
  var selected=SelectPayload(root,nextRumble,nextDS,nextAfterimage);var compatibility=GameCompatibility.Inspect(Path.Combine(root,Payload.ExeName));
  string message=compatibility.Family=="original-code"?LaunchEngine.Install(root,true,selected):new PatchEngine(null,selected).Install(root);
  return String.Format(InstallerLanguage.T(
   "震动 {0} · PS5 手柄 {1} · 残影 {2}。\r\n可直接打开游戏 EXE，或点击“启动游戏”。\r\n{3}",
   "Rumble {0} · DualSense {1} · Afterimages {2}.\r\nOpen the game EXE or click Launch Game.\r\n{3}"),
   EnabledText(nextRumble),EnabledText(nextDS),EnabledText(nextAfterimage),message);
 }
 static string EnabledText(bool enabled){return enabled?InstallerLanguage.T("开启","on"):InstallerLanguage.T("关闭","off");}
 public string InstallRumble(string root){return Change(root,true,null);}
 public string InstallDualSense(string root){return Change(root,null,true);}
 public string RestoreRumble(string root){return Change(root,false,null);}
 public string RestoreDualSense(string root){return Change(root,null,false);}
 public string InstallAfterimage(string root){return Change(root,null,null,true);}
 public string RestoreAfterimage(string root){return Change(root,null,null,false);}
 public string RestoreAll(string input){string root=PatchEngine.Root(input);using(var guard=new FeatureLock(root))return core.Restore(root);}
 string ReadAfterimageIni(string input){
  string file=PatchEngine.Child(DataRoot(PatchEngine.Root(input)),"NGS1Rumble.ini");
  return File.Exists(file)?File.ReadAllText(file):"";
 }
 public AfterimageMaterial LoadMaterial(string input){return AfterimagePreset.ReadMaterial(ReadAfterimageIni(input));}
 public AfterimagePreset LoadPreset(string input,int costume,AfterimageMaterial material=AfterimageMaterial.Classic){
  return AfterimagePreset.Read(ReadAfterimageIni(input),costume,material);
 }
 public string SavePreset(string input,int costume,AfterimagePreset preset,AfterimageMaterial material=AfterimageMaterial.Classic){
  string root=PatchEngine.Root(input);
  using(var guard=new FeatureLock(root)){
   var info=Inspect(root);
   if(!info.Afterimage||!info.CanInstallAfterimage)throw new Exception(InstallerLanguage.T("请先在这个目录安装残影补丁。", "Install afterimages for this game folder first."));
   if(material==AfterimageMaterial.Sigma2&&!info.AfterimageMaterials)
    throw new Exception(InstallerLanguage.T("请先更新残影补丁。","Update the afterimage patch first."));
   if(material==AfterimageMaterial.Sigma2&&
    (preset.SurfaceBrightness>info.MaxSurfaceBrightness||preset.Brightness>info.MaxGlowStrength))
    throw new Exception(InstallerLanguage.T("请先点击「安装／更新残影」，再保存超过旧版上限的设置。",
     "Choose Install / Update Afterimages before saving values above the installed version's limits."));
   string file=PatchEngine.Child(info.ConfigRoot,"NGS1Rumble.ini");
   string text=preset.Write(File.ReadAllText(file),costume,material);
   if(info.AfterimageMaterials)text=FeatureIni.Set(text,"Afterimage","Material",material.ToString());
   var bytes=new UTF8Encoding(false).GetBytes(text);
   PatchEngine.AtomicWrite(file,bytes);
  }
  return String.Format(InstallerLanguage.T(
   "已保存「{0}」的{1}设置，并应用该材质。游戏运行中会自动重载。",
   "Saved {1} settings for {0} and applied the material. A running game reloads them automatically."),
   AfterimagePreset.Labels[costume],AfterimagePreset.MaterialLabel(material));
 }
 public int Launch(string input){
  string root=PatchEngine.Root(input);var current=Inspect(root);bool patched=current.Rumble||current.DualSense||current.Afterimage;
  if(patched){GameCompatibility.Validate(Path.Combine(root,Payload.ExeName));LaunchEngine.StartInstalledWatcher(root);}
  var running=RuntimeBridge.Games(root);try{if(running.Length>0)return running[0].Id;}finally{foreach(var game in running)game.Dispose();}
  using(var process=System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo{FileName=Path.Combine(root,Payload.ExeName),WorkingDirectory=root,UseShellExecute=true}))return process.Id;
 }
}
}
