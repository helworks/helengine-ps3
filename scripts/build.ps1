<#
.SYNOPSIS
    Builds the native Helengine PS3 executable inside the locked SDK image.

.DESCRIPTION
    Creates a fresh external build directory named by BuildId, runs the verified
    helengine-ps3-sdk image (by image ID) with the source tree mounted read-only at /src,
    the build directory mounted read-write at /out and the NVIDIA Cg toolkit mounted
    read-only at /opt/cg, with networking disabled. Mode, pose, duration and failure
    settings become explicit compile definitions. After a zero exit the ELF header is
    checked with the pinned readelf, every required artifact must be nonempty, and only
    then is build-manifest.json written (to a temporary name first, then renamed).

    Artifacts: helengine_ps3.elf, helengine_ps3.self, helengine_ps3.map, build.log,
    build-manifest.json (plus shader intermediates under shaders/).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')]
    [string]$BuildId,
    [ValidateSet('Clear','Cube')][string]$Mode = 'Cube',
    [switch]$FixedPose,
    [ValidateRange(0,3600)][int]$RunSeconds = 0,
    [ValidateSet('None','AfterVideo','AfterRenderer')]
    [string]$FailurePoint = 'None',
    [string]$OutputRoot = 'C:\dev\helworks\builds\helengine-ps3',
    [ValidateRange(0.0, 6.2832)][double]$FixedPoseRadians = 0.6
)
# FixedPoseRadians is a documented diagnostic: it only applies with -FixedPose and lets
# known orientations (0, pi/2, pi) be built for depth/culling checks. 0.6 is the
# documented fixed pose of the final build interface.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$repoPrefix = $repoRoot.TrimEnd('\', '/') + '\'
if ($OutputRoot.Equals($repoRoot, [StringComparison]::OrdinalIgnoreCase) -or $OutputRoot.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must be outside the source worktree: $OutputRoot"
}
$buildRoot = Join-Path $OutputRoot $BuildId
$lockPath = Join-Path $repoRoot 'toolchain-lock.json'
$launchArtifactName = 'helengine_ps3.self'
$requiredArtifacts = @('helengine_ps3.elf', 'helengine_ps3.self', 'helengine_ps3.map', 'build.log', 'elf-header.txt')

function Get-FileSha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function ConvertTo-LineText {
    <#
        Converts one merged-stream item to text. Windows PowerShell 5.1 wraps native stderr
        lines in ErrorRecords whose string form is the exception type, so the message is used.
    #>
    param($Item)
    if ($Item -is [System.Management.Automation.ErrorRecord]) { return [string]$Item.Exception.Message }
    return [string]$Item
}

