using System;
using System.IO;
using System.Text;
using System.Linq;
using System.Security.Cryptography;
using System.Collections.Generic;

namespace NgsRumblePatch {
public sealed class CompatibilityInfo {public bool Compatible,AfterimageCompatible;public string Family,Reason,CoreVersion;}
public static class GameCompatibility {
 sealed class Section {public string Name;public uint Rva,VirtualSize,RawSize,Raw,Flags;}
 sealed class Profile {public string Version,Family,CodeHash,UnwindHash,ExactHash;public uint TextVirtual,TextRaw,DataVirtual,Entry;public bool Aligned;}
 static readonly Profile[] Profiles={
  new Profile{Version="1.0.0.2",Family="original-code",TextVirtual=9545644,TextRaw=9545728,DataVirtual=58438268,Entry=0x83bd08,CodeHash="FA7D1B9AB50F64492A1534416BDB3C337BE93D3CE05C4902A6C22A89E4A94046",UnwindHash="DC27B2B7A5B54EC068FCC28EE7A2EA4F67BCDC720313DB9B3942C17FE38B921D"},
  new Profile{Version="1.0.0.2",Family="localized-code",TextVirtual=9545644,TextRaw=9545728,DataVirtual=58438268,Entry=0x83bd08,CodeHash="DE08180112CD7A27F0F3DFF0214AF0387C771F6FD35427DC02D75A56C4F96909",UnwindHash="DC27B2B7A5B54EC068FCC28EE7A2EA4F67BCDC720313DB9B3942C17FE38B921D"},
  new Profile{Version="1.0.0.4",Family="original-code",TextVirtual=9545740,TextRaw=9546240,DataVirtual=58438236,Entry=0x83bd68,CodeHash="8D940668F039CAF52E68083BE53C7F3D2A52131D908D52A64295C65F980B13D4",UnwindHash="4A86A4DD23E0CC9D58DC556FC71E3F3D31009B15E14D0A89C85F8871705D788C"},
  new Profile{Version="1.0.0.4",Family="original-code",Aligned=true,TextVirtual=9547776,TextRaw=9546240,DataVirtual=58441728,Entry=0x83bd68,CodeHash="AF01591152D1A11C0F3A0A6C6080DC588EA3EEFE11BFF07C7383195ADD6020EF",UnwindHash="4A86A4DD23E0CC9D58DC556FC71E3F3D31009B15E14D0A89C85F8871705D788C",ExactHash="E9F77BA801470403B819A0CDD21BB02B472C06A16952515160FE355888F85459"}
 };
 static string Hash(byte[] bytes,int start,int count){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(bytes,start,count)).Replace("-","");}
 static CompatibilityInfo Bad(string reason){return new CompatibilityInfo{Compatible=false,Family="unsupported",Reason=reason+" 请确认未安装其他会修改游戏程序的补丁。"};}
 public static CompatibilityInfo Inspect(string path){try{if(!File.Exists(path))return Bad("找不到游戏 EXE。");long bytes=new FileInfo(path).Length;if(bytes<328||bytes>128*1024*1024)return Bad("EXE 大小不属于已验证的程序结构。");return Inspect(File.ReadAllBytes(path));}catch(Exception e){return Bad("无法检查游戏程序："+e.Message);}}
 public static CompatibilityInfo Inspect(byte[] bytes){
  try{
   if(bytes==null||bytes.Length<328||bytes.Length>128*1024*1024||BitConverter.ToUInt16(bytes,0)!=0x5a4d)return Bad("不是兼容的 x64 PE 程序。");
   uint offset=BitConverter.ToUInt32(bytes,0x3c);if(offset<64||offset>bytes.Length-264||BitConverter.ToUInt32(bytes,(int)offset)!=0x4550)return Bad("PE 头无效。");
   int pe=(int)offset,opt=pe+24;ushort count=BitConverter.ToUInt16(bytes,pe+6),optional=BitConverter.ToUInt16(bytes,pe+20);uint imageSize=BitConverter.ToUInt32(bytes,opt+56),entry=BitConverter.ToUInt32(bytes,opt+16);
   if(BitConverter.ToUInt16(bytes,pe+4)!=0x8664||optional!=240||BitConverter.ToUInt16(bytes,opt)!=0x20b||count<4||count>96||BitConverter.ToUInt64(bytes,opt+24)!=0x140000000UL||imageSize<0x435f000||imageSize>0x6000000)return Bad("游戏程序布局不同，需要单独适配。");
   int table=opt+optional;if((long)table+count*40>bytes.Length)return Bad("PE 节表被截断。");var sections=new List<Section>();
   for(int i=0;i<count;++i){int at=table+i*40;var section=new Section{Name=Encoding.ASCII.GetString(bytes,at,8).TrimEnd('\0'),VirtualSize=BitConverter.ToUInt32(bytes,at+8),Rva=BitConverter.ToUInt32(bytes,at+12),RawSize=BitConverter.ToUInt32(bytes,at+16),Raw=BitConverter.ToUInt32(bytes,at+20),Flags=BitConverter.ToUInt32(bytes,at+36)};if((ulong)section.Raw+section.RawSize>(ulong)bytes.Length||(ulong)section.Rva+section.VirtualSize>imageSize)return Bad("PE 节数据被截断或超出映像。");sections.Add(section);}
   string[] names={".text",".rdata",".data",".pdata"};if(names.Any(n=>sections.Count(s=>s.Name==n)!=1))return Bad("关键节缺失或重复。");
   var text=sections.Single(s=>s.Name==".text");string codeHash=Hash(bytes,(int)text.Raw,(int)text.RawSize);var profile=Profiles.SingleOrDefault(p=>p.CodeHash==codeHash);if(profile==null)return Bad("核心代码尚未验证。当前已定位 1.0.0.2／1.0.0.4；其它核心构建需单独适配。");
   if(profile.ExactHash!=null&&Hash(bytes,0,bytes.Length)!=profile.ExactHash)return Bad("此 1.0.0.4 构建的完整程序指纹不匹配。");
   uint[,] layout={{0x1000,profile.TextVirtual,profile.TextRaw},{0x91c000,profile.Aligned?2252800u:2250382u,2250752},{0xb42000,profile.DataVirtual,871936},{0x42fe000,profile.Aligned?397312u:397152u,397312}};
   for(int i=0;i<4;++i){var s=sections.Single(x=>x.Name==names[i]);if(s.Rva!=layout[i,0]||s.VirtualSize!=layout[i,1]||s.RawSize!=layout[i,2])return Bad("关键代码／数据节布局不同。");}
   // Retaining/removing a loader tail or extending resources does not change
   // the validated game code, data addresses, IAT, or unwind table.
   bool loaderEntry=sections.Any(s=>s.Name==".bind"&&(s.Flags&0x20000000)!=0&&entry>=s.Rva&&(ulong)entry<(ulong)s.Rva+s.VirtualSize);
   if(entry!=profile.Entry&&!loaderEntry)return Bad("游戏入口不属于此核心的已知启动方式。");
   Func<uint,ulong,bool> anchor=(rva,expected)=>{foreach(var s in sections){if(rva<s.Rva)continue;ulong delta=(ulong)rva-s.Rva;if(delta+8>s.RawSize||(ulong)s.Raw+delta+8>(ulong)bytes.Length)continue;return BitConverter.ToUInt64(bytes,(int)((ulong)s.Raw+delta))==expected;}return false;};
   if(!anchor(0x959990,0x14057e6a0UL)||!anchor(0x91c900,0x8000000000000002UL)||!anchor(0x91c8f8,0x8000000000000003UL)||!anchor(0x91c908,0x8000000000000004UL))return Bad("输入接口或虚表位置不匹配。");
   var pdata=sections.Single(s=>s.Name==".pdata");if(Hash(bytes,(int)pdata.Raw,(int)pdata.RawSize)!=profile.UnwindHash)return Bad("函数展开表不匹配。");
   string frameHash=profile.Version=="1.0.0.4"?"4AE68FD9E758F585BDCD6A05E4F037F538A0EBA33BEA0B47A7A6E3D7BA495D01":"96EDFA9CE9E1E2342AC3015BC206892B8EF86D27B7FA53EF6AD7781D511DFDA0";
   bool afterimage=Hash(bytes,(int)text.Raw+0x35780-(int)text.Rva,0x218)==frameHash;
   return new CompatibilityInfo{Compatible=true,AfterimageCompatible=afterimage,Family=profile.Family,CoreVersion=profile.Version,Reason="已验证 "+profile.Version+" "+(profile.Aligned?"独立构建":"核心")+"。"};
  }catch{return Bad("游戏程序结构不完整或存在歧义。");}
 }
 public static void Validate(string path){var result=Inspect(path);if(!result.Compatible)throw new Exception("此游戏版本尚未验证，未安装任何文件。"+result.Reason);}
}
}
