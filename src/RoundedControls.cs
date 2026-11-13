using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace NgsRumblePatch {
static class RoundedShape {
 public static GraphicsPath Path(Rectangle bounds,int radius){
  var path=new GraphicsPath();
  int diameter=Math.Min(radius*2,Math.Min(bounds.Width,bounds.Height));
  if(diameter<=0){path.AddRectangle(bounds);return path;}
  path.AddArc(bounds.Left,bounds.Top,diameter,diameter,180,90);
  path.AddArc(bounds.Right-diameter,bounds.Top,diameter,diameter,270,90);
  path.AddArc(bounds.Right-diameter,bounds.Bottom-diameter,diameter,diameter,0,90);
  path.AddArc(bounds.Left,bounds.Bottom-diameter,diameter,diameter,90,90);
  path.CloseFigure();
  return path;
 }
}

enum ButtonGlyph { None, Folder, Download, Restore, Save, Play }

sealed class RoundedButton:Button {
 bool hover,pressed;
 public RoundedButton(){
  SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|
   ControlStyles.SupportsTransparentBackColor,true);
  BackColor=Color.Transparent;
  ForeColor=Color.White;
 }
 public Color FillColor {get;set;}
 public ButtonGlyph Glyph {get;set;}
 protected override void OnMouseEnter(EventArgs e){base.OnMouseEnter(e);hover=true;Invalidate();}
 protected override void OnMouseLeave(EventArgs e){base.OnMouseLeave(e);hover=false;Invalidate();}
 protected override void OnMouseDown(MouseEventArgs e){base.OnMouseDown(e);pressed=e.Button==MouseButtons.Left;Invalidate();}
 protected override void OnMouseUp(MouseEventArgs e){base.OnMouseUp(e);pressed=false;Invalidate();}
 protected override void OnMouseCaptureChanged(EventArgs e){base.OnMouseCaptureChanged(e);if(!Capture){pressed=false;Invalidate();}}
 protected override void OnPaint(PaintEventArgs e){
  if(SystemInformation.HighContrast){base.OnPaint(e);return;}
  if(Width<2||Height<2)return;
  e.Graphics.SmoothingMode=SmoothingMode.AntiAlias;
  var bounds=new Rectangle(0,0,Width-1,Height-1);
  int radius=Height/2;
  Color fill=Enabled?(pressed?ControlPaint.Dark(FillColor,0.12f):hover?ControlPaint.Light(FillColor,0.16f):FillColor)
   :Color.FromArgb(49,56,68);
  using(var shape=RoundedShape.Path(bounds,radius))
  using(var brush=new SolidBrush(fill))
  using(var edge=new Pen(Focused&&ShowFocusCues?Color.FromArgb(166,195,255):Color.FromArgb(82,99,125))){
   e.Graphics.FillPath(brush,shape);
   e.Graphics.DrawPath(edge,shape);
  }
  Color ink=Enabled?ForeColor:Color.FromArgb(170,179,192);
  var textBounds=Rectangle.Inflate(bounds,-radius/2,0);
  var flags=TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis|
   (ShowKeyboardCues?(TextFormatFlags)0:TextFormatFlags.HidePrefix);
  if(Glyph==ButtonGlyph.None){
   TextRenderer.DrawText(e.Graphics,Text,Font,textBounds,ink,flags|TextFormatFlags.HorizontalCenter);
   return;
  }
  int iconSize=(int)Math.Round(e.Graphics.DpiX/96*15);
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
  using(var pen=new Pen(ink,1.5f){StartCap=LineCap.Round,EndCap=LineCap.Round,LineJoin=LineJoin.Round}){
   switch(Glyph){
    case ButtonGlyph.Folder:
     graphics.DrawPolygon(pen,new[]{new Point(1,13),new Point(1,4),new Point(6,4),new Point(8,6),new Point(15,6),new Point(14,13)});
     break;
    case ButtonGlyph.Download:
     graphics.DrawLine(pen,8,2,8,10);
     graphics.DrawLines(pen,new[]{new Point(4,6),new Point(8,10),new Point(12,6)});
     graphics.DrawLines(pen,new[]{new Point(2,11),new Point(2,14),new Point(14,14),new Point(14,11)});
     break;
    case ButtonGlyph.Restore:
     graphics.DrawArc(pen,2,3,12,11,205,290);
     graphics.DrawLines(pen,new[]{new Point(2,2),new Point(2,7),new Point(7,7)});
     break;
    case ButtonGlyph.Save:
     graphics.DrawLines(pen,new[]{new Point(2,8),new Point(6,12),new Point(14,4)});
     break;
    case ButtonGlyph.Play:
     graphics.DrawPolygon(pen,new[]{new Point(5,2),new Point(14,8),new Point(5,14)});
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
