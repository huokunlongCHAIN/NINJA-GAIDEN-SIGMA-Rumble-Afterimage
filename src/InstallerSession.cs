using System;
using System.IO;
using System.Reflection;

namespace NgsRumblePatch {
// Run in place from the extracted package; no temporary executable or cleanup script.
static class InstallerSession {
 public static bool Initialize(ref string[] args){
  string directory=Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
  string gameRoot=Path.GetDirectoryName(directory);
  if((args.Length>0&&args[0]=="--session-parent")||
   (String.Equals(Path.GetFileName(directory),LaunchEngine.Folder,StringComparison.OrdinalIgnoreCase)&&
    !String.IsNullOrEmpty(gameRoot)&&File.Exists(Path.Combine(gameRoot,Payload.ExeName))))
   throw new InvalidOperationException(InstallerLanguage.T(
    "请将完整的新安装包解压到游戏运行库目录之外，再运行安装器进行更新或还原。",
    "Extract the complete new package outside the game's runtime folder, then run its installer to update or restore."));
  return false;
 }
}
}
