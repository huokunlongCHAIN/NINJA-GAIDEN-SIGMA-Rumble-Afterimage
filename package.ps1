$ErrorActionPreference='Stop'
$base=$PSScriptRoot
$release=Join-Path $base 'release'
$installer=Join-Path $release 'NGS1RumblePatch.exe'
$version=[Diagnostics.FileVersionInfo]::GetVersionInfo($installer).FileVersion -replace '\.0$',''
if($version -ne '0.6.27'){throw 'Build the native-plugin 0.6.27 installer before packaging.'}
$assembly=[Reflection.Assembly]::LoadFile($installer)
if($assembly.GetManifestResourceNames().Length -ne 0){throw 'Transparent installer must not embed payloads.'}
$sourceBase='3ff3e5542f2279726291fb2e2c7138c3d02424a7'
$sourcePaths=@('.gitignore','.github','docs','build.ps1','package.ps1','release-core-history.json','src','native','tools','assets','tests','release','README.md',':(exclude)release/SOURCE-CHANGES.patch')
$patch=& git -C $base diff --binary $sourceBase -- @sourcePaths
if($LASTEXITCODE -ne 0 -or !$patch){throw 'Could not generate auditable source changes.'}
[IO.File]::WriteAllText((Join-Path $release 'SOURCE-CHANGES.patch'),($patch -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
$rootNames=@('NGS1RumblePatch.exe','使用说明.txt','README.en.txt','许可证.txt','SECURITY-REVIEW.txt','BUILD-AND-SIGNING.txt','TEST-RESULTS.txt','SOURCE-CHANGES.patch')
$componentNames=@('dinput8.dll','NGS1CrashTrace.exe','NGS1Rumble.ini','NGS1RumbleRules.ini','NGS1RumbleWaves.ini')
$relative=@($rootNames)+@($componentNames|ForEach-Object {'Payload/'+$_})
$stage=Join-Path $base ('dist\NGS1_NativePlugin_'+$version+'_Test')
if(Test-Path -LiteralPath $stage){
 if((Get-Item -LiteralPath $stage).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Stage must not be a link.'}
 foreach($entry in Get-ChildItem -LiteralPath $stage -Force -Recurse){
  if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Stage must not contain links.'}
  $name=$entry.FullName.Substring($stage.Length+1).Replace('\','/')
  if($entry.PSIsContainer){if($name -ne 'Payload'){throw ('Unexpected stage folder: '+$name)}}
  elseif($name -notin ($relative+@('PACKAGE-MANIFEST.json','SHA256SUMS.txt'))){throw ('Unexpected stage file: '+$name)}
 }
}
New-Item -ItemType Directory -Path $stage,(Join-Path $stage 'Payload') -Force | Out-Null
foreach($name in $relative){Copy-Item -LiteralPath (Join-Path $release $name) -Destination (Join-Path $stage $name) -Force}
$records=@(foreach($name in $relative){
 $path=Join-Path $stage $name
 $signature=if($name -match '\.(exe|dll)$'){(Get-AuthenticodeSignature -LiteralPath $path).Status.ToString()}else{$null}
 if($signature -and $signature -notin @('Valid','NotSigned')){throw ('Invalid signature: '+$name+' '+$signature)}
 [ordered]@{file=$name;bytes=(Get-Item -LiteralPath $path).Length;sha256=(Get-FileHash -LiteralPath $path).Hash;authenticode=$signature}
})
$info=[ordered]@{
 product='NGS1Enhancements';version=$version;distribution='native-plugin-test';core='0.7.6';afterimageBase='0.5.9'
 malwareClearance='Not claimed; independent review required';publisherSigning=(Get-AuthenticodeSignature -LiteralPath $installer).Status.ToString()
 sourceBase=$sourceBase;sourceChanges='SOURCE-CHANGES.patch';files=$records
 embedsExecutableComponents=$false;usesTemporaryInstaller=$false;startsCleanupScripts=$false
 modifiesGameExe=$false;modifiesGameArchive=$false;startsGameAfterInstall=$false;createsShortcuts=$false
 retainsTargetedDllInjection=$false;originalBuildsUsePerUserAutoloader=$false;createsStartupRegistration=$false;optionalCrashDebugger=$true
 startsCoreInsideDllMain=$false;legacyAutoloaderCleanup=$true
 installerRecognizedVersions=@('1.0.0.2','1.0.0.4');runtimeSmokeTestExeSHA256=@('A40242446574F4E56C5C560FBB09875C2DBF71D2C084195A0ED9B93BC405A338','5D52F7BA7AD725F99613E24C4F571526FB603C7CBFB4346AFF53F40EA0470472');supportsUnknownBuilds=$false
}
$info|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $stage 'PACKAGE-MANIFEST.json') -Encoding utf8
$sums=@(foreach($name in ($relative+@('PACKAGE-MANIFEST.json'))){(Get-FileHash -LiteralPath (Join-Path $stage $name)).Hash.ToLowerInvariant()+'  '+$name})
[IO.File]::WriteAllLines((Join-Path $stage 'SHA256SUMS.txt'),$sums,[Text.UTF8Encoding]::new($false))
New-Item -ItemType Directory -Path (Join-Path $base 'evidence') -Force|Out-Null
$info|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $base 'evidence\release-info.json') -Encoding utf8
$zip=Join-Path $base ('NGS1_NativePlugin_'+$version+'_Test_Install_Restore.zip')
Compress-Archive -LiteralPath $stage -DestinationPath $zip -Force
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::OpenRead($zip)
try{
 $files=@($archive.Entries|Where-Object {$_.Name})
 if($files.Count -ne $relative.Count+2){throw 'Unexpected ZIP entry count.'}
 foreach($entry in $files){
  $name=$entry.FullName.Substring($entry.FullName.IndexOf('/')+1)
  if($name -notin ($relative+@('PACKAGE-MANIFEST.json','SHA256SUMS.txt'))){throw ('Unexpected ZIP file: '+$name)}
  $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
  try{$hash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
  if($hash -ne (Get-FileHash -LiteralPath (Join-Path $stage $name)).Hash){throw ('ZIP checksum mismatch: '+$name)}
 }
}finally{$archive.Dispose()}
Write-Output 'Verified native-plugin test package, five external components, source patch and checksums. No malware-clearance claim.'
Get-FileHash -LiteralPath $zip
