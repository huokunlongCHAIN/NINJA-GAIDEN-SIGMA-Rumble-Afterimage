using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace NgsRumblePatch {
static class RoundedShape {
 public static GraphicsPath Path(RectangleF bounds,float radius){
  var path=new GraphicsPath();
  float diameter=Math.Min(radius*2,Math.Min(bounds.Width,bounds.Height));
  if(diameter<=0){path.AddRectangle(bounds);return path;}
  if(diameter==bounds.Height){
   path.AddArc(bounds.Left,bounds.Top,diameter,diameter,90,180);
   path.AddArc(bounds.Right-diameter,bounds.Top,diameter,diameter,270,180);
   path.CloseFigure();
   return path;
  }
  path.AddArc(bounds.Left,bounds.Top,diameter,diameter,180,90);
  path.AddArc(bounds.Right-diameter,bounds.Top,diameter,diameter,270,90);
  path.AddArc(bounds.Right-diameter,bounds.Bottom-diameter,diameter,diameter,0,90);
  path.AddArc(bounds.Left,bounds.Bottom-diameter,diameter,diameter,90,90);
  path.CloseFigure();
  return path;
 }
}

enum ButtonGlyph { None, Folder, Download, Restore, Save, Play, Export }

sealed class RoundedButton:Button {
 bool hover,pressed;
 public RoundedButton(){
  FlatStyle=FlatStyle.Flat;
  FlatAppearance.BorderSize=0;
  SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|
   ControlStyles.SupportsTransparentBackColor|ControlStyles.ResizeRedraw,true);
  BackColor=Color.Transparent;
  ForeColor=Color.White;
 }
 public Color FillColor {get;set;}
 public ButtonGlyph Glyph {get;set;}
 protected override void OnSizeChanged(EventArgs e){
  base.OnSizeChanged(e);
  if(Width<2||Height<2)return;
  var old=Region;
  using(var shape=RoundedShape.Path(ClientRectangle,Height/2f))Region=new Region(shape);
  if(old!=null)old.Dispose();
 }
 protected override void OnMouseEnter(EventArgs e){base.OnMouseEnter(e);hover=true;Invalidate();}
 protected override void OnMouseLeave(EventArgs e){base.OnMouseLeave(e);hover=false;Invalidate();}
 protected override void OnMouseDown(MouseEventArgs e){base.OnMouseDown(e);pressed=e.Button==MouseButtons.Left;Invalidate();}
 protected override void OnMouseUp(MouseEventArgs e){base.OnMouseUp(e);pressed=false;Invalidate();}
 protected override void OnMouseCaptureChanged(EventArgs e){base.OnMouseCaptureChanged(e);if(!Capture){pressed=false;Invalidate();}}
 protected override void OnPaint(PaintEventArgs e){
  if(Width<4||Height<4)return;
  e.Graphics.SmoothingMode=SmoothingMode.AntiAlias;
  e.Graphics.PixelOffsetMode=PixelOffsetMode.HighQuality;
  var bounds=Rectangle.Inflate(ClientRectangle,-2,-2);
  int radius=bounds.Height/2;
  bool highContrast=SystemInformation.HighContrast;
  bool focused=Focused&&ShowFocusCues;
  Color fill=Enabled?(pressed?ControlPaint.Dark(FillColor,0.12f):hover?ControlPaint.Light(FillColor,0.16f):FillColor)
   :Color.FromArgb(49,56,68);
  Color edgeColor=focused?Color.FromArgb(178,198,254):Color.FromArgb(78,101,128);
  Color ink=Enabled?ForeColor:Color.FromArgb(170,179,192);
  if(highContrast){
   fill=SystemColors.Control;
   edgeColor=focused?SystemColors.Highlight:SystemColors.ControlText;
   ink=Enabled?SystemColors.ControlText:SystemColors.GrayText;
  }
  using(var shape=RoundedShape.Path(bounds,bounds.Height/2f))
  using(var brush=new SolidBrush(fill))
  using(var edge=new Pen(edgeColor,focused?1.75f:1.0f)){
   e.Graphics.FillPath(brush,shape);
   e.Graphics.DrawPath(edge,shape);
  }
  var textBounds=Rectangle.Inflate(bounds,-radius/2,0);
  var flags=TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis|
   (ShowKeyboardCues?(TextFormatFlags)0:TextFormatFlags.HidePrefix);
  if(Glyph==ButtonGlyph.None){
   TextRenderer.DrawText(e.Graphics,Text,Font,textBounds,ink,flags|TextFormatFlags.HorizontalCenter);
   return;
  }
  int iconSize=(int)Math.Round(e.Graphics.DpiX/96*17);
  int spacing=iconSize/2;
  int textWidth=TextRenderer.MeasureText(e.Graphics,Text,Font,textBounds.Size,flags).Width;
  int x=textBounds.X+Math.Max(0,(textBounds.Width-textWidth-iconSize-spacing)/2);
  GraphicsState saved=e.Graphics.Save();
  try{
   e.Graphics.TranslateTransform(x,(Height-iconSize)/2f);
   e.Graphics.ScaleTransform(iconSize/16f,iconSize/16f);
   DrawGlyph(e.Graphics,ink);
  }finally{e.Graphics.Restore(saved);}
  textBounds=Rectangle.FromLTRB(x+iconSize+spacing,textBounds.Top,textBounds.Right,textBounds.Bottom);
  TextRenderer.DrawText(e.Graphics,Text,Font,textBounds,ink,flags|TextFormatFlags.Left);
 }
 void DrawGlyph(Graphics graphics,Color ink){
  using(var pen=new Pen(ink,1.6f){StartCap=LineCap.Round,EndCap=LineCap.Round,LineJoin=LineJoin.Round}){
   switch(Glyph){
    case ButtonGlyph.Folder:
     using(var folder=new GraphicsPath()){
      folder.AddLine(2,7,2,5);
      folder.AddArc(2,3,4,4,180,90);
      folder.AddLines(new[]{new Point(4,3),new Point(6,3),new Point(8,5),new Point(12,5)});
      folder.AddArc(10,5,4,4,270,90);
      folder.AddLine(14,7,14,11);
      folder.AddArc(10,9,4,4,0,90);
      folder.AddLine(12,13,4,13);
      folder.AddArc(2,9,4,4,90,90);
      folder.CloseFigure();
      graphics.DrawPath(pen,folder);
     }
     break;
    case ButtonGlyph.Download:
     graphics.DrawLine(pen,8,2,8,9);
     graphics.DrawLines(pen,new[]{new Point(5,6),new Point(8,9),new Point(11,6)});
     graphics.DrawLines(pen,new[]{new Point(2,10),new Point(2,12),new Point(4,14),new Point(12,14),new Point(14,12),new Point(14,10)});
     break;
    case ButtonGlyph.Restore:
     graphics.DrawArc(pen,2.5f,2.5f,11,11,205,280);
     graphics.DrawLines(pen,new[]{new Point(2,3),new Point(2,7),new Point(6,7)});
     break;
    case ButtonGlyph.Save:
     graphics.DrawLines(pen,new[]{new Point(3,8),new Point(6,11),new Point(13,4)});
     break;
    case ButtonGlyph.Play:
     using(var play=new SolidBrush(ink))
      graphics.FillPolygon(play,new[]{new Point(5,3),new Point(13,8),new Point(5,13)});
     break;
    case ButtonGlyph.Export:
     using(var sheet=RoundedShape.Path(new Rectangle(3,2,10,12),2))graphics.DrawPath(pen,sheet);
     graphics.DrawLine(pen,6,6,10,6);
     graphics.DrawLine(pen,6,9,10,9);
     break;
   }
  }
 }
}

