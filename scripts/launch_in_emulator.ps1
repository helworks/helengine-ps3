<#
.SYNOPSIS
    Launches a verified Helengine PS3 build artifact in RPCS3.

.DESCRIPTION
    Validates, before starting anything, that the artifact lives inside a build directory
    under the selected PS3 output root, that the directory's build-manifest.json names it
    as the launch artifact with a matching BuildId, that its SHA-256 matches the manifest
    record and that the manifest was produced against the current toolchain lock. A
    missing emulator, missing manifest or any mismatch is an error. Background launches
    use a hidden window; -Visible is explicitly interactive. Emulator stdout/stderr are
    captured per run under <build>/emulator-runs/<timestamp>/. The emulator's own
    configuration is never modified.

    RPCS3 command line (verified against RPCS3 0.0.42-20013-2d103407, see docs/Validation.md):
        rpcs3.exe [--headless | --no-gui] --config <run config.yml> <artifact>
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArtifactPath,
    [string]$RpcS3Path = $env:HELENGINE_PS3_RPCS3_PATH,
    [switch]$Visible,
    [string]$OutputRoot = '',
    [string]$RuntimeRoot = '',
    [switch]$SynchronousShaders
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $repoRoot 'scripts\ps3-paths.ps1')
$OutputRoot = Resolve-Ps3OutputRoot -RepoRoot $repoRoot -RequestedRoot $OutputRoot
$ArtifactPath = [IO.Path]::GetFullPath($ArtifactPath)
$lockPath = Join-Path $repoRoot 'toolchain-lock.json'

function Get-FileSha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-Rpcs3RunConfigLines {
    param([switch]$SynchronousShaders)

    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add('Video:')
    $lines.Add('  Write Color Buffers: true')
    if ($SynchronousShaders) {
        $lines.Add('  Shader Mode: Legacy Recompiler (single-threaded)')
    }
    $lines.Add('Miscellaneous:')
    $lines.Add('  Exit RPCS3 when process finishes: true')
    $lines.Add('  Start games in fullscreen mode: false')
    $lines.Add('  Show shader compilation hint: false')
    $lines.Add('  Show PPU compilation hint: false')
    return $lines.ToArray()
}

# --- Artifact containment and manifest identity ---------------------------------------
$outputPrefix = $OutputRoot.TrimEnd('\', '/') + '\'
if (-not $ArtifactPath.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Artifact must live under the PS3 output root ${OutputRoot}: $ArtifactPath"
}
if (-not (Test-Path -LiteralPath $ArtifactPath -PathType Leaf)) {
    throw "Artifact does not exist; refusing to launch: $ArtifactPath"
}
$buildRoot = Split-Path -Parent $ArtifactPath
$buildId = Split-Path -Leaf $buildRoot
$manifestPath = Join-Path $buildRoot 'build-manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Build manifest is missing; refusing to launch: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.SchemaVersion -ne 1) {
    throw "Unsupported manifest SchemaVersion '$($manifest.SchemaVersion)' in $manifestPath"
}
if ($manifest.BuildId -ne $buildId) {
    throw "Manifest BuildId '$($manifest.BuildId)' does not match build directory '$buildId'."
}
$artifactName = Split-Path -Leaf $ArtifactPath
if ($manifest.LaunchArtifact -ne $artifactName) {
    throw "Manifest launch artifact is '$($manifest.LaunchArtifact)', not '$artifactName'."
}
$record = @($manifest.Artifacts | Where-Object { $_.Path -eq $artifactName })
if ($record.Count -ne 1) {
    throw "Manifest does not record exactly one hash for '$artifactName'."
}
$actualHash = Get-FileSha256 $ArtifactPath
if ($actualHash -ne ([string]$record[0].Sha256).ToLowerInvariant()) {
    throw "Artifact SHA-256 mismatch; refusing to launch. Expected $($record[0].Sha256), found $actualHash."
}
if ((Get-Item -LiteralPath $ArtifactPath).Length -eq 0) {
    throw "Artifact is empty; refusing to launch: $ArtifactPath"
}
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
    throw "Toolchain lock is missing: $lockPath"
}
$lockHash = Get-FileSha256 $lockPath
if ($lockHash -ne ([string]$manifest.ToolchainLockSha256).ToLowerInvariant()) {
    throw "Build was produced against a different toolchain lock (manifest $($manifest.ToolchainLockSha256), current $lockHash); rebuild before launching."
}

