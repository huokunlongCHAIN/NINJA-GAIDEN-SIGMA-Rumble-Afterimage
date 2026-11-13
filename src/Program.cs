using System;
using System.IO;
using System.Text;
using System.Drawing;
using System.Collections.Generic;
using System.Windows.Forms;
using System.Web.Script.Serialization;
using System.Threading.Tasks;
namespace NgsRumblePatch {
static class Program {
 [STAThread]static int Main(string[] args){
  // Preserve existing CLI output for automation. The UI uses the saved preference.
  if(args.Length==0||args[0]=="--ui")InstallerLanguage.Load();
  try{if(InstallerSession.Initialize(ref args))return 0;}catch(Exception error){
   if(args.Length==3)File.WriteAllText(args[2],new JavaScriptSerializer().Serialize(new{ok=false,message=error.Message}),new UTF8Encoding(false));
   else MessageBox.Show(error.Message,InstallerLanguage.T("西格玛1 功能补丁","Ninja Gaiden Sigma Enhancements"));
   return 1;
  }
  if(args.Length==2&&args[0]=="--ui"){Show(args[1]);return 0;}
  if(args.Length>0){try{var features=new FeatureFacade();if(args.Length==2&&args[0]=="--launch"){features.Launch(args[1]);return 0;}if(args.Length!=3)throw new Exception("Usage: operation <game folder> <result.json>");string result;
    if(args[0]=="--inspect")result=new JavaScriptSerializer().Serialize(features.Inspect(args[1]));
    else if(args[0]=="--launch")result=new JavaScriptSerializer().Serialize(new{ok=true,pid=features.Launch(args[1])});
    else{string message;
     switch(args[0]){
      case "--install":case "--install-rumble":message=features.InstallRumble(args[1]);break;
      case "--install-ps5":message=features.InstallDualSense(args[1]);break;
      case "--install-afterimage":message=features.InstallAfterimage(args[1]);break;
      case "--restore-rumble":message=features.RestoreRumble(args[1]);break;
      case "--restore-ps5":message=features.RestoreDualSense(args[1]);break;
      case "--restore-afterimage":message=features.RestoreAfterimage(args[1]);break;
      case "--restore":message=features.RestoreAll(args[1]);break;
      case "--prepare":message=new PatchFacade().Prepare(args[1]);break;
      default:throw new Exception("Unknown operation");
     }
     result=new JavaScriptSerializer().Serialize(new{ok=true,started=false,message=message});
    }
    File.WriteAllText(args[2],result,new UTF8Encoding(false));return 0;
   }catch(Exception error){if(args.Length==3)File.WriteAllText(args[2],new JavaScriptSerializer().Serialize(new{ok=false,started=false,message=error.Message}),new UTF8Encoding(false));else MessageBox.Show(error.Message,InstallerLanguage.T("西格玛1 功能补丁","Ninja Gaiden Sigma Enhancements"));return 1;}}
  Show(null);return 0;
 }
 static void Show(string root){InstallerLanguage.Load();Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new FeatureForm(root));}
}
sealed class FeatureForm:Form {
 readonly FeatureFacade engine=new FeatureFacade();
 readonly List<Action> translatedControls=new List<Action>();
 readonly TextBox path=new TextBox(),details=new TextBox();
 readonly Label status=new Label(),rumbleState=new Label(),dsState=new Label(),afterimageState=new Label();
 readonly ComboBox outfit=new ComboBox();
 readonly ComboBox language=new ComboBox();
 readonly NumericUpDown brightness=new NumericUpDown(),transparency=new NumericUpDown(),muscleHighlight=new NumericUpDown();
 readonly TableLayoutPanel visuals;
 readonly Control muscleRow;
 Button browse,installRumble,restoreRumble,installDS,restoreDS,installAfterimage,restoreAfterimage,pickColor,savePreset,launch,restoreAll;
 Color tint;
 bool translating;
 static readonly bool IncludesIcons=PayloadHashes.HasIconData;
 public FeatureForm(string initial=null){
  Bind(this,"西格玛1 · 功能补丁 "+Payload.Version,"Ninja Gaiden Sigma Enhancements "+Payload.Version);
  ClientSize=new Size(880,730);MinimumSize=new Size(810,680);StartPosition=FormStartPosition.CenterScreen;Font=new Font("Microsoft YaHei UI",10);BackColor=Color.FromArgb(23,29,38);ForeColor=Color.White;
  var page=new TableLayoutPanel{Dock=DockStyle.Fill,Padding=new Padding(24),ColumnCount=1,RowCount=6};
  foreach(int height in new[]{48,48,48})page.RowStyles.Add(new RowStyle(SizeType.Absolute,height));
  page.RowStyles.Add(new RowStyle(SizeType.Percent,100));
  page.RowStyles.Add(new RowStyle(SizeType.Absolute,82));
  page.RowStyles.Add(new RowStyle(SizeType.Absolute,52));
  Controls.Add(page);
  var heading=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,Margin=Padding.Empty};
  heading.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
  heading.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,125));
  heading.Controls.Add(Bind(new Label{Font=new Font(Font.FontFamily,19,FontStyle.Bold),Dock=DockStyle.Fill},
   "西格玛1 · 功能补丁","Ninja Gaiden Sigma Enhancements"),0,0);
  language.Name="InstallerLanguage";
  language.AccessibleName="语言 / Language";
  language.DropDownStyle=ComboBoxStyle.DropDownList;
  language.Dock=DockStyle.Fill;
  language.Items.AddRange(new object[]{"简体中文","English"});
  language.SelectedIndex=InstallerLanguage.English?1:0;
  language.SelectedIndexChanged+=(s,e)=>{
   bool saved=InstallerLanguage.Select(language.SelectedIndex==1);
   ApplyLanguage();
   RefreshState(false);
   if(!saved)details.Text=InstallerLanguage.T("本次会话已切换语言。无法保存语言偏好。","Language changed for this session. The preference could not be saved.");
  };
  heading.Controls.Add(language,1,0);
  page.Controls.Add(heading,0,0);
  var choose=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2};choose.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));choose.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,170));path.ReadOnly=true;path.Dock=DockStyle.Fill;path.BackColor=Color.FromArgb(40,48,60);path.ForeColor=Color.White;path.Margin=new Padding(0,5,12,0);choose.Controls.Add(path,0,0);browse=Button("选择游戏 EXE","Select Game EXE");browse.Click+=(s,e)=>Browse();choose.Controls.Add(browse,1,0);page.Controls.Add(choose,0,1);
  status.Dock=DockStyle.Fill;status.ForeColor=Color.LightSteelBlue;page.Controls.Add(status,0,2);
  var tabs=new TabControl{Dock=DockStyle.Fill,Margin=new Padding(0,0,0,12)};
  var controllers=Bind(new TabPage{BackColor=BackColor,ForeColor=ForeColor},"震动与手柄","Rumble & Controllers");
  var visual=Bind(new TabPage{BackColor=BackColor,ForeColor=ForeColor},"残影","Afterimages");
  tabs.TabPages.Add(controllers);tabs.TabPages.Add(visual);page.Controls.Add(tabs,0,3);
  installRumble=Button("安装／更新震动","Install / Update Rumble");restoreRumble=Button("还原震动","Restore Rumble");
  installDS=Button("安装／更新 PS5","Install / Update DualSense");restoreDS=Button("还原 PS5","Restore DualSense");
  var controllerPage=Section(70,70,56);
  controllers.Controls.Add(controllerPage);
  controllerPage.Controls.Add(Card(rumbleState,installRumble,restoreRumble),0,0);
  controllerPage.Controls.Add(Card(dsState,installDS,restoreDS),0,1);
  controllerPage.Controls.Add(Bind(new Label{Dock=DockStyle.Fill,ForeColor=Color.Silver},
   IncludesIcons?"PS5 手柄支持直接输入、热切换和 PS／Xbox 按键图标切换。":"PS5 手柄支持直接输入和热切换。图标切换可随本地图标数据构建。",
   IncludesIcons?"DualSense supports direct input, hot switching, and automatic PS/Xbox button icons.":
   "DualSense supports direct input and hot switching. Icon switching requires a build with local icon data."),0,2);
  installRumble.Click+=async(s,e)=>await Run(root=>engine.InstallRumble(root));restoreRumble.Click+=async(s,e)=>await Run(root=>engine.RestoreRumble(root));installDS.Click+=async(s,e)=>await Run(root=>engine.InstallDualSense(root));restoreDS.Click+=async(s,e)=>await Run(root=>engine.RestoreDualSense(root));
  visuals=Section(70,38,38,38,44,38,44);
  visual.Controls.Add(visuals);
  installAfterimage=Button("安装／更新残影","Install / Update Afterimages");restoreAfterimage=Button("还原残影","Restore Afterimages");
  visuals.Controls.Add(Card(afterimageState,installAfterimage,restoreAfterimage),0,0);
  installAfterimage.Click+=async(s,e)=>await Run(root=>engine.InstallAfterimage(root));
  restoreAfterimage.Click+=async(s,e)=>await Run(root=>engine.RestoreAfterimage(root));
  outfit.DropDownStyle=ComboBoxStyle.DropDownList;
  outfit.SelectedIndexChanged+=(s,e)=>{if(!translating)RefreshPreset();};
  visuals.Controls.Add(Field("服装","Costume",outfit),0,1);
  foreach(var number in new[]{brightness,transparency,muscleHighlight}){number.Minimum=0;number.Maximum=100;}
  visuals.Controls.Add(Field("发光亮度（%）","Glow brightness (%)",brightness),0,2);
  visuals.Controls.Add(Field("透明度（%）","Transparency (%)",transparency),0,3);
  pickColor=Button("#90A3FD","#90A3FD");
  pickColor.Click+=(s,e)=>{using(var dialog=new ColorDialog{Color=tint,FullOpen=true})if(dialog.ShowDialog(this)==DialogResult.OK)SetTint(dialog.Color);};
  visuals.Controls.Add(Field("残影颜色","Afterimage color",pickColor),0,4);
  muscleRow=Field("肌肉高亮程度（%）","Muscle highlight (%)",muscleHighlight);
  visuals.Controls.Add(muscleRow,0,5);
  savePreset=Button("保存本套服装设置","Save Costume Settings");savePreset.Width=212;
  savePreset.Click+=async(s,e)=>{
   int selected=outfit.SelectedIndex;
   var preset=new AfterimagePreset{Brightness=(int)brightness.Value,Opacity=100-(int)transparency.Value,
    MuscleHighlight=(int)muscleHighlight.Value,Red=tint.R,Green=tint.G,Blue=tint.B};
   await Run(root=>engine.SavePreset(root,selected,preset));
  };
  visuals.Controls.Add(Field("","",savePreset),0,6);
  details.Dock=DockStyle.Fill;details.Multiline=true;details.ReadOnly=true;details.ScrollBars=ScrollBars.Vertical;details.BackColor=Color.FromArgb(30,38,50);details.ForeColor=Color.Gainsboro;details.BorderStyle=BorderStyle.None;page.Controls.Add(details,0,4);
  var actions=new FlowLayoutPanel{Dock=DockStyle.Fill,Margin=new Padding(0,8,0,0)};launch=Button("启动游戏","Launch Game");launch.Width=220;launch.Click+=async(s,e)=>await Run(root=>String.Format(InstallerLanguage.T("游戏已启动，进程 {0}","Game launched. Process ID {0}."),engine.Launch(root)));restoreAll=Button("全部还原","Restore All");restoreAll.Width=180;restoreAll.Click+=async(s,e)=>await Run(root=>engine.RestoreAll(root));actions.Controls.Add(launch);actions.Controls.Add(restoreAll);page.Controls.Add(actions,0,5);
  ApplyLanguage();
  SetEnabled(false);Shown+=(s,e)=>{if(!String.IsNullOrWhiteSpace(initial))path.Text=initial;else{string here=AppDomain.CurrentDomain.BaseDirectory;var parent=Directory.GetParent(here.TrimEnd(Path.DirectorySeparatorChar));foreach(string root in new[]{here,parent==null?here:parent.FullName})if(File.Exists(Path.Combine(root,Payload.ExeName))){path.Text=root;break;}}RefreshState();};
 }
 static TableLayoutPanel Section(params int[] heights){
  var section=new TableLayoutPanel{Dock=DockStyle.Fill,Padding=new Padding(12),ColumnCount=1,AutoScroll=true};
  foreach(int height in heights)section.RowStyles.Add(new RowStyle(SizeType.Absolute,height));
  return section;
 }
 T Bind<T>(T control,string chinese,string english) where T:Control {
  translatedControls.Add(()=>control.Text=InstallerLanguage.T(chinese,english));
  return control;
 }
 void ApplyLanguage(){
  translating=true;
  int selection=Math.Max(0,outfit.SelectedIndex);
  foreach(var translate in translatedControls)translate();
  outfit.Items.Clear();
  outfit.Items.AddRange(AfterimagePreset.Labels);
  outfit.SelectedIndex=selection;
  SetTint(tint.IsEmpty?Color.FromArgb(144,163,253):tint);
  details.Text=InstallerLanguage.T(
   "各功能可独立安装和还原。安装后可直接打开游戏 EXE。\r\n残影设置保存后，运行中的游戏会自动重载。",
   "Install and restore each feature separately. Open the game EXE after installing.\r\nAfterimage settings reload automatically when saved.");
  translating=false;
 }
 Control Field(string chinese,string english,Control value){
  var row=new FlowLayoutPanel{Dock=DockStyle.Fill,WrapContents=false};
  row.Controls.Add(Bind(new Label{Width=160,Height=30,TextAlign=ContentAlignment.MiddleLeft},chinese,english));
  value.Width=Math.Max(212,value.Width);row.Controls.Add(value);return row;
 }
 Button Button(string chinese,string english){return Bind(new Button{Height=38,Width=155,FlatStyle=FlatStyle.Flat,BackColor=Color.FromArgb(42,69,84),ForeColor=Color.White,Margin=new Padding(0,0,10,0)},chinese,english);}
 static Control Card(Label state,Button install,Button restore){var card=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=3,Padding=new Padding(12),BackColor=Color.FromArgb(30,39,51),Margin=new Padding(0,0,0,10)};card.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));card.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,235));card.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,175));state.Dock=DockStyle.Fill;state.TextAlign=ContentAlignment.MiddleLeft;card.Controls.Add(state,0,0);install.Dock=restore.Dock=DockStyle.Fill;card.Controls.Add(install,1,0);card.Controls.Add(restore,2,0);return card;}
 void SetEnabled(bool value){foreach(var control in new Control[]{installRumble,restoreRumble,installDS,restoreDS,installAfterimage,restoreAfterimage,launch,restoreAll,outfit,brightness,transparency,pickColor,muscleHighlight,savePreset})control.Enabled=value;}
 void Browse(){using(var dialog=new OpenFileDialog{Title=InstallerLanguage.T("选择游戏 EXE","Select Game EXE"),Filter=InstallerLanguage.T("西格玛1","Ninja Gaiden Sigma")+"|NINJA GAIDEN SIGMA.exe",CheckFileExists=true})if(dialog.ShowDialog(this)==DialogResult.OK){path.Text=Path.GetDirectoryName(dialog.FileName);RefreshState();}}
 void SetTint(Color color){tint=color;pickColor.BackColor=color;pickColor.ForeColor=color.GetBrightness()>0.55?Color.Black:Color.White;pickColor.Text=String.Format("#{0:X2}{1:X2}{2:X2}",color.R,color.G,color.B);}
 void RefreshPreset(){
  if(outfit.SelectedIndex<0)return;
  bool showMuscle=AfterimagePreset.ShowsMuscleHighlight(outfit.SelectedIndex);
  muscleRow.Visible=showMuscle;
  visuals.RowStyles[5].Height=showMuscle?38:0;
  var preset=new AfterimagePreset();
  try{if(!String.IsNullOrWhiteSpace(path.Text))preset=engine.LoadPreset(path.Text,outfit.SelectedIndex);}
  catch(Exception error){details.Text=error.Message;}
  brightness.Value=preset.Brightness;transparency.Value=100-preset.Opacity;SetTint(Color.FromArgb(preset.Red,preset.Green,preset.Blue));
  muscleHighlight.Value=preset.MuscleHighlight;
 }
 void RefreshState(bool refreshPreset=true){
  if(refreshPreset)RefreshPreset();
  bool hasPath=!String.IsNullOrWhiteSpace(path.Text);
  var info=hasPath?engine.Inspect(path.Text):new FeatureInspection();
  status.Text=hasPath?info.Description:InstallerLanguage.T(
   "请选择游戏。安装、更新或还原功能前，请先正常退出游戏。",
   "Select the game EXE. Exit the game before installing, updating, or restoring features.");
  rumbleState.Text=InstallerLanguage.T("震动补丁","Rumble")+InstalledSuffix(info.Rumble);
  dsState.Text=(IncludesIcons?InstallerLanguage.T("PS5 手柄＋自动图标","DualSense + Icons"):
   InstallerLanguage.T("PS5 手柄","DualSense"))+InstalledSuffix(info.DualSense);
  afterimageState.Text=InstallerLanguage.T("服装残影","Costume Afterimages")+InstalledSuffix(info.Afterimage);
  installRumble.Enabled=installDS.Enabled=info.CanInstall;restoreRumble.Enabled=info.Rumble;restoreDS.Enabled=info.DualSense;
  installAfterimage.Enabled=info.CanInstallAfterimage;restoreAfterimage.Enabled=info.Afterimage;
  outfit.Enabled=brightness.Enabled=transparency.Enabled=pickColor.Enabled=muscleHighlight.Enabled=savePreset.Enabled=info.Afterimage&&info.CanInstallAfterimage;
  restoreAll.Enabled=info.CanRestore;launch.Enabled=hasPath&&(info.CanInstall||File.Exists(Path.Combine(path.Text,Payload.ExeName)));
 }
 static string InstalledSuffix(bool active){return " · "+(active?InstallerLanguage.T("已安装","Installed"):InstallerLanguage.T("未安装","Not installed"));}
 async Task Run(Func<string,string> action){string root=path.Text;SetEnabled(false);browse.Enabled=false;language.Enabled=false;UseWaitCursor=true;try{details.Text=await Task.Run(()=>action(root));}catch(Exception error){details.Text=error.Message;}finally{RefreshState();browse.Enabled=true;language.Enabled=true;UseWaitCursor=false;}}
}
}