sealed class RoundedCard:TableLayoutPanel {
 public Color FillColor {get;set;}
 public RoundedCard(){DoubleBuffered=true;}
 protected override void OnPaintBackground(PaintEventArgs e){
  base.OnPaintBackground(e);
  if(Width<2||Height<2)return;
  e.Graphics.SmoothingMode=SmoothingMode.AntiAlias;
  int radius=(int)Math.Round(e.Graphics.DpiX/96*16);
  using(var shape=RoundedShape.Path(new Rectangle(0,0,Width-1,Height-1),radius))
  using(var brush=new SolidBrush(SystemInformation.HighContrast?SystemColors.Control:FillColor))
   e.Graphics.FillPath(brush,shape);
 }
}

static class InstallerIcon {
 [DllImport("user32.dll")]
 static extern bool DestroyIcon(IntPtr handle);
 public static Icon Create(){
  using(var bitmap=new Bitmap(32,32))
  using(var graphics=Graphics.FromImage(bitmap))
  using(var background=RoundedShape.Path(new Rectangle(0,0,31,31),8))
  using(var fill=new SolidBrush(Color.FromArgb(33,42,61))){
   graphics.SmoothingMode=SmoothingMode.AntiAlias;
   graphics.FillPath(fill,background);
   var shades=new[]{Color.FromArgb(116,113,178),Color.FromArgb(153,157,233),Color.FromArgb(219,225,255)};
   for(int i=0;i<shades.Length;i++){
    using(var pen=new Pen(shades[i],2.5f){StartCap=LineCap.Round,EndCap=LineCap.Round})
     graphics.DrawLine(pen,6+i*5,21,16+i*5,10);
   }
   IntPtr handle=bitmap.GetHicon();
   try{using(var icon=Icon.FromHandle(handle))return (Icon)icon.Clone();}
   finally{DestroyIcon(handle);}
  }
 }
}
}
