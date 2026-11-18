param([string]$ClangDirectory,[string]$IconHeader,[string]$SigningCertificateThumbprint,[string]$SignToolPath,[string]$TimestampUrl='http://timestamp.digicert.com',[switch]$RunRenderTests)
$ErrorActionPreference='Stop'
$base=$PSScriptRoot
$build=Join-Path $base 'build'
$release=Join-Path $base 'release'
New-Item -ItemType Directory -Path $build,$release -Force|Out-Null
if(!$ClangDirectory){$ClangDirectory=Split-Path (Get-Command clang++.exe -ErrorAction Stop).Source}
$compiler=Join-Path $ClangDirectory 'clang++.exe'
$cCompiler=Join-Path $ClangDirectory 'clang.exe'
if((& $compiler '-dumpmachine') -notmatch '^x86_64.*(?:windows-gnu|mingw)'){throw 'Use an x64 Windows LLVM/MinGW toolchain.'}
if($SigningCertificateThumbprint){
 if($SigningCertificateThumbprint -notmatch '^[0-9a-fA-F]{40}$'){throw 'Invalid certificate thumbprint.'}
 if(!$SignToolPath){$SignToolPath=(Get-Command signtool.exe -ErrorAction Stop).Source}
}
function Sign-PatchBinary([string]$Path){
 if(!$SigningCertificateThumbprint){return}
 & $SignToolPath sign /sha1 $SigningCertificateThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $Path
 if($LASTEXITCODE -ne 0){throw ('Signing failed: '+$Path)}
 & $SignToolPath verify /pa /tw $Path
 if($LASTEXITCODE -ne 0){throw ('Signature verification failed: '+$Path)}
}
$vendor=Join-Path $base 'vendor\minhook'
$minhook=Join-Path $vendor 'include'
$objects=@('buffer.c','hook.c','trampoline.c','hde\hde64.c')|ForEach-Object {
 $object=Join-Path $build ([IO.Path]::GetFileNameWithoutExtension($_)+'.o')
 & $cCompiler '-O2' '-c' '-I' $minhook (Join-Path $vendor ('src\'+$_)) '-o' $object
 if($LASTEXITCODE -ne 0){throw ('MinHook build failed: '+$_)}
 $object
}
$shaderCompiler=Join-Path $build 'compile_afterimage_shaders.exe'
& $compiler '-std=c++17' '-O2' '-static' (Join-Path $base 'tools\compile_afterimage_shaders.cpp') '-ld3dcompiler' '-o' $shaderCompiler
if($LASTEXITCODE -ne 0){throw 'Shader compiler build failed.'}
& $shaderCompiler (Join-Path $build 'afterimage_shaders.generated.h')
if($LASTEXITCODE -ne 0){throw 'Shader compilation failed.'}
$icons=@()
if($IconHeader){Copy-Item -LiteralPath $IconHeader -Destination (Join-Path $build 'icon_data.generated.h') -Force;$icons=@('-DNGS1_LOCAL_ICONS','-I',$build)}
& $compiler '-std=c++17' '-O2' '-s' '-shared' '-static' '-Wall' '-Wextra' '-Wno-unused-parameter' '-I' $minhook '-I' $build @icons (Join-Path $base 'native\rumble.cpp') (Join-Path $base 'native\afterimage.cpp') (Join-Path $base 'native\dinput8.def') @objects '-lbcrypt' '-luser32' '-lhid' '-lsetupapi' '-ldxguid' '-o' (Join-Path $base 'payload\dinput8.dll')
if($LASTEXITCODE -ne 0){throw 'Native plugin build failed.'}
Sign-PatchBinary (Join-Path $base 'payload\dinput8.dll')
if($RunRenderTests){
 & $compiler '-std=c++17' '-O2' '-static' '-I' $minhook '-I' $build (Join-Path $base 'native\test_overlay.cpp') @objects '-ld3dcompiler' '-ld3d11' '-lbcrypt' '-luser32' '-ldxguid' '-o' (Join-Path $build 'test_overlay.exe')
 if($LASTEXITCODE -ne 0){throw 'Render test build failed.'}
 & (Join-Path $build 'test_overlay.exe')
 if($LASTEXITCODE -ne 0){throw 'Render test failed.'}
 foreach($test in @('charge_effect','controller_glyphs')){
  & $compiler '-std=c++17' '-O2' '-static' (Join-Path $base ('native\test_'+$test+'.cpp')) '-o' (Join-Path $build ('test_'+$test+'.exe'))
  if($LASTEXITCODE -ne 0){throw ($test+' test build failed.')}
  & (Join-Path $build ('test_'+$test+'.exe'))
  if($LASTEXITCODE -ne 0){throw ($test+' test failed.')}
 }
}
$traceHelper=Join-Path $build 'NGS1CrashTrace.exe'
& $compiler '-std=c++17' '-O2' '-s' '-static' '-municode' (Join-Path $base 'tools\crash_trace.cpp') '-o' $traceHelper
if($LASTEXITCODE -ne 0){throw 'Crash helper build failed.'}
Sign-PatchBinary $traceHelper
$coreNames=@('dinput8.dll','NGS1Rumble.ini','NGS1RumbleRules.ini','NGS1RumbleWaves.ini')
$hashRows=@(foreach($name in $coreNames){' {"'+$name+'","'+(Get-FileHash -LiteralPath (Join-Path $base ('payload\'+$name))).Hash+'"},'})
$hashRows+=' {"NGS1CrashTrace.exe","'+(Get-FileHash -LiteralPath $traceHelper).Hash+'"},'
$hashSource='using System.Collections.Generic; namespace NgsRumblePatch { public static class PayloadHashes { public static readonly Dictionary<string,string> Values=new Dictionary<string,string>{'+($hashRows -join "`n")+'}; } }'
[IO.File]::WriteAllText((Join-Path $build 'PayloadHashes.cs'),$hashSource,[Text.UTF8Encoding]::new($false))
$history=@(Get-Content -LiteralPath (Join-Path $base 'release-core-history.json') -Raw|ConvertFrom-Json)
foreach($hash in $history){if($hash -notmatch '^[0-9A-Fa-f]{64}$'){throw 'Invalid released core history.'}}
$known='namespace NgsRumblePatch { public static class ReleasedCoreHashes { public static readonly string[] Values={'+(($history|ForEach-Object {'"'+$_+'"'}) -join ',')+'}; } }'
[IO.File]::WriteAllText((Join-Path $build 'ReleasedCoreHashes.cs'),$known,[Text.UTF8Encoding]::new($false))
foreach($parent in @($build,$release)){
 $components=Join-Path $parent 'Payload'
 New-Item -ItemType Directory -Path $components -Force|Out-Null
 if(Test-Path -LiteralPath (Join-Path $components 'NGS1RumbleAuto.exe')){throw 'Unexpected old autoloader in native-plugin output folder.'}
 foreach($name in $coreNames){Copy-Item -LiteralPath (Join-Path $base ('payload\'+$name)) -Destination (Join-Path $components $name) -Force}
 Copy-Item -LiteralPath $traceHelper -Destination (Join-Path $components 'NGS1CrashTrace.exe') -Force
}
$version=[regex]::Match([IO.File]::ReadAllText((Join-Path $base 'src\Payload.cs')),'Version="([^"]+)"').Groups[1].Value
$metadata='using System.Reflection; [assembly: AssemblyTitle("Ninja Gaiden Sigma Enhancements - Native Plugin Test")] [assembly: AssemblyProduct("Ninja Gaiden Sigma Enhancements")] [assembly: AssemblyCompany("huokunlongCHAIN")] [assembly: AssemblyVersion("'+$version+'.0")] [assembly: AssemblyFileVersion("'+$version+'.0")]'
[IO.File]::WriteAllText((Join-Path $build 'InstallerAssemblyInfo.cs'),$metadata,[Text.UTF8Encoding]::new($false))
$csc='C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$common=@('/nologo','/platform:x64','/optimize+','/reference:System.Core.dll','/reference:System.Web.Extensions.dll')
$engine=@('Payload.cs','PatchEngine.cs','GameCompatibility.cs','Launcher.cs','CleanUninstall.cs','RuntimeBridge.cs','AutoLifecycle.cs','Features.cs','InstallerLanguage.cs')|ForEach-Object {Join-Path $base ('src\'+$_)}
$generated=@((Join-Path $build 'PayloadHashes.cs'),(Join-Path $build 'ReleasedCoreHashes.cs'))
$installer=Join-Path $release 'NGS1RumblePatch.exe'
& $csc @common '/target:winexe' ('/win32manifest:'+(Join-Path $base 'assets\installer.manifest')) '/reference:System.Windows.Forms.dll' '/reference:System.Drawing.dll' '/reference:System.IO.Compression.dll' '/reference:System.Xml.Linq.dll' '/reference:System.Management.dll' ('/out:'+$installer) @engine (Join-Path $base 'src\InstallerSession.cs') (Join-Path $base 'src\Program.cs') (Join-Path $base 'src\RoundedControls.cs') (Join-Path $base 'src\PatchDiagnostics.cs') (Join-Path $base 'src\HardwareInfo.cs') (Join-Path $build 'InstallerAssemblyInfo.cs') @generated
if($LASTEXITCODE -ne 0){throw 'Native-plugin UI build failed.'}
Sign-PatchBinary $installer
foreach($name in @('TransparentPackageTests','PluginInstallerTests')){
 & $csc @common '/target:exe' ('/out:'+(Join-Path $build ($name+'.exe'))) @engine (Join-Path $base ('tests\'+$name+'.cs')) @generated
 if($LASTEXITCODE -ne 0){throw ($name+' build failed.')}
}
& $compiler '-std=c++17' '-O2' '-static' '-municode' (Join-Path $base 'tools\test_plugin_startup.cpp') '-ldxguid' '-lole32' '-o' (Join-Path $build 'test_plugin_startup.exe')
if($LASTEXITCODE -ne 0){throw 'Plugin startup probe build failed.'}
Get-FileHash -LiteralPath $installer
