$ErrorActionPreference='Stop'
$base=$PSScriptRoot
$release=Join-Path $base 'release'
$installer=Join-Path $release 'NGS1RumblePatch.exe'
$version=[Diagnostics.FileVersionInfo]::GetVersionInfo($installer).FileVersion -replace '\.0$',''
if($version -notmatch '^\d+\.\d+\.\d+$'){throw 'Build the installer before packaging.'}
$signature=Get-AuthenticodeSignature -LiteralPath $installer
if($signature.Status -notin @('Valid','NotSigned')){throw ('Invalid installer signature: '+$signature.Status)}
$info=[ordered]@{product='NGS1RumblePatch';version=$version;core='0.7.5';installerLanguages=@('zh-CN','en');afterimageBase='0.5.9';supportedCoreVersions=@('1.0.0.2-original','1.0.0.2-localized','1.0.0.4-standard','1.0.0.4-aligned');unknownCoreBuildsAccepted=$false;afterimageRequiresVerifiedRenderer=$true;independentInstall=@('rumble','dualsense','afterimage');outfitPresets=5;afterimageMaterials=@('Classic','Sigma2');sigma2MaxBrightness=200;automaticControllerIcons=$true;preservesCustomParameters=$true;createsShortcuts=$false;autoStartAfterInstall=$false;separateLaunchButton=$true;directUpgradeSupported=$true;directExeLaunchSupported=$true;originalUsesPerUserLoginAutoloader=$true;modifiesGameExe=$false;modifiesGameArchive=$false;authenticode=$signature.Status.ToString();installerSHA256=(Get-FileHash -LiteralPath $installer).Hash;embeddedFiles=[ordered]@{}}
$info.sigma2MaxGlowStrength=200
foreach($name in @('dinput8.dll','NGS1Rumble.ini','NGS1RumbleRules.ini','NGS1RumbleWaves.ini','NGS1RumbleAuto.exe')){
 $info.embeddedFiles[$name]=(Get-FileHash -LiteralPath (Join-Path $base ('payload\'+$name))).Hash
}
New-Item -ItemType Directory -Path (Join-Path $base 'evidence') -Force | Out-Null
$info | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $base 'evidence\release-info.json') -Encoding utf8
$names=@('NGS1RumblePatch.exe','使用说明.txt','README.en.txt','许可证.txt')
$stage=Join-Path $base ('dist\NGS1_Integrated_'+$version)
New-Item -ItemType Directory -Path $stage -Force | Out-Null
foreach($entry in Get-ChildItem -LiteralPath $stage -Force){if($entry.PSIsContainer -or $entry.Name -notin $names){throw 'Unexpected existing stage entry'}}
foreach($name in $names){Copy-Item -LiteralPath (Join-Path $release $name) -Destination (Join-Path $stage $name) -Force}
$zip=Join-Path $base ('NGS1_Integrated_'+$version+'_Install_Restore.zip')
Compress-Archive -LiteralPath $stage -DestinationPath $zip -Force
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::OpenRead($zip)
try{
 $files=@($archive.Entries | Where-Object {$_.Name})
 if($files.Count -ne $names.Count){throw 'Unexpected file count in share package'}
 foreach($entry in $files){
  if($entry.Name -notin $names){throw ('Unexpected package file: '+$entry.FullName)}
  $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
  try{$hash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
  if($hash -ne (Get-FileHash -LiteralPath (Join-Path $release $entry.Name)).Hash){throw ('ZIP content mismatch: '+$entry.FullName)}
 }
 Write-Output 'Verified installer, Chinese and English instructions, and licenses.'
}finally{$archive.Dispose()}
Get-FileHash -LiteralPath $zip | ConvertTo-Json