# --- Emulator ---------------------------------------------------------------------------
if ([string]::IsNullOrWhiteSpace($RpcS3Path)) {
    throw 'RPCS3 path is required: pass -RpcS3Path or set HELENGINE_PS3_RPCS3_PATH.'
}
$RpcS3Path = [IO.Path]::GetFullPath($RpcS3Path)
if (-not (Test-Path -LiteralPath $RpcS3Path -PathType Leaf)) {
    throw "RPCS3 executable does not exist: $RpcS3Path"
}

# --- Launch -----------------------------------------------------------------------------
if ([string]::IsNullOrWhiteSpace($RuntimeRoot)) {
    $runStamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
    $runRoot = Join-Path $buildRoot "emulator-runs\$runStamp"
    New-Item -ItemType Directory -Path $runRoot | Out-Null
} else {
    $runRoot = [IO.Path]::GetFullPath($RuntimeRoot)
    $runtimePrefix = [IO.Path]::GetFullPath((Join-Path $buildRoot 'emulator-runs')).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    if (-not $runRoot.StartsWith($runtimePrefix, [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $runRoot -PathType Container)) { throw 'RuntimeRoot must name an existing run below this verified build.' }
}
$stdoutPath = Join-Path $runRoot 'emulator-stdout.log'
$stderrPath = Join-Path $runRoot 'emulator-stderr.log'
# A per-run game configuration (RPCS3 --config) makes the emulator exit when the PS3
# process finishes, keeps the emulation window windowed, and mirrors rendered color
# buffers into guest memory so the host's diagnostic pixel probe can read them. It
# applies to this CLI boot only; the emulator's own config.yml is never touched.
$runConfigPath = Join-Path $runRoot 'rpcs3-run-config.yml'
Set-Content -LiteralPath $runConfigPath -Encoding Ascii -Value (Get-Rpcs3RunConfigLines -SynchronousShaders:$SynchronousShaders)
$modeArgument = if ($Visible) { '--no-gui' } else { '--headless' }
$argumentList = @($modeArgument, '--config', ('"' + $runConfigPath + '"'), ('"' + $ArtifactPath + '"'))
$windowStyle = if ($Visible) { 'Normal' } else { 'Hidden' }
Set-Content -LiteralPath (Join-Path $runRoot 'emulator-command.txt') -Encoding UTF8 -Value @(
    ('"' + $RpcS3Path + '" ' + ($argumentList -join ' ')),
    "BuildId=$buildId",
    "ArtifactSha256=$actualHash",
    "Visible=$([bool]$Visible)",
    "SynchronousShaders=$([bool]$SynchronousShaders)",
    "StartedUtc=$([DateTime]::UtcNow.ToString('o'))"
)
$processPath = [Environment]::GetEnvironmentVariable('Path', 'Process')
if ([string]::IsNullOrWhiteSpace($processPath)) {
    $processPath = [Environment]::GetEnvironmentVariable('PATH', 'Process')
}
# Windows PowerShell can inherit both Path and PATH from the host harness. Its
# Start-Process environment conversion treats those names as case-insensitive and
# fails before launching. Keep one canonical Path entry for this child process.
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
if (-not [string]::IsNullOrWhiteSpace($processPath)) {
    [Environment]::SetEnvironmentVariable('Path', $processPath, 'Process')
}
$process = Start-Process -FilePath $RpcS3Path -ArgumentList $argumentList -WorkingDirectory (Split-Path -Parent $RpcS3Path) -WindowStyle $windowStyle -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru -Wait
$exitCode = $process.ExitCode
Add-Content -LiteralPath (Join-Path $runRoot 'emulator-command.txt') -Encoding UTF8 -Value @("ExitCode=$exitCode", "FinishedUtc=$((Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss'))")
# RPCS3 (Windows, portable layout) writes its logs to <rpcs3 dir>\log\.
foreach ($emulatorLog in @('RPCS3.log', 'TTY.log')) {
    $sourceLog = Join-Path (Split-Path -Parent $RpcS3Path) "log\$emulatorLog"
    if (Test-Path -LiteralPath $sourceLog -PathType Leaf) {
        Copy-Item -LiteralPath $sourceLog -Destination (Join-Path $runRoot $emulatorLog)
    }
}
if ($exitCode -ne 0) {
    throw "RPCS3 exited with code $exitCode. See $runRoot"
}
Write-Output "RPCS3 completed for $buildId. Run directory: $runRoot"
