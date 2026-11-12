using System;
using System.IO;
using System.Text;
using System.Linq;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Web.Script.Serialization;
using NgsRumblePatch;

static class InstallerTests {
 static string game,home;static int passed;
 static List<string> checks=new List<string>();
 static string previousVersion="0.5.14";
 static string Fixture(string name){string root=Path.Combine(home,name);Directory.CreateDirectory(root);File.Copy(game,Path.Combine(root,Payload.ExeName));File.WriteAllText(Path.Combine(root,"save-sentinel.bin"),"USER SAVE");File.WriteAllText(Path.Combine(root,"d3d11.dll"),"OTHER MOD");return root;}
 static void Assert(bool value,string message){if(!value)throw new Exception(message);}
 static void Reject(Action action,string contains){try{action();}catch(Exception e){Assert(e.Message.Contains(contains),"Wrong failure: "+e.Message);return;}throw new Exception("Expected refusal: "+contains);}
 static string At(string root,string name){return Path.Combine(root,name);}
 static void CheckSentinels(string root){Assert(Payload.FileHash(At(root,Payload.ExeName))==Payload.ExeHash,"EXE modified");Assert(File.ReadAllText(At(root,"save-sentinel.bin"))=="USER SAVE","Save modified");Assert(File.ReadAllText(At(root,"d3d11.dll"))=="OTHER MOD","Other mod modified");}
 static void ExactPayload(string root){foreach(var f in Payload.Load())Assert(File.Exists(At(root,f.Name))&&Payload.FileHash(At(root,f.Name))==f.Hash,"Payload mismatch: "+f.Name);}
 static void NoPayload(string root){Assert(Payload.Names.All(n=>!File.Exists(At(root,n))),"Patch file left behind");}
 static void Pass(string name){++passed;checks.Add(name);Console.WriteLine("PASS: "+name);}
 static Dictionary<string,byte[]> Originals(string root,string knownCore){
  var originals=new Dictionary<string,byte[]>();
  foreach(string name in Payload.Names){byte[] bytes=name=="dinput8.dll"?File.ReadAllBytes(knownCore):Encoding.UTF8.GetBytes("prior user config "+name+"\r\n");File.WriteAllBytes(At(root,name),bytes);originals[name]=bytes;}
  return originals;
 }
 static void ExactOriginals(string root,Dictionary<string,byte[]> originals){foreach(var p in originals)Assert(File.ReadAllBytes(At(root,p.Key)).SequenceEqual(p.Value),"Original not restored: "+p.Key);}
 static string StatePath(string root){return At(root,Path.Combine(Payload.StateFolder,"state.json"));}
 static PatchState Read(string root){return new JavaScriptSerializer().Deserialize<PatchState>(File.ReadAllText(StatePath(root)));}
 static void Write(string root,PatchState s){File.WriteAllText(StatePath(root),new JavaScriptSerializer().Serialize(s));}
 static void InstallPrevious(string exe,string root){
  string result=At(root,"previous-installer-result.json");
  string operation=new Version(previousVersion)>=new Version("0.5.17")?"--prepare":"--install";
  using(var p=Process.Start(new ProcessStartInfo{FileName=exe,Arguments=operation+" \""+root+"\" \""+result+"\"",UseShellExecute=false,CreateNoWindow=true})){
   p.WaitForExit();Assert(p.ExitCode==0,"Previous public installer failed");
  }
  var output=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(result,Encoding.UTF8));
  Assert((bool)output["ok"],"Previous installer refused fixture");Assert(Read(root).Version==previousVersion,"Previous package version mismatch");
 }
 static int Main(string[] args){
  if(args.Length>0&&args[0]=="--hold"){File.WriteAllText(args[1],"ready");Thread.Sleep(60000);return 0;}
  try{
   game=Path.GetFullPath(args[0]);string knownCore=Path.GetFullPath(args[1]);home=Path.GetFullPath(args[2]);Directory.CreateDirectory(home);
   Assert(PatchEngine.Root(Path.GetPathRoot(home))==Path.GetPathRoot(home),"Drive root was turned into drive-relative path");
   Assert(Payload.FileHash(game)==Payload.ExeHash,"Test game differs");
   var engine=new PatchEngine();
   string r=Fixture("01-clean-install");engine.Install(r);ExactPayload(r);CheckSentinels(r);
   string journal=File.ReadAllText(StatePath(r));engine.Install(r);Assert(journal==File.ReadAllText(StatePath(r)),"Repeat install changed backup");
   engine.Restore(r);NoPayload(r);CheckSentinels(r);engine.Restore(r);NoPayload(r);Pass("clean install, repeat install, exact clean uninstall, repeated restore, EXE/save/other mods untouched");

   r=Fixture("02-existing-owned-mod");var original=Originals(r,knownCore);DateTime stamp=new DateTime(2001,2,3,4,5,6,DateTimeKind.Utc);File.SetLastWriteTimeUtc(At(r,"NGS1RumbleRules.ini"),stamp);
   engine.Install(r);ExactPayload(r);engine.Restore(r);ExactOriginals(r,original);Assert(File.GetLastWriteTimeUtc(At(r,"NGS1RumbleRules.ini"))==stamp,"Original timestamp not restored");CheckSentinels(r);Pass("upgrade from known core restores all previous custom files and modification time");

   r=Fixture("03-unrelated-dll");File.WriteAllText(At(r,"dinput8.dll"),"UNKNOWN PLUGIN");Reject(()=>engine.Install(r),"其它插件");Assert(File.ReadAllText(At(r,"dinput8.dll"))=="UNKNOWN PLUGIN"&&!Directory.Exists(At(r,Payload.StateFolder)),"Conflict mutated files");Pass("unknown dinput8 conflict refused before any write");

   r=Fixture("04-unsupported-version");File.WriteAllText(At(r,Payload.ExeName),"UNSUPPORTED");Reject(()=>engine.Install(r),"尚未验证");NoPayload(r);Assert(!Directory.Exists(At(r,Payload.StateFolder)),"Unsupported created state");Pass("unknown executable version refused before any write");

   r=Fixture("05-write-failure");original=Originals(r,knownCore);var fault=new PatchEngine{BeforeWrite=i=>{if(i==2)throw new IOException("simulated disk write failure");}};
   Reject(()=>fault.Install(r),"已自动恢复");ExactOriginals(r,original);Assert(Read(r).Phase=="Restored","Failed install not rolled back");CheckSentinels(r);
   engine.Install(r);ExactPayload(r);Assert(Directory.GetDirectories(r,".NGS1RumblePatch.archive-*").Length==1,"Old backup not archived");engine.Restore(r);ExactOriginals(r,original);Pass("mid-install failure automatically rolls back; reinstall retains historical backup");

   r=Fixture("06-backup-corruption");original=Originals(r,knownCore);engine.Install(r);File.WriteAllText(At(r,Path.Combine(Payload.StateFolder,"original","NGS1RumbleRules.ini")),"CORRUPTED");
   Reject(()=>engine.Restore(r),"备份缺失或已改变");ExactPayload(r);CheckSentinels(r);Pass("corrupted backup refuses restore before modifying installed files");

   r=Fixture("07-dll-replaced-after-install");engine.Install(r);File.WriteAllText(At(r,"dinput8.dll"),"NEW THIRD PARTY DLL");Reject(()=>engine.Restore(r),"已被其它程序替换");Assert(File.ReadAllText(At(r,"dinput8.dll"))=="NEW THIRD PARTY DLL","Foreign DLL overwritten");Pass("later third-party DLL preserved during restore");

   r=Fixture("08-edited-config");engine.Install(r);string edit="[USER]\r\ncustom strength=12345\r\n";File.WriteAllText(At(r,"NGS1RumbleRules.ini"),edit);
   Reject(()=>engine.Install(r),"参数已改变");engine.Restore(r);NoPayload(r);var changed=Directory.GetDirectories(At(r,Payload.StateFolder),"changed-*");Assert(changed.Length==1&&File.ReadAllText(At(changed[0],"NGS1RumbleRules.ini"))==edit,"User edit not archived");Pass("custom edits block repeat install and are preserved before uninstall");

   r=Fixture("09-updated-exe");engine.Install(r);File.WriteAllText(At(r,Payload.ExeName),"NEW GAME UPDATE");var info=engine.Inspect(r);Assert(!info.CanInstall&&info.CanRestore,"Updated game cannot restore");engine.Restore(r);NoPayload(r);Assert(File.ReadAllText(At(r,Payload.ExeName))=="NEW GAME UPDATE","Updated EXE overwritten");Pass("restore remains available after game executable update");

   r=Fixture("10-interrupted-journal");original=Originals(r,knownCore);engine.Install(r);var state=Read(r);state.Phase="Installing";Write(r,state);engine.Restore(r);ExactOriginals(r,original);Pass("interrupted installation journal recovers original files");

   r=Fixture("11-中文 & spaces");engine.Install(At(r,Payload.ExeName));ExactPayload(r);engine.Restore(At(r,Payload.ExeName));NoPayload(r);CheckSentinels(r);Pass("Unicode, spaces and ampersand paths; selecting EXE directly");

   r=Fixture("12-same-name-directory");Directory.CreateDirectory(At(r,"NGS1RumbleRules.ini"));Reject(()=>engine.Install(r),"同名目录");Assert(!File.Exists(At(r,"dinput8.dll")),"Directory collision partially installed");Pass("same-name directory collision refused before any write");

   r=Fixture("13-tampered-path");engine.Install(r);state=Read(r);state.Files[0].Name="..\\outside.dll";Write(r,state);Reject(()=>engine.Restore(r),"文件列表异常");ExactPayload(r);Pass("tampered journal paths cannot escape the four-file whitelist");

   r=Fixture("14-game-running");File.Copy(System.Reflection.Assembly.GetExecutingAssembly().Location,At(r,Payload.ExeName),true);string marker=At(r,"child-ready.txt");
   using(var child=Process.Start(new ProcessStartInfo{FileName=At(r,Payload.ExeName),Arguments="--hold \""+marker+"\"",UseShellExecute=false,CreateNoWindow=true})){
    try{for(int i=0;i<100&&!File.Exists(marker);++i)Thread.Sleep(30);Assert(File.Exists(marker),"Fake game did not start");Reject(()=>engine.Install(r),"退出这个目录的游戏");NoPayload(r);}
    finally{if(!child.HasExited)child.Kill();child.WaitForExit();}
   }
   Pass("running target game refused; unrelated running game does not block other directories");

   r=Fixture("15-concurrent-operation");string key=Payload.Hash(Encoding.UTF8.GetBytes(r.ToUpperInvariant())).Substring(0,24);
   using(var mutex=new Mutex(true,"Local\\NGS1RumblePatch-"+key)){
    Task.Run(()=>Reject(()=>engine.Install(r),"另一个安装或还原")).GetAwaiter().GetResult();mutex.ReleaseMutex();
   }
   NoPayload(r);Pass("concurrent installer operation refused without altering files");
   r=Fixture("16-backup-preparation-failure");original=Originals(r,knownCore);
   var prepareFault=new PatchEngine{BeforeBackup=i=>{if(i==1)throw new IOException("simulated backup failure");}};
   Reject(()=>prepareFault.Install(r),"未写入补丁");ExactOriginals(r,original);CheckSentinels(r);
   Assert(!Directory.Exists(At(r,Payload.StateFolder))&&Directory.GetDirectories(r,".NGS1RumblePatch.backup-failed-*").Length==1,"Failed preparation not retained safely");
   engine.Install(r);ExactPayload(r);engine.Restore(r);ExactOriginals(r,original);Pass("backup preparation failure leaves game untouched and allows a clean retry");
   if(args.Length>=4){
    string previousInstaller=Path.GetFullPath(args[3]);if(args.Length>=5)previousVersion=args[4];
    r=Fixture("17-direct-public-upgrade");original=Originals(r,knownCore);InstallPrevious(previousInstaller,r);
    var prior=Read(r);var oldOriginals=prior.Files.ToDictionary(f=>f.Name,f=>f.OriginalHash);
    string upgradeResult=engine.Install(r);Assert(upgradeResult.Contains("已升级"),"Direct upgrade not taken");ExactPayload(r);
    state=Read(r);Assert(state.Version==Payload.Version&&state.Phase=="Installed","Upgrade journal version mismatch");
    foreach(var f in state.Files)Assert(f.OriginalHash==oldOriginals[f.Name]&&f.PreviousInstalledHash==null,"First backup overwritten by upgrade");
    Assert(Directory.GetDirectories(At(r,Payload.StateFolder),"upgrade-*").Length==1,"Pre-upgrade snapshot absent");
    engine.Restore(r);ExactOriginals(r,original);CheckSentinels(r);Pass("real "+previousVersion+" public package directly upgrades to "+Payload.Version+" and restores the first pre-install originals");

    r=Fixture("18-failed-upgrade");InstallPrevious(previousInstaller,r);var beforeUpgrade=Payload.Names.ToDictionary(n=>n,n=>File.ReadAllBytes(At(r,n)));
    var upgradeFault=new PatchEngine{AfterWrite=i=>{if(i==3)throw new IOException("simulated upgrade failure after changed waveform file was written");}};
    Reject(()=>upgradeFault.Install(r),"已恢复升级前");ExactOriginals(r,beforeUpgrade);Assert(Read(r).Version==previousVersion&&Read(r).Phase=="Installed","Failed upgrade lost previous version journal");
    engine.Install(r);ExactPayload(r);engine.Restore(r);NoPayload(r);CheckSentinels(r);Pass("failed upgrade restores the previous "+previousVersion+" payload and journal after new waveform was written; retry works");

    r=Fixture("19-interrupted-upgrade");original=Originals(r,knownCore);InstallPrevious(previousInstaller,r);state=Read(r);state.Phase="Upgrading";state.Version=Payload.Version;
    foreach(var f in state.Files){f.PreviousInstalledHash=f.InstalledHash;f.InstalledHash=Payload.Load().Single(p=>p.Name==f.Name).Hash;}
    Write(r,state);foreach(string file in new[]{"NGS1RumbleRules.ini","NGS1RumbleWaves.ini"})File.WriteAllBytes(At(r,file),Payload.Load().Single(p=>p.Name==file).Bytes);
    engine.Restore(r);ExactOriginals(r,original);CheckSentinels(r);Pass("interrupted mixed-version upgrade restores the original pre-install files");

    r=Fixture("20-customized-old-public-profile");InstallPrevious(previousInstaller,r);edit="User custom old profile strength=12345";File.WriteAllText(At(r,"NGS1RumbleRules.ini"),edit);
    string customUpgrade=engine.Install(r);ExactPayload(r);Assert(customUpgrade.Contains("自定义参数已另存"),"Custom backup not reported");
    Assert(Directory.GetDirectories(At(r,Payload.StateFolder),"upgrade-*").Any(d=>File.ReadAllText(At(d,"NGS1RumbleRules.ini"))==edit),"Customized old profile not archived");
    engine.Restore(r);NoPayload(r);Pass("customized "+previousVersion+" profile is snapshotted before direct upgrade; first originals remain restorable");

    r=Fixture("21-failed-custom-upgrade");InstallPrevious(previousInstaller,r);File.WriteAllText(At(r,"NGS1RumbleRules.ini"),edit);beforeUpgrade=Payload.Names.ToDictionary(n=>n,n=>File.ReadAllBytes(At(r,n)));
    Reject(()=>upgradeFault.Install(r),"已恢复升级前");ExactOriginals(r,beforeUpgrade);Assert(Read(r).Version==previousVersion,"Custom failure lost old journal");
    engine.Install(r);ExactPayload(r);engine.Restore(r);NoPayload(r);CheckSentinels(r);Pass("failed custom-profile upgrade restores actual edited parameters and old journal; direct retry succeeds");
   }
   File.WriteAllText(At(home,"test-results.json"),new JavaScriptSerializer().Serialize(new{passed=passed,checks=checks,fixtureRoot=home}),new UTF8Encoding(false));
   Console.WriteLine("ALL PASSED: "+passed+" filesystem scenarios");return 0;
  }catch(Exception e){Console.Error.WriteLine(e.ToString());return 1;}
 }
}
