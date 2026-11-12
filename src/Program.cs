using System;
using System.IO;
using System.Text;
using System.Drawing;
using System.Windows.Forms;
using System.Web.Script.Serialization;
using System.Threading.Tasks;
namespace NgsRumblePatch {
static class Program {
 [STAThread]static int Main(string[] args){
  try{if(InstallerSession.Initialize(ref args))return 0;}catch(Exception error){if(args.Length==3)File.WriteAllText(args[2],new JavaScriptSerializer().Serialize(new{ok=false,message=error.Message}),new UTF8Encoding(false));return 1;}
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
   }catch(Exception error){if(args.Length==3)File.WriteAllText(args[2],new JavaScriptSerializer().Serialize(new{ok=false,started=false,message=error.Message}),new UTF8Encoding(false));else MessageBox.Show(error.Message,"西格玛1 功能补丁");return 1;}}
  Show(null);return 0;
 }
 static void Show(string root){Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new FeatureForm(root));}
}
sealed class FeatureForm:Form {
 readonly FeatureFacade engine=new FeatureFacade();
 readonly TextBox path=new TextBox(),details=new TextBox();
 readonly Label status=new Label(),rumbleState=new Label(),dsState=new Label(),afterimageState=new Label();
 readonly ComboBox outfit=new ComboBox();
 readonly NumericUpDown brightness=new NumericUpDown(),transparency=new NumericUpDown(),muscleHighlight=new NumericUpDown();
 readonly TableLayoutPanel visuals;
 readonly Control muscleRow;
 Button browse,installRumble,restoreRumble,installDS,restoreDS,installAfterimage,restoreAfterimage,pickColor,savePreset,launch,restoreAll;
 Color tint;
 public FeatureForm(string initial=null){
  Text="西格玛1 · 功能补丁 "+Payload.Version;ClientSize=new Size(840,730);MinimumSize=new Size(780,680);StartPosition=FormStartPosition.CenterScreen;Font=new Font("Microsoft YaHei UI",10);BackColor=Color.FromArgb(23,29,38);ForeColor=Color.White;
  var page=new TableLayoutPanel{Dock=DockStyle.Fill,Padding=new Padding(24),ColumnCount=1,RowCount=6};
  foreach(int height in new[]{48,48,48})page.RowStyles.Add(new RowStyle(SizeType.Absolute,height));
  page.RowStyles.Add(new RowStyle(SizeType.Percent,100));
  page.RowStyles.Add(new RowStyle(SizeType.Absolute,82));
  page.RowStyles.Add(new RowStyle(SizeType.Absolute,52));
  Controls.Add(page);
  page.Controls.Add(new Label{Text="西格玛1 · 功能补丁",Font=new Font(Font.FontFamily,19,FontStyle.Bold),Dock=DockStyle.Fill},0,0);
  var choose=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2};choose.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));choose.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,150));path.ReadOnly=true;path.Dock=DockStyle.Fill;path.BackColor=Color.FromArgb(40,48,60);path.ForeColor=Color.White;path.Margin=new Padding(0,5,12,0);choose.Controls.Add(path,0,0);browse=Button("选择游戏 EXE");browse.Click+=(s,e)=>Browse();choose.Controls.Add(browse,1,0);page.Controls.Add(choose,0,1);
  status.Dock=DockStyle.Fill;status.ForeColor=Color.LightSteelBlue;status.Text="请选择游戏。安装、更新或还原功能前，请先正常退出游戏。";page.Controls.Add(status,0,2);
  var tabs=new TabControl{Dock=DockStyle.Fill,Margin=new Padding(0,0,0,12)};
  var controllers=new TabPage("震动与手柄"){BackColor=BackColor,ForeColor=ForeColor};
  var visual=new TabPage("残影"){BackColor=BackColor,ForeColor=ForeColor};
  tabs.TabPages.Add(controllers);tabs.TabPages.Add(visual);page.Controls.Add(tabs,0,3);
  installRumble=Button("安装／更新震动");restoreRumble=Button("还原震动");installDS=Button("安装／更新 PS5");restoreDS=Button("还原 PS5");
  var controllerPage=Section(70,70,56);
  controllers.Controls.Add(controllerPage);
  controllerPage.Controls.Add(Card("震动补丁",rumbleState,installRumble,restoreRumble),0,0);
  controllerPage.Controls.Add(Card(PayloadHashes.HasIconData?"PS5 手柄＋自动图标":"PS5 手柄",dsState,installDS,restoreDS),0,1);
  controllerPage.Controls.Add(new Label{Text=PayloadHashes.HasIconData?
   "PS5 手柄支持直接输入、热切换和 PS／Xbox 按键图标切换。":"PS5 手柄支持直接输入和热切换。图标切换可随本地图标数据构建。",
   Dock=DockStyle.Fill,ForeColor=Color.Silver},0,2);
  installRumble.Click+=async(s,e)=>await Run(root=>engine.InstallRumble(root));restoreRumble.Click+=async(s,e)=>await Run(root=>engine.RestoreRumble(root));installDS.Click+=async(s,e)=>await Run(root=>engine.InstallDualSense(root));restoreDS.Click+=async(s,e)=>await Run(root=>engine.RestoreDualSense(root));
  visuals=Section(70,38,38,38,44,38,44);
  visual.Controls.Add(visuals);
  installAfterimage=Button("安装／更新残影");restoreAfterimage=Button("还原残影");
  visuals.Controls.Add(Card("服装残影",afterimageState,installAfterimage,restoreAfterimage),0,0);
  installAfterimage.Click+=async(s,e)=>await Run(root=>engine.InstallAfterimage(root));
  restoreAfterimage.Click+=async(s,e)=>await Run(root=>engine.RestoreAfterimage(root));
  outfit.DropDownStyle=ComboBoxStyle.DropDownList;outfit.Items.AddRange(AfterimagePreset.Labels);outfit.SelectedIndex=0;
  outfit.SelectedIndexChanged+=(s,e)=>RefreshPreset();
  visuals.Controls.Add(Field("服装",outfit),0,1);
  foreach(var number in new[]{brightness,transparency,muscleHighlight}){number.Minimum=0;number.Maximum=100;}
  visuals.Controls.Add(Field("发光亮度（%）",brightness),0,2);
  visuals.Controls.Add(Field("透明度（%）",transparency),0,3);
  pickColor=Button("#90A3FD");
  pickColor.Click+=(s,e)=>{using(var dialog=new ColorDialog{Color=tint,FullOpen=true})if(dialog.ShowDialog(this)==DialogResult.OK)SetTint(dialog.Color);};
  visuals.Controls.Add(Field("残影颜色",pickColor),0,4);
  muscleRow=Field("肌肉高亮程度（%）",muscleHighlight);
  visuals.Controls.Add(muscleRow,0,5);
  savePreset=Button("保存本套服装设置");savePreset.Width=204;
  savePreset.Click+=async(s,e)=>{
   int selected=outfit.SelectedIndex;
   var preset=new AfterimagePreset{Brightness=(int)brightness.Value,Opacity=100-(int)transparency.Value,
    MuscleHighlight=(int)muscleHighlight.Value,Red=tint.R,Green=tint.G,Blue=tint.B};
   await Run(root=>engine.SavePreset(root,selected,preset));
  };
  visuals.Controls.Add(Field("",savePreset),0,6);
  details.Dock=DockStyle.Fill;details.Multiline=true;details.ReadOnly=true;details.ScrollBars=ScrollBars.Vertical;details.BackColor=Color.FromArgb(30,38,50);details.ForeColor=Color.Gainsboro;details.BorderStyle=BorderStyle.None;details.Text="各功能可独立安装和还原。安装后可直接打开游戏 EXE。\r\n残影设置保存后，运行中的游戏会自动重载。";page.Controls.Add(details,0,4);
  var actions=new FlowLayoutPanel{Dock=DockStyle.Fill,Margin=new Padding(0,8,0,0)};launch=Button("启动游戏");launch.Width=220;launch.Click+=async(s,e)=>await Run(root=>"游戏已启动，进程 "+engine.Launch(root));restoreAll=Button("全部还原");restoreAll.Width=180;restoreAll.Click+=async(s,e)=>await Run(root=>engine.RestoreAll(root));actions.Controls.Add(launch);actions.Controls.Add(restoreAll);page.Controls.Add(actions,0,5);
  SetEnabled(false);Shown+=(s,e)=>{if(!String.IsNullOrWhiteSpace(initial))path.Text=initial;else{string here=AppDomain.CurrentDomain.BaseDirectory;var parent=Directory.GetParent(here.TrimEnd(Path.DirectorySeparatorChar));foreach(string root in new[]{here,parent==null?here:parent.FullName})if(File.Exists(Path.Combine(root,Payload.ExeName))){path.Text=root;break;}}RefreshState();};
 }
 static TableLayoutPanel Section(params int[] heights){
  var section=new TableLayoutPanel{Dock=DockStyle.Fill,Padding=new Padding(12),ColumnCount=1,AutoScroll=true};
  foreach(int height in heights)section.RowStyles.Add(new RowStyle(SizeType.Absolute,height));
  return section;
 }
 static Control Field(string name,Control value){
  var row=new FlowLayoutPanel{Dock=DockStyle.Fill,WrapContents=false};
  row.Controls.Add(new Label{Text=name,Width=140,Height=30,TextAlign=ContentAlignment.MiddleLeft});
  value.Width=Math.Max(180,value.Width);row.Controls.Add(value);return row;
 }
 Button Button(string text){return new Button{Text=text,Height=38,Width=155,FlatStyle=FlatStyle.Flat,BackColor=Color.FromArgb(42,69,84),ForeColor=Color.White,Margin=new Padding(0,0,10,0)};}
 Control Card(string title,Label state,Button install,Button restore){var card=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=3,Padding=new Padding(12),BackColor=Color.FromArgb(30,39,51),Margin=new Padding(0,0,0,10)};card.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));card.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,180));card.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,145));state.Text=title+" · 未安装";state.Dock=DockStyle.Fill;state.TextAlign=ContentAlignment.MiddleLeft;state.Tag=title;card.Controls.Add(state,0,0);install.Dock=restore.Dock=DockStyle.Fill;card.Controls.Add(install,1,0);card.Controls.Add(restore,2,0);return card;}
 void SetEnabled(bool value){foreach(var control in new Control[]{installRumble,restoreRumble,installDS,restoreDS,installAfterimage,restoreAfterimage,launch,restoreAll,outfit,brightness,transparency,pickColor,muscleHighlight,savePreset})control.Enabled=value;}
 void Browse(){using(var dialog=new OpenFileDialog{Filter="西格玛1|NINJA GAIDEN SIGMA.exe",CheckFileExists=true})if(dialog.ShowDialog(this)==DialogResult.OK){path.Text=Path.GetDirectoryName(dialog.FileName);RefreshState();}}
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
 void RefreshState(){
  RefreshPreset();
  if(String.IsNullOrWhiteSpace(path.Text))return;
  var info=engine.Inspect(path.Text);status.Text=info.Description;
  foreach(var item in new[]{new{Label=rumbleState,Active=info.Rumble},new{Label=dsState,Active=info.DualSense},new{Label=afterimageState,Active=info.Afterimage}})
   item.Label.Text=item.Label.Tag+" · "+(item.Active?"已安装":"未安装");
  installRumble.Enabled=installDS.Enabled=info.CanInstall;restoreRumble.Enabled=info.Rumble;restoreDS.Enabled=info.DualSense;
  installAfterimage.Enabled=info.CanInstallAfterimage;restoreAfterimage.Enabled=info.Afterimage;
  outfit.Enabled=brightness.Enabled=transparency.Enabled=pickColor.Enabled=muscleHighlight.Enabled=savePreset.Enabled=info.Afterimage&&info.CanInstallAfterimage;
  restoreAll.Enabled=info.CanRestore;launch.Enabled=info.CanInstall||File.Exists(Path.Combine(path.Text,Payload.ExeName));
 }
 async Task Run(Func<string,string> action){string root=path.Text;SetEnabled(false);browse.Enabled=false;UseWaitCursor=true;try{details.Text=await Task.Run(()=>action(root));}catch(Exception error){details.Text=error.Message;}finally{RefreshState();browse.Enabled=true;UseWaitCursor=false;}}
}
}
