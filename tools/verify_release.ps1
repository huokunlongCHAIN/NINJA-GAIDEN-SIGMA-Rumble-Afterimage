$ErrorActionPreference='Stop'
$base=Split-Path $PSScriptRoot
$installer=Join-Path $base 'release\NGS1RumblePatch.exe'
$version=[Diagnostics.FileVersionInfo]::GetVersionInfo($installer).ProductVersion
$files=@($installer,(Join-Path $base 'release\Payload\dinput8.dll'),(Join-Path $base 'release\Payload\NGS1CrashTrace.exe'))
foreach($file in $files){
 $metadata=[Diagnostics.FileVersionInfo]::GetVersionInfo($file)
 if($metadata.ProductName -ne 'Ninja Gaiden Sigma Enhancements' -or $metadata.ProductVersion -ne $version -or $metadata.FileVersion -ne $version){throw ('Product metadata mismatch: '+$file)}
 if(!$metadata.OriginalFilename){throw ('Missing original filename: '+$file)}
}
$result=Join-Path $base 'build\ci-verification.json'
$process=Start-Process -FilePath $installer -ArgumentList @('--verify-package',('"'+$result+'"')) -Wait -PassThru -WindowStyle Hidden
if($process.ExitCode -ne 0){throw 'Installer package preflight failed.'}
$report=Get-Content -LiteralPath $result -Raw|ConvertFrom-Json
if(!$report.ok -or @($report.components.PSObject.Properties).Count -ne 5){throw 'Expected five verified external components.'}
Write-Output ('PASS: matching product/version metadata and five verified external components; '+$version)
