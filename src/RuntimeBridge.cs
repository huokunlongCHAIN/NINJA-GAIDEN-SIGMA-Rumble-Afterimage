using System;
using System.IO;
using System.Text;
using System.Linq;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.IO.MemoryMappedFiles;
using System.Security.Cryptography;
using System.Collections.Generic;

namespace NgsRumblePatch {
public static class RuntimeBridge {
 public static string Hash(string path){using(var sha=SHA256.Create())using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete))return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","");}
 public static string Key(string root){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar).ToUpperInvariant()))).Replace("-","").Substring(0,24);}
 public static Process[] Games(string root){
  var found=new List<Process>();foreach(var process in Process.GetProcessesByName("NINJA GAIDEN SIGMA")){
   try{if(String.Equals(process.MainModule.FileName,Path.Combine(root,"NINJA GAIDEN SIGMA.exe"),StringComparison.OrdinalIgnoreCase)){found.Add(process);continue;}}
   catch(InvalidOperationException){}catch(System.ComponentModel.Win32Exception){}process.Dispose();
  }return found.ToArray();
 }
 public static bool Ready(int pid){try{using(var map=MemoryMappedFile.OpenExisting("Local\\NGS1RumbleMonitor-"+pid,MemoryMappedFileRights.Read))using(var view=map.CreateViewAccessor(0,648,MemoryMappedFileAccess.Read))return view.ReadUInt32(0)==0x4d52474e&&view.ReadUInt32(4)==1&&view.ReadUInt32(8)==648&&view.ReadUInt32(16)==pid&&view.ReadUInt32(20)==1;}catch(FileNotFoundException){return false;}catch(UnauthorizedAccessException){return false;}}
 public static void Inject(Process process,string path){
  process.Refresh();if(process.Modules.Cast<ProcessModule>().Any(m=>String.Equals(m.FileName,path,StringComparison.OrdinalIgnoreCase)))return;
  IntPtr local=GetProcAddress(GetModuleHandle("kernel32.dll"),"LoadLibraryW"),owner;
  if(local==IntPtr.Zero||!GetModuleHandleEx(6,local,out owner))throw new Exception(InstallerLanguage.T("无法定位系统加载器。", "Could not locate the Windows loader."));
  var filename=new StringBuilder(32768);GetModuleFileName(owner,filename,filename.Capacity);string name=Path.GetFileName(filename.ToString());
  var remote=process.Modules.Cast<ProcessModule>().SingleOrDefault(m=>String.Equals(Path.GetFileName(m.FileName),name,StringComparison.OrdinalIgnoreCase));if(remote==null)throw new Exception(InstallerLanguage.T("目标进程缺少匹配的系统加载器。", "The game process has no matching Windows loader."));
  IntPtr entry=new IntPtr(remote.BaseAddress.ToInt64()+local.ToInt64()-owner.ToInt64());byte[] data=Encoding.Unicode.GetBytes(path+"\0");IntPtr handle=OpenProcess(0x43a,false,process.Id),buffer=IntPtr.Zero,thread=IntPtr.Zero;bool finished=false;
  if(handle==IntPtr.Zero)throw new Exception(InstallerLanguage.T("无法访问游戏进程，错误 ", "Could not access the game process. Windows error ")+Marshal.GetLastWin32Error());
  try{
   buffer=VirtualAllocEx(handle,IntPtr.Zero,(UIntPtr)data.Length,0x3000,4);UIntPtr written;
   if(buffer==IntPtr.Zero||!WriteProcessMemory(handle,buffer,data,(UIntPtr)data.Length,out written)||written.ToUInt64()!=(ulong)data.Length)throw new Exception(InstallerLanguage.T("无法传递运行库路径，错误 ", "Could not send the runtime path. Windows error ")+Marshal.GetLastWin32Error());
   thread=CreateRemoteThread(handle,IntPtr.Zero,UIntPtr.Zero,entry,buffer,0,IntPtr.Zero);if(thread==IntPtr.Zero)throw new Exception(InstallerLanguage.T("无法加载震动运行库，错误 ", "Could not load the patch runtime. Windows error ")+Marshal.GetLastWin32Error());
   finished=WaitForSingleObject(thread,10000)==0;if(!finished)throw new Exception(InstallerLanguage.T("运行库加载超时，未重复注入。","The patch runtime timed out. The loader did not retry."));
   process.Refresh();if(!process.Modules.Cast<ProcessModule>().Any(m=>String.Equals(m.FileName,path,StringComparison.OrdinalIgnoreCase)))throw new Exception(InstallerLanguage.T("系统未能加载震动运行库。", "Windows could not load the patch runtime."));
  }finally{if(thread!=IntPtr.Zero)CloseHandle(thread);if(buffer!=IntPtr.Zero&&(thread==IntPtr.Zero||finished))VirtualFreeEx(handle,buffer,UIntPtr.Zero,0x8000);CloseHandle(handle);}
 }
 [DllImport("kernel32.dll",SetLastError=true)]static extern IntPtr OpenProcess(uint access,bool inherit,int pid);
 [DllImport("kernel32.dll",SetLastError=true)]static extern IntPtr VirtualAllocEx(IntPtr process,IntPtr at,UIntPtr size,uint type,uint protect);
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool WriteProcessMemory(IntPtr process,IntPtr at,byte[] data,UIntPtr size,out UIntPtr written);
 [DllImport("kernel32.dll",SetLastError=true)]static extern IntPtr CreateRemoteThread(IntPtr process,IntPtr attributes,UIntPtr stack,IntPtr entry,IntPtr parameter,uint flags,IntPtr threadId);
 [DllImport("kernel32.dll")]static extern uint WaitForSingleObject(IntPtr handle,uint milliseconds);
 [DllImport("kernel32.dll")]static extern bool VirtualFreeEx(IntPtr process,IntPtr at,UIntPtr size,uint type);
 [DllImport("kernel32.dll")]static extern bool CloseHandle(IntPtr handle);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]static extern IntPtr GetModuleHandle(string name);
 [DllImport("kernel32.dll",CharSet=CharSet.Ansi)]static extern IntPtr GetProcAddress(IntPtr module,string name);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]static extern bool GetModuleHandleEx(uint flags,IntPtr address,out IntPtr module);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]static extern uint GetModuleFileName(IntPtr module,StringBuilder path,int size);
}
}
