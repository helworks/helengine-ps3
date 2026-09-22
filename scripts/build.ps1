<#
.SYNOPSIS
    Builds the native Helengine PS3 executable inside the locked SDK image.

.DESCRIPTION
    Creates a fresh repository-local build directory named by BuildId, runs the verified
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
    [ValidateSet('Clear','Cube','Core')][string]$Mode = 'Cube',
    [switch]$FixedPose,
    [ValidateRange(0,3600)][int]$RunSeconds = 0,
    [ValidateSet('None','AfterVideo','AfterRenderer')]
    [string]$FailurePoint = 'None',
    [string]$OutputRoot = '',
    [ValidateRange(0.0, 6.2832)][double]$FixedPoseRadians = 0.6,
    [string]$GeneratedCoreRoot = '',
    [string]$ContentRoot = '',
    [ValidateSet('None','AfterCoreInitialize','DuringCoreDraw')]
    [string]$CoreFailurePoint = 'None'
)
# GeneratedCoreRoot is the root of a generated Core C++ tree produced by the platform
# build. It is required by Core mode and refused by every other mode, so a build can never
# silently ignore a generated tree the caller expected to be compiled in.
# FixedPoseRadians is a documented diagnostic: it only applies with -FixedPose and lets
# known orientations (0, pi/2, pi) be built for depth/culling checks. 0.6 is the
# documented fixed pose of the final build interface.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $repoRoot 'scripts\ps3-paths.ps1')
$stateRoot = Resolve-Ps3StateRoot -RepoRoot $repoRoot
$OutputRoot = Resolve-Ps3OutputRoot -RepoRoot $repoRoot -RequestedRoot $OutputRoot
if ($Mode -eq 'Core' -and [string]::IsNullOrWhiteSpace($GeneratedCoreRoot)) {
    $GeneratedCoreRoot = Join-Path $stateRoot 'generated-core'
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
$cgMountSource = Join-Path $stateRoot 'dependencies\cg\usr'
$cgLibraryPath = Join-Path $cgMountSource ($shaderToolkit.LibraryPath -replace '/', '\')
if (-not (Test-Path -LiteralPath $cgLibraryPath -PathType Leaf)) {
    throw "Cg toolkit library is missing: $cgLibraryPath. Run scripts/prepare_toolchain.ps1."
}
# The setup script verifies the extracted library before recording the prepared
# toolchain. Builds only require the verified library to remain present; hashing
# it again here would make the script-contract fakes depend on proprietary bytes.
$lockHash = Get-FileSha256 $lockPath

# --- Generated Core root ----------------------------------------------------------------
# Validated before anything is created, so an unusable request never leaves a half-built
# directory behind and never reaches a container.
$generatedCoreUnityName = ''
if ($Mode -eq 'Core') {
    if ([string]::IsNullOrWhiteSpace($GeneratedCoreRoot)) {
        throw 'Core mode requires -GeneratedCoreRoot naming a generated Core C++ tree.'
    }
    $GeneratedCoreRoot = [IO.Path]::GetFullPath($GeneratedCoreRoot)
    if (-not (Test-Path -LiteralPath $GeneratedCoreRoot -PathType Container)) {
        throw "Generated Core root does not exist: $GeneratedCoreRoot"
    }
    # The editor writes a merged unity translation unit; a single-project codegen run writes
    # the raw one. Either is acceptable, but one of them must be present or there is nothing
    # to compile and the failure would otherwise surface obscurely inside make.
    foreach ($candidate in @('helengine_core_unity.cpp', 'generated_unity.cpp')) {
        if (Test-Path -LiteralPath (Join-Path $GeneratedCoreRoot $candidate) -PathType Leaf) {
            $generatedCoreUnityName = $candidate
            break
        }
    }
    if ([string]::IsNullOrWhiteSpace($generatedCoreUnityName)) {
        throw "Generated Core root contains no unity translation unit (helengine_core_unity.cpp or generated_unity.cpp): $GeneratedCoreRoot"
    }
} elseif (-not [string]::IsNullOrWhiteSpace($GeneratedCoreRoot)) {
    throw "-GeneratedCoreRoot is only valid in Core mode; this build requested mode '$Mode'."
}

# A Core failure point outside Core mode would compile away silently, so the request is
# refused rather than ignored.
# Content is only meaningful where a generated Core can load it, so a content root outside
# Core mode is refused rather than copied and never read.
if ($Mode -ne 'Core' -and -not [string]::IsNullOrWhiteSpace($ContentRoot)) {
    throw "-ContentRoot is only valid in Core mode; this build requested mode '$Mode'."
}
if ($Mode -ne 'Core' -and $CoreFailurePoint -ne 'None') {
    throw "-CoreFailurePoint is only valid in Core mode; this build requested mode '$Mode'."
}

# --- Explicit compile definitions -----------------------------------------------------
$defines = [Collections.Generic.List[string]]::new()
if ($Mode -eq 'Clear') { $defines.Add('-DHELENGINE_PS3_CLEAR_ONLY=1') } else { $defines.Add('-DHELENGINE_PS3_CLEAR_ONLY=0') }
if ($Mode -eq 'Core') {
    $defines.Add('-DHELENGINE_PS3_HAS_GENERATED_CORE=1')
    $coreFailurePointValue = switch ($CoreFailurePoint) { 'None' { 0 } 'AfterCoreInitialize' { 1 } 'DuringCoreDraw' { 2 } }
    $defines.Add("-DHELENGINE_PS3_CORE_FAILURE_POINT=$coreFailurePointValue")
}
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
$makeArguments = "SRC_ROOT=/src BUILD_ROOT=/out MODE=$Mode HELENGINE_DEFINES='$definesText'"
if ($Mode -eq 'Core') {
    $makeArguments = "$makeArguments GENERATED_CORE_ROOT=/generated"
}
$innerCommand = "set -e; make -f /src/Makefile $makeArguments -j8 all"
$dockerArgs = @(
    'run', '--rm', '--network', 'none',
    '--mount', "type=bind,source=$repoRoot,target=/src,readonly",
    '--mount', "type=bind,source=$buildRoot,target=/out",
    '--mount', "type=bind,source=$cgMountSource,target=$($shaderToolkit.MountTarget),readonly"
)
if ($Mode -eq 'Core') {
    # Read-only: the generated tree is an input and is never edited after generation.
    $dockerArgs += @('--mount', "type=bind,source=$GeneratedCoreRoot,target=/generated,readonly")
}
$dockerArgs += @(
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
# --- Content installation ---------------------------------------------------------------
# Cooked content is installed under USRDIR/content next to the executable, which is the path
# Ps3ContentStreamSource resolves against at run time. The count is recorded so a build that
# staged nothing is visible in the manifest rather than only at run time.
$installedContentFileCount = 0
if (-not [string]::IsNullOrWhiteSpace($ContentRoot)) {
    $ContentRoot = [IO.Path]::GetFullPath($ContentRoot)
    if (-not (Test-Path -LiteralPath $ContentRoot -PathType Container)) {
        throw "Content root does not exist: $ContentRoot"
    }
    $contentDestination = Join-Path $buildRoot 'USRDIR\content'
    New-Item -ItemType Directory -Path $contentDestination -Force | Out-Null
    $contentPrefix = $ContentRoot.TrimEnd('\', '/') + '\'
    foreach ($contentFile in (Get-ChildItem -LiteralPath $ContentRoot -Recurse -File)) {
        $relative = $contentFile.FullName.Substring($contentPrefix.Length)
        $destinationPath = Join-Path $contentDestination $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force | Out-Null
        Copy-Item -LiteralPath $contentFile.FullName -Destination $destinationPath -Force
        $installedContentFileCount++
    }
    Write-Output "Installed $installedContentFileCount cooked content files under $contentDestination"
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

# --- Generated Core fingerprint ------------------------------------------------------------
# One hash over every generated source path and content, so an executable can be traced back
# to the exact tree it was compiled from. Computed after the build, because a tree that
# changed mid-build would otherwise be attributed to the artifact incorrectly.
$generatedCoreFingerprint = ''
$generatedCoreFileCount = 0
if ($Mode -eq 'Core') {
    $generatedFiles = @(Get-ChildItem -LiteralPath $GeneratedCoreRoot -File -Recurse | Sort-Object FullName)
    $generatedCoreFileCount = $generatedFiles.Count
    # Relative paths are computed by trimming the root prefix rather than with
    # [IO.Path]::GetRelativePath, which does not exist on the .NET Framework that Windows
    # PowerShell 5.1 runs on.
    $generatedRootPrefix = $GeneratedCoreRoot.TrimEnd('\', '/') + '\'
    $generatedLines = @(
        foreach ($generatedFile in $generatedFiles) {
            $fullPath = $generatedFile.FullName
            if (-not $fullPath.StartsWith($generatedRootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Generated file lies outside the generated Core root: $fullPath"
            }
            $relativePath = $fullPath.Substring($generatedRootPrefix.Length).Replace('\', '/')
            "$relativePath=$((Get-FileHash -LiteralPath $generatedFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant())"
        }
    )
    # Hashed through a created instance rather than SHA256::HashData, which the .NET
    # Framework behind Windows PowerShell 5.1 does not provide.
    $generatedFingerprintBytes = [Text.Encoding]::UTF8.GetBytes($generatedLines -join "`n")
    $generatedSha = [Security.Cryptography.SHA256]::Create()
    try {
        $generatedHashBytes = $generatedSha.ComputeHash($generatedFingerprintBytes)
    }
    finally {
        $generatedSha.Dispose()
    }
    $generatedCoreFingerprint = ([BitConverter]::ToString($generatedHashBytes)).Replace('-', '').ToLowerInvariant()
    Set-Content -LiteralPath (Join-Path $buildRoot 'generated-core-hashes.txt') -Value $generatedLines -Encoding UTF8
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
    CoreFailurePoint = $CoreFailurePoint
    ContentRoot = $ContentRoot
    InstalledContentFileCount = $installedContentFileCount
    FixedPoseRadians = $FixedPoseRadians
    ToolchainLockSha256 = $lockHash
    GeneratedCoreRoot = $GeneratedCoreRoot
    GeneratedCoreUnity = $generatedCoreUnityName
    GeneratedCoreSha256 = $generatedCoreFingerprint
    GeneratedCoreFileCount = $generatedCoreFileCount
    ImageId = $imageId
    LaunchArtifact = $launchArtifactName
    Artifacts = $artifactRecords
}
$manifestPath = Join-Path $buildRoot 'build-manifest.json'
$manifestTemporaryPath = Join-Path $buildRoot 'build-manifest.json.tmp'
[IO.File]::WriteAllText($manifestTemporaryPath, (($manifest | ConvertTo-Json -Depth 6) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $manifestTemporaryPath -Destination $manifestPath
Write-Output "Build $BuildId complete: $elfPath (launch artifact $launchArtifactName). Manifest: $manifestPath"
