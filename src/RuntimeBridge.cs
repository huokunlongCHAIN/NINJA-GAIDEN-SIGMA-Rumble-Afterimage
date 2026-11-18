using System;
using System.IO;
using System.Text;
using System.Linq;
using System.Diagnostics;
using System.IO.MemoryMappedFiles;
using System.Security.Cryptography;
using System.Collections.Generic;

namespace NgsRumblePatch {
public static class RuntimeBridge {
 public static string Hash(string path){return Payload.FileHash(path);}
 public static string Key(string root){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar).ToUpperInvariant()))).Replace("-","").Substring(0,24);}
 public static Process[] Games(string root){
  var found=new List<Process>();foreach(var process in Process.GetProcessesByName("NINJA GAIDEN SIGMA")){
   try{if(String.Equals(process.MainModule.FileName,Path.Combine(root,Payload.ExeName),StringComparison.OrdinalIgnoreCase)){found.Add(process);continue;}}
   catch(InvalidOperationException){}catch(System.ComponentModel.Win32Exception){}process.Dispose();
  }return found.ToArray();
 }
 public static bool Ready(int pid){try{using(var map=MemoryMappedFile.OpenExisting("Local\\NGS1RumbleMonitor-"+pid,MemoryMappedFileRights.Read))using(var view=map.CreateViewAccessor(0,648,MemoryMappedFileAccess.Read))return view.ReadUInt32(0)==0x4d52474e&&view.ReadUInt32(4)==1&&view.ReadUInt32(8)==648&&view.ReadUInt32(16)==pid&&view.ReadUInt32(20)==1;}catch(FileNotFoundException){return false;}catch(UnauthorizedAccessException){return false;}}
}
}
