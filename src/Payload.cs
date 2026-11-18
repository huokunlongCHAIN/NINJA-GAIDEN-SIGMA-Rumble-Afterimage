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
 public const string Version="0.6.27";
 public const string Core="0.7.6";
 public const string ExeName="NINJA GAIDEN SIGMA.exe";
 public const string ExeHash="15B7778118F8B1D0E0EE98A4DFF03DA9F99A3ABA4C7B3BF4F2CA157674B614C7";
 public const string StateFolder=".NGS1RumblePatch";
 public static readonly string[] Names={"dinput8.dll","NGS1Rumble.ini","NGS1RumbleRules.ini","NGS1RumbleWaves.ini"};
 public static readonly string[] ExtraNames={"NGS1CrashTrace.exe"};
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
 public static string DirectoryPath {get{return Path.Combine(Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location),"Payload");}}
 static string ExternalPath(string name){
  if(!Array.Exists(Names,n=>n==name)&&!Array.Exists(ExtraNames,n=>n==name))throw new ArgumentException("Unknown package component.","name");
  string directory=DirectoryPath,path=Path.Combine(directory,name);
  if(!Directory.Exists(directory)||!File.Exists(path))throw new IOException(InstallerLanguage.T(
   "安装包不完整，请完整解压并保留安装器旁的 Payload 文件夹。缺少：",
   "The package is incomplete. Extract the entire ZIP and keep the Payload folder beside the installer. Missing: ")+name);
  if((File.GetAttributes(directory)&FileAttributes.ReparsePoint)!=0||(File.GetAttributes(path)&FileAttributes.ReparsePoint)!=0)
   throw new IOException(InstallerLanguage.T("安装组件不能是链接：","Package components must not be links: ")+name);
  if(new FileInfo(path).Length>32*1024*1024)throw new IOException("Package component exceeds the size limit: "+name);
  return path;
 }
 static PatchFile Read(string name){
  byte[] bytes=File.ReadAllBytes(ExternalPath(name));string hash=Hash(bytes);
  if(hash!=PayloadHashes.Values[name])throw new IOException(InstallerLanguage.T(
   "安装组件校验失败，未安装该文件。请重新获取完整的同版本安装包：",
   "Package checksum mismatch. This file was not installed. Obtain the complete matching release: ")+name);
  return new PatchFile{Name=name,Bytes=bytes,Hash=hash};
 }
 public static void ValidateAll(){foreach(string name in Names)Read(name);foreach(string name in ExtraNames)Read(name);}
 public static string VerifiedExtraPath(string name){Read(name);return ExternalPath(name);}
 public static List<PatchFile> Load(){
  var result=new List<PatchFile>();foreach(string name in Names)result.Add(Read(name));
  return result;
 }
 public static PatchFile LoadExtra(string name){return Read(name);}
}
}
