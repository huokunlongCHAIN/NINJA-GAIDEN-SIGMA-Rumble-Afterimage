using System;
using System.IO;
using System.Collections.Generic;
using System.Reflection;
using System.Security.Cryptography;

namespace NgsRumblePatch {
public sealed class PatchFile {
 public string Name,Hash;public byte[] Bytes;
}
public static class Payload {
 public const string Version="0.6.6";
 public const string Core="0.7.5";
 public const string ExeName="NINJA GAIDEN SIGMA.exe";
 public const string ExeHash="15B7778118F8B1D0E0EE98A4DFF03DA9F99A3ABA4C7B3BF4F2CA157674B614C7";
 public const string StateFolder=".NGS1RumblePatch";
 public static readonly string[] Names={"dinput8.dll","NGS1Rumble.ini","NGS1RumbleRules.ini","NGS1RumbleWaves.ini"};
 public static readonly HashSet<string> KnownCores=new HashSet<string>(StringComparer.OrdinalIgnoreCase){
  "BEBCD595C6EF9C7040E8089A9270EFD6EDDB7381A0BA9174204CF498964A499C",
  "5E6E8217AB43A5EE1B03ADE9416A3F9EEC6F0AA6889AF61D19CC30C4CEF7BFBE",
  "FBD46B6C069497D7969FD687C24820AD945883BAAE16E2CB5CF73EE0BE0C93A7",
  "6DD886435DE6CBB22A88347063547955B609C84C63DFCBED41307344E4919327",
  PayloadHashes.Values["dinput8.dll"]
 };
 static Payload(){foreach(string hash in ReleasedCoreHashes.Values)KnownCores.Add(hash);}
 public static string Hash(byte[] bytes){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-","");}
 public static string FileHash(string path){using(var sha=SHA256.Create())using(var s=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read))return BitConverter.ToString(sha.ComputeHash(s)).Replace("-","");}
 public static List<PatchFile> Load(){
  var result=new List<PatchFile>();var assembly=Assembly.GetExecutingAssembly();
  var expected=PayloadHashes.Values;
  foreach(string name in Names)using(var input=assembly.GetManifestResourceStream("Payload."+name)){
   if(input==null)throw new Exception(InstallerLanguage.T("安装包缺少内置文件：", "The installer is missing an embedded file: ")+name);
   using(var buffer=new MemoryStream()){input.CopyTo(buffer);var bytes=buffer.ToArray();string hash=Hash(bytes);
    if(hash!=expected[name])throw new Exception(InstallerLanguage.T("内置文件校验失败：", "Embedded file verification failed: ")+name);
    result.Add(new PatchFile{Name=name,Bytes=bytes,Hash=hash});}
  }
  return result;
 }
 public static PatchFile LoadExtra(string name){
  using(var input=Assembly.GetExecutingAssembly().GetManifestResourceStream("Payload."+name)){
   if(input==null)throw new Exception(InstallerLanguage.T("安装包缺少自动加载器。", "The installer is missing the autoloader."));using(var buffer=new MemoryStream()){input.CopyTo(buffer);byte[] bytes=buffer.ToArray();string hash=Hash(bytes);if(hash!=PayloadHashes.Values[name])throw new Exception(InstallerLanguage.T("自动加载器校验失败。", "Autoloader verification failed."));return new PatchFile{Name=name,Bytes=bytes,Hash=hash};}
  }
 }
}
}
