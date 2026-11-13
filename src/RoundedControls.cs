using System;
using System.Drawing;
using System.Drawing.Drawing2D;
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

sealed class RoundedButton:Button {
 bool hover,pressed;
 public RoundedButton(){
  SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|
   ControlStyles.SupportsTransparentBackColor,true);
  BackColor=Color.Transparent;
  ForeColor=Color.White;
 }
 public Color FillColor {get;set;}
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
  int radius=(int)Math.Round(e.Graphics.DpiX/96*10);
  Color fill=Enabled?(pressed?ControlPaint.Dark(FillColor,0.12f):hover?ControlPaint.Light(FillColor,0.16f):FillColor)
   :Color.FromArgb(49,56,68);
  using(var shape=RoundedShape.Path(bounds,radius))
  using(var brush=new SolidBrush(fill))
  using(var edge=new Pen(Focused&&ShowFocusCues?Color.FromArgb(166,195,255):Color.FromArgb(82,99,125))){
   e.Graphics.FillPath(brush,shape);
   e.Graphics.DrawPath(edge,shape);
  }
  TextRenderer.DrawText(e.Graphics,Text,Font,Rectangle.Inflate(bounds,-6,0),
   Enabled?ForeColor:Color.FromArgb(170,179,192),
   TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|
   TextFormatFlags.EndEllipsis|(ShowKeyboardCues?(TextFormatFlags)0:TextFormatFlags.HidePrefix));
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
}