function Invoke-CheckedNative {
    <#
        Runs a native command with stdout and stderr merged. Windows PowerShell 5.1 turns
        redirected stderr lines into terminating errors under ErrorActionPreference=Stop,
        so the preference is relaxed only inside this function; the exit code decides.
    #>
    param([string]$FilePath, [string[]]$ArgumentList)
    $ErrorActionPreference = 'Continue'
    $output = @(& $FilePath @ArgumentList 2>&1 | ForEach-Object { ConvertTo-LineText $_ })
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Command failed ($exitCode): $FilePath $($ArgumentList -join ' ')`n$($output -join [Environment]::NewLine)"
    }
    return $output
}

function Invoke-NativeToLog {
    <#
        Runs a native command, streaming merged stdout/stderr to the console and appending
        it to a log file, and returns the exit code. See Invoke-CheckedNative for why the
        error preference is relaxed here.
    #>
    param([string]$FilePath, [string[]]$ArgumentList, [string]$LogPath)
    $ErrorActionPreference = 'Continue'
    $writer = [IO.StreamWriter]::new($LogPath, $true, [Text.UTF8Encoding]::new($false))
    try {
        & $FilePath @ArgumentList 2>&1 | ForEach-Object {
            $line = ConvertTo-LineText $_
            $writer.WriteLine($line)
            Write-Host $line
        }
        $exitCode = $LASTEXITCODE
    }
    finally {
        $writer.Dispose()
    }
    return $exitCode
}

# --- Preconditions: fresh directory, resolved lock, verified image, Cg mount ----------
if (Test-Path -LiteralPath $buildRoot) {
    throw "Refusing to reuse existing build directory: $buildRoot"
}
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
    throw "Toolchain lock does not exist: $lockPath"
}
$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
$resolved = $lock.ResolvedImage
if ($null -eq $resolved -or -not ($resolved.PSObject.Properties.Name -contains 'ImageId') -or [string]::IsNullOrWhiteSpace($resolved.ImageId)) {
    throw 'toolchain-lock.json has no ResolvedImage. Run scripts/prepare_toolchain.ps1 first.'
}
$imageId = [string]$resolved.ImageId
$inspectedId = ((Invoke-CheckedNative 'docker' @('image', 'inspect', $imageId, '--format', '{{.Id}}')) -join '').Trim()
if ($inspectedId -ne $imageId) {
    throw "Locked SDK image $imageId is not available locally (docker reported '$inspectedId'). Run scripts/prepare_toolchain.ps1."
}
$shaderToolkit = $lock.ShaderToolkit
$cgMountSource = [IO.Path]::GetFullPath($shaderToolkit.MountSource)
$cgLibraryPath = Join-Path $cgMountSource ($shaderToolkit.LibraryPath -replace '/', '\')
if (-not (Test-Path -LiteralPath $cgLibraryPath -PathType Leaf)) {
    throw "Cg toolkit library is missing: $cgLibraryPath. Run scripts/prepare_toolchain.ps1."
}
$libCgHash = Get-FileSha256 $cgLibraryPath
if ($libCgHash -ne $shaderToolkit.LibCgSha256) {
    throw "libCg.so hash mismatch. Expected $($shaderToolkit.LibCgSha256), found $libCgHash."
}
$lockHash = Get-FileSha256 $lockPath

# --- Explicit compile definitions -----------------------------------------------------
$defines = [Collections.Generic.List[string]]::new()
if ($Mode -eq 'Clear') { $defines.Add('-DHELENGINE_PS3_CLEAR_ONLY=1') } else { $defines.Add('-DHELENGINE_PS3_CLEAR_ONLY=0') }
if ($FixedPose) { $defines.Add('-DHELENGINE_PS3_FIXED_POSE=1') } else { $defines.Add('-DHELENGINE_PS3_FIXED_POSE=0') }
$defines.Add("-DHELENGINE_PS3_RUN_SECONDS=$RunSeconds")
$defines.Add('-DHELENGINE_PS3_FIXED_POSE_RADIANS=' + $FixedPoseRadians.ToString('0.0####', [Globalization.CultureInfo]::InvariantCulture) + 'f')
$failurePointValue = switch ($FailurePoint) { 'None' { 0 } 'AfterVideo' { 1 } 'AfterRenderer' { 2 } }
$defines.Add("-DHELENGINE_PS3_FAILURE_POINT=$failurePointValue")
$definesText = $defines -join ' '

# --- Source identity -------------------------------------------------------------------
$sourceCommit = ((Invoke-CheckedNative 'git' @('-C', $repoRoot, 'rev-parse', 'HEAD')) -join '').Trim()
$sourceStatus = Invoke-CheckedNative 'git' @('-C', $repoRoot, 'status', '--porcelain', '--untracked-files=all')
$sourceDirty = @($sourceStatus | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }).Count -gt 0

# --- Native build in the locked image --------------------------------------------------
New-Item -ItemType Directory -Path $buildRoot | Out-Null
$buildLogPath = Join-Path $buildRoot 'build.log'
$innerCommand = "set -e; make -f /src/Makefile SRC_ROOT=/src BUILD_ROOT=/out HELENGINE_DEFINES='$definesText' -j8 all"
$dockerArgs = @(
    'run', '--rm', '--network', 'none',
    '--mount', "type=bind,source=$repoRoot,target=/src,readonly",
    '--mount', "type=bind,source=$buildRoot,target=/out",
    '--mount', "type=bind,source=$cgMountSource,target=$($shaderToolkit.MountTarget),readonly",
    '--workdir', '/out',
    '--entrypoint', '/bin/bash',
    $imageId,
    '-c', $innerCommand
)
$commandText = "docker $($dockerArgs -join ' ')"
Set-Content -LiteralPath $buildLogPath -Value @("# $commandText", "# BuildId=$BuildId Mode=$Mode FixedPose=$([bool]$FixedPose) RunSeconds=$RunSeconds FailurePoint=$FailurePoint", "# Defines: $definesText") -Encoding UTF8
$dockerExitCode = Invoke-NativeToLog 'docker' $dockerArgs $buildLogPath
if ($dockerExitCode -ne 0) {
    throw "Native build failed with exit code $dockerExitCode. See $buildLogPath"
}

# --- Artifact verification --------------------------------------------------------------
$elfPath = Join-Path $buildRoot 'helengine_ps3.elf'
if (-not (Test-Path -LiteralPath $elfPath -PathType Leaf)) {
    throw "Successful build did not produce $elfPath"
}
$readelfArgs = @(
    'run', '--rm', '--network', 'none',
    '--mount', "type=bind,source=$buildRoot,target=/out,readonly",
    '--entrypoint', [string]$resolved.ReadelfPath,
    $imageId, '-h', '/out/helengine_ps3.elf'
)
$readelfOutput = Invoke-CheckedNative 'docker' $readelfArgs
$readelfText = $readelfOutput -join [Environment]::NewLine
if ($readelfText -notmatch 'PowerPC64' -or $readelfText -notmatch 'big endian') {
    throw "ELF header is not a PowerPC64 big-endian ELF:`n$readelfText"
}
Set-Content -LiteralPath (Join-Path $buildRoot 'elf-header.txt') -Value $readelfOutput -Encoding UTF8

