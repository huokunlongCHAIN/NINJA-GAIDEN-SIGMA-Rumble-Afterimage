param(
 [string]$ClangDirectory,
 [string]$IconHeader,
 [switch]$RunRenderTests
)
$ErrorActionPreference='Stop'
$base=$PSScriptRoot
$build=Join-Path $base 'build'
New-Item -ItemType Directory -Force -Path $build,(Join-Path $base 'release') | Out-Null
if(!$ClangDirectory){$ClangDirectory=Split-Path (Get-Command clang++.exe -ErrorAction Stop).Source}
$compiler=Join-Path $ClangDirectory 'clang++.exe'
$cCompiler=Join-Path $ClangDirectory 'clang.exe'
if((& $compiler '-dumpmachine') -notmatch '^x86_64.*(?:windows-gnu|mingw)'){throw 'Use an x64 Windows MinGW LLVM toolchain.'}
$vendor=Join-Path $base 'vendor\minhook'
$minhook=Join-Path $vendor 'include'
$objects=@('buffer.c','hook.c','trampoline.c','hde\hde64.c') | ForEach-Object {
 $object=Join-Path $build ([IO.Path]::GetFileNameWithoutExtension($_)+'.o')
 & $cCompiler '-O2' '-c' '-I' $minhook (Join-Path $vendor ('src\'+$_)) '-o' $object
 if($LASTEXITCODE -ne 0){throw ('MinHook build failed for '+$_)}
 $object
}
$icons=@()
if($IconHeader){
 Copy-Item -LiteralPath $IconHeader -Destination (Join-Path $build 'icon_data.generated.h') -Force
 $icons=@('-DNGS1_LOCAL_ICONS','-I',$build)
}
& $compiler '-std=c++17' '-O2' '-s' '-shared' '-static' '-Wall' '-Wextra' '-Wno-unused-parameter' '-I' $minhook @icons (Join-Path $base 'native\rumble.cpp') (Join-Path $base 'native\afterimage.cpp') (Join-Path $base 'native\dinput8.def') @objects '-lbcrypt' '-luser32' '-lhid' '-lsetupapi' '-ldxguid' '-ld3dcompiler' '-o' (Join-Path $base 'payload\dinput8.dll')
if($LASTEXITCODE -ne 0){throw 'Integrated core build failed'}
if($RunRenderTests){
 & $compiler '-std=c++17' '-O2' '-static' '-I' $minhook (Join-Path $base 'native\test_overlay.cpp') @objects '-ld3dcompiler' '-ld3d11' '-lbcrypt' '-luser32' '-ldxguid' '-o' (Join-Path $build 'test_overlay.exe')
 if($LASTEXITCODE -ne 0){throw 'Render test build failed'}
 & (Join-Path $build 'test_overlay.exe')
 if($LASTEXITCODE -ne 0){throw 'Render tests failed'}
 & $compiler '-std=c++17' '-O2' '-static' (Join-Path $base 'native\test_charge_effect.cpp') '-o' (Join-Path $build 'test_charge_effect.exe')
 if($LASTEXITCODE -ne 0){throw 'Charge test build failed'}
 & (Join-Path $build 'test_charge_effect.exe')
 if($LASTEXITCODE -ne 0){throw 'Charge tests failed'}
 & $compiler '-std=c++17' '-O2' '-static' (Join-Path $base 'native\test_controller_glyphs.cpp') '-o' (Join-Path $build 'test_controller_glyphs.exe')
 if($LASTEXITCODE -ne 0){throw 'Controller glyph test build failed'}
 & (Join-Path $build 'test_controller_glyphs.exe')
 if($LASTEXITCODE -ne 0){throw 'Controller glyph tests failed'}
}
$historyPath=Join-Path $base 'release-core-history.json'
$history=@()
if(Test-Path -LiteralPath $historyPath){$history=@(Get-Content -LiteralPath $historyPath -Raw -Encoding utf8 | ConvertFrom-Json)}
$coreHash=(Get-FileHash -LiteralPath (Join-Path $base 'payload\dinput8.dll')).Hash
foreach($entry in $history){if($entry -notmatch '^[0-9A-Fa-f]{64}$'){throw 'Invalid released core hash history'}}
$knownSource='namespace NgsRumblePatch { public static class ReleasedCoreHashes { public static readonly string[] Values={'+(($history | ForEach-Object {'"'+$_+'"'}) -join ',')+'}; } }'
[IO.File]::WriteAllText((Join-Path $base 'build\ReleasedCoreHashes.cs'),$knownSource,[Text.UTF8Encoding]::new($false))
$csc='C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$common=@('/nologo','/platform:x64','/optimize+','/reference:System.Core.dll','/reference:System.Web.Extensions.dll')
$payloadSource=[IO.File]::ReadAllText((Join-Path $base 'src\Payload.cs'))
$version=[regex]::Match($payloadSource,'Version="([^"]+)"').Groups[1].Value
$autoSource='namespace NgsRumblePatch { public static class AutoLoaderConstants { public const string Version="'+$version+'"; public const string CoreHash="'+$coreHash+'"; } }'
[IO.File]::WriteAllText((Join-Path $base 'build\AutoLoaderConstants.cs'),$autoSource,[Text.UTF8Encoding]::new($false))
$inputRows=@('AutoLoader.cs','GameCompatibility.cs','RuntimeBridge.cs','InstallerLanguage.cs') | ForEach-Object {$_+':'+(Get-FileHash -LiteralPath (Join-Path $base ('src\'+$_))).Hash}
$inputRows+=@('Version:'+$version,'CoreHash:'+$coreHash,'Compiler:'+(Get-FileHash -LiteralPath $csc).Hash)
$sha=[Security.Cryptography.SHA256]::Create();try{$inputHash=[BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($inputRows -join "`n")))).Replace('-','')}finally{$sha.Dispose()}
$cachePath=Join-Path $base 'build\autoloader-build.json';$autoPath=Join-Path $base 'payload\NGS1RumbleAuto.exe';$reuse=$false
if((Test-Path -LiteralPath $cachePath) -and (Test-Path -LiteralPath $autoPath)){$cache=Get-Content -LiteralPath $cachePath -Raw -Encoding utf8|ConvertFrom-Json;$reuse=$cache.inputHash -eq $inputHash -and $cache.binaryHash -eq (Get-FileHash -LiteralPath $autoPath).Hash}
if(!$reuse){
 & $csc @common '/target:winexe' ('/out:'+$autoPath) (Join-Path $base 'src\AutoLoader.cs') (Join-Path $base 'src\GameCompatibility.cs') (Join-Path $base 'src\RuntimeBridge.cs') (Join-Path $base 'src\InstallerLanguage.cs') (Join-Path $base 'build\AutoLoaderConstants.cs')
 if($LASTEXITCODE -ne 0){throw 'Autoloader build failed'}
 @{inputHash=$inputHash;binaryHash=(Get-FileHash -LiteralPath $autoPath).Hash}|ConvertTo-Json|Set-Content -LiteralPath $cachePath -Encoding utf8
}
$hashRows=@()
foreach($name in @('dinput8.dll','NGS1Rumble.ini','NGS1RumbleRules.ini','NGS1RumbleWaves.ini','NGS1RumbleAuto.exe')){
 $hash=(Get-FileHash -LiteralPath (Join-Path $base ('payload\'+$name))).Hash
 $hashRows+=('  {"'+$name+'","'+$hash+'"},')
}
$hashSource="using System.Collections.Generic;`r`nnamespace NgsRumblePatch { public static class PayloadHashes { public static readonly Dictionary<string,string> Values=new Dictionary<string,string>{`r`n"+($hashRows -join "`r`n")+"`r`n}; } }"
[IO.File]::WriteAllText((Join-Path $base 'build\PayloadHashes.cs'),$hashSource,[Text.UTF8Encoding]::new($false))
$csc='C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$common=@('/nologo','/platform:x64','/optimize+','/reference:System.Core.dll','/reference:System.Web.Extensions.dll')
$resources=@()
foreach($name in @('dinput8.dll','NGS1Rumble.ini','NGS1RumbleRules.ini','NGS1RumbleWaves.ini','NGS1RumbleAuto.exe')){$resources+=('/resource:'+(Join-Path $base ('payload\'+$name))+',Payload.'+$name)}
$engineSources=@('Payload.cs','PatchEngine.cs','GameCompatibility.cs','Launcher.cs','CleanUninstall.cs','RuntimeBridge.cs','AutoLifecycle.cs','Features.cs','InstallerLanguage.cs') | ForEach-Object {Join-Path $base ('src\'+$_)}
& $csc @common '/target:winexe' '/reference:System.Windows.Forms.dll' '/reference:System.Drawing.dll' ('/out:'+(Join-Path $base 'release\NGS1RumblePatch.exe')) @engineSources (Join-Path $base 'src\InstallerSession.cs') (Join-Path $base 'src\Program.cs') (Join-Path $base 'build\PayloadHashes.cs') (Join-Path $base 'build\ReleasedCoreHashes.cs') @resources
if($LASTEXITCODE -ne 0){throw 'Installer build failed'}
if(Test-Path -LiteralPath (Join-Path $base 'tests\InstallerTests.cs')){
 & $csc @common '/target:exe' ('/out:'+(Join-Path $base 'build\InstallerTests.exe')) @engineSources (Join-Path $base 'tests\InstallerTests.cs') (Join-Path $base 'build\PayloadHashes.cs') (Join-Path $base 'build\ReleasedCoreHashes.cs') @resources
 if($LASTEXITCODE -ne 0){throw 'Installer tests build failed'}
}
Get-FileHash -LiteralPath (Join-Path $base 'release\NGS1RumblePatch.exe')
if(Test-Path -LiteralPath (Join-Path $base 'tests\AutoLifecycleTests.cs')){
 & $csc @common '/target:exe' ('/out:'+(Join-Path $base 'build\AutoLifecycleTests.exe')) @engineSources (Join-Path $base 'tests\AutoLifecycleTests.cs') (Join-Path $base 'build\PayloadHashes.cs') (Join-Path $base 'build\ReleasedCoreHashes.cs') @resources
 if($LASTEXITCODE -ne 0){throw 'Autoloader lifecycle tests build failed'}
}
& $csc @common '/target:exe' ('/out:'+(Join-Path $base 'build\IntegratedTests.exe')) @engineSources (Join-Path $base 'tests\IntegratedTests.cs') (Join-Path $base 'build\PayloadHashes.cs') (Join-Path $base 'build\ReleasedCoreHashes.cs') @resources
if($LASTEXITCODE -ne 0){throw 'Integrated installer tests build failed'}
