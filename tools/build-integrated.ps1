[CmdletBinding()]
param(
  [string]$Cli,
  [string]$DataDirectory='C:\Users\33920\AppData\Local\Arduino15',
  [string]$Sketch=(Join-Path $PSScriptRoot '..\firmware\robot\ChariotIntegrated'),
  [string]$OutputDirectory
)
$ErrorActionPreference='Stop'
if (-not $Cli) {
  $command=Get-Command arduino-cli -ErrorAction SilentlyContinue
  if ($command) { $Cli=$command.Source }
  elseif (Test-Path -LiteralPath 'D:\arduino\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe') {
    $Cli='D:\arduino\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
  } else { throw 'Arduino CLI not found. Pass -Cli and -DataDirectory.' }
}
if (-not (Test-Path -LiteralPath $DataDirectory)) { throw "Missing ESP32 data directory: $DataDirectory" }
$taskBuild=Join-Path ([IO.Path]::GetTempPath()) ('chariot_integrated_'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskBuild | Out-Null
$sourceCopy=Join-Path $taskBuild 'ChariotIntegrated'
New-Item -ItemType Directory -Path $sourceCopy | Out-Null
foreach ($name in @('ChariotIntegrated.ino','RobotConfig.h','DpadControl.h','DriveMath.h')) {
  Copy-Item -LiteralPath (Join-Path $Sketch $name) -Destination (Join-Path $sourceCopy $name)
}
$configFile=Join-Path $taskBuild 'arduino-cli.yaml'
$config=@"
directories:
  data: '$($DataDirectory.Replace('\','/'))'
  downloads: '$($taskBuild.Replace('\','/'))/downloads'
  user: '$($taskBuild.Replace('\','/'))/libraries'
build_cache:
  path: '$($taskBuild.Replace('\','/'))/cache'
"@
[IO.File]::WriteAllText($configFile,$config,[Text.UTF8Encoding]::new($false))
$buildDirectory=Join-Path $taskBuild 'build'
$result=& $Cli compile --config-file $configFile --fqbn esp32:esp32:esp32 --warnings default --build-path $buildDirectory $sourceCopy 2>&1
$compileExit=$LASTEXITCODE
$result | ForEach-Object { Write-Output $_ }
Write-Output "BUILD_DIRECTORY=$buildDirectory"
Write-Output "COMPILE_EXIT=$compileExit"
if ($OutputDirectory) {
  New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
  $log=(@($result | ForEach-Object { $_.ToString() })+"BUILD_DIRECTORY=$buildDirectory"+"COMPILE_EXIT=$compileExit") -join "`n"
  [IO.File]::WriteAllText((Join-Path $OutputDirectory 'compile.txt'),$log,[Text.UTF8Encoding]::new($false))
}
if ($compileExit -ne 0) { throw "ESP32 compilation failed ($compileExit). Source preserved." }
Write-Output 'Compile only. No upload or serial access was performed.'