$artifactRecords = @()
foreach ($artifactName in $requiredArtifacts) {
    $artifactPath = Join-Path $buildRoot $artifactName
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
        throw "Required artifact is missing after a successful build: $artifactPath"
    }
    if ((Get-Item -LiteralPath $artifactPath).Length -eq 0) {
        throw "Required artifact is empty after a successful build: $artifactPath"
    }
}
$shaderDirectory = Join-Path $buildRoot 'shaders'
$shaderArtifacts = @()
if (Test-Path -LiteralPath $shaderDirectory -PathType Container) {
    $shaderArtifacts = @(Get-ChildItem -LiteralPath $shaderDirectory -File | Where-Object { $_.Extension -in @('.vpo', '.fpo') } | ForEach-Object { 'shaders/' + $_.Name })
}
foreach ($artifactName in ($requiredArtifacts + $shaderArtifacts)) {
    if ($artifactName -eq 'build.log') { continue }
    $artifactPath = Join-Path $buildRoot ($artifactName -replace '/', '\')
    $artifactRecords += [PSCustomObject]@{
        Path = $artifactName
        Sha256 = Get-FileSha256 $artifactPath
        Bytes = (Get-Item -LiteralPath $artifactPath).Length
    }
}

# --- Success manifest: temporary name first, then rename ---------------------------------
$manifest = [PSCustomObject]@{
    SchemaVersion = 1
    BuildId = $BuildId
    SourceCommit = $sourceCommit
    SourceDirty = $sourceDirty
    Mode = $Mode
    FixedPose = [bool]$FixedPose
    RunSeconds = $RunSeconds
    FailurePoint = $FailurePoint
    FixedPoseRadians = $FixedPoseRadians
    ToolchainLockSha256 = $lockHash
    ImageId = $imageId
    LaunchArtifact = $launchArtifactName
    Artifacts = $artifactRecords
}
$manifestPath = Join-Path $buildRoot 'build-manifest.json'
$manifestTemporaryPath = Join-Path $buildRoot 'build-manifest.json.tmp'
[IO.File]::WriteAllText($manifestTemporaryPath, (($manifest | ConvertTo-Json -Depth 6) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $manifestTemporaryPath -Destination $manifestPath
Write-Output "Build $BuildId complete: $elfPath (launch artifact $launchArtifactName). Manifest: $manifestPath"
