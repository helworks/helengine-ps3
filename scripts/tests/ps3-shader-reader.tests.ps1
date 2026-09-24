<#
.SYNOPSIS
    Builds and optionally runs the standalone PPU P3SP reader regression target.

.DESCRIPTION
    Compilation is restricted to the locked SDK Docker image. The checkout and
    producer fixture are read-only container mounts; every build, staged fixture,
    manifest and log is written below the standard native-reader build root.
    Runtime launch uses the existing verified RPCS3 launcher and is intentionally
    separate from compilation through -BuildOnly.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$FixtureRoot,
    [Parameter(Mandatory)][string]$OutputRoot,
    [string]$RpcS3Path = $env:HELENGINE_PS3_RPCS3_PATH,
    [switch]$BuildOnly,
    [string]$BuildId = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$lockedImageId = 'sha256:63a28f67dacb0a6d80c165645af126c7d6760e6a3b02e03b6e08aa5aeb96c40f'
$ps3BuildRoot = [IO.Path]::GetFullPath('C:\dev\helworks\builds\helengine-ps3')
$nativeReaderRoot = [IO.Path]::GetFullPath((Join-Path $ps3BuildRoot 'shader-integration-20260923\native-reader'))
$lockPath = Join-Path $repoRoot 'toolchain-lock.json'
$manifestName = 'build-manifest.json'

function Get-Sha256 {
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-ContainedPath {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Parent,
        [Parameter(Mandatory)][string]$Label,
        [switch]$AllowParent
    )
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $fullParent = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
    $prefix = $fullParent + [IO.Path]::DirectorySeparatorChar
    $isParent = [string]::Equals($fullPath, $fullParent, [StringComparison]::OrdinalIgnoreCase)
    if ((-not $AllowParent -and $isParent) -or (-not $isParent -and -not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase))) {
        throw "$Label must be $(if ($AllowParent) { 'at or below' } else { 'strictly below' }) $fullParent; found $fullPath"
    }
    return $fullPath
}

if (-not [IO.Path]::IsPathRooted($FixtureRoot) -or -not [IO.Path]::IsPathRooted($OutputRoot)) {
    throw 'FixtureRoot and OutputRoot must be fully qualified paths.'
}
$FixtureRoot = Assert-ContainedPath -Path $FixtureRoot -Parent $nativeReaderRoot -Label 'FixtureRoot'
$OutputRoot = Assert-ContainedPath -Path $OutputRoot -Parent $nativeReaderRoot -Label 'OutputRoot' -AllowParent
if (-not (Test-Path -LiteralPath $FixtureRoot -PathType Container)) {
    throw "FixtureRoot does not exist: $FixtureRoot"
}
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
    throw "Toolchain lock is missing: $lockPath"
}
$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($lock.ResolvedImage.ImageId -ne $lockedImageId) {
    throw "Toolchain lock image ID is not the task-pinned image: $($lock.ResolvedImage.ImageId)"
}
$lockHash = Get-Sha256 -Path $lockPath

$fixtureNames = @('fixture.ps3sp', 'vertex.bin', 'pixel.bin')
$fixtureHashes = [ordered]@{}
foreach ($fixtureName in $fixtureNames) {
    $fixturePath = Join-Path $FixtureRoot $fixtureName
    if (-not (Test-Path -LiteralPath $fixturePath -PathType Leaf)) {
        throw "Required producer fixture is missing: $fixturePath"
    }
    if ((Get-Item -LiteralPath $fixturePath).Length -eq 0) {
        throw "Required producer fixture is empty: $fixturePath"
    }
    $fixtureHashes[$fixtureName] = Get-Sha256 -Path $fixturePath
}

$resolvedImageId = (& docker image inspect --format '{{.Id}}' $lockedImageId 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $resolvedImageId -ne $lockedImageId) {
    throw "Docker image ID does not match toolchain.lock.json: $resolvedImageId"
}

if ([string]::IsNullOrWhiteSpace($BuildId)) {
    $stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
    $BuildId = "shader-reader-$stamp-$([guid]::NewGuid().ToString('N'))"
}
if ($BuildId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$' -or $BuildId -in @('.', '..')) {
    throw "BuildId must be a single safe directory name; found '$BuildId'."
}
$buildRoot = Join-Path $OutputRoot $BuildId
if (Test-Path -LiteralPath $buildRoot) {
    throw "Refusing to reuse an existing native reader build directory: $buildRoot"
}
New-Item -ItemType Directory -Path $buildRoot -ErrorAction Stop | Out-Null

$contentRoot = Join-Path $buildRoot 'USRDIR\content'
New-Item -ItemType Directory -Path $contentRoot -ErrorAction Stop | Out-Null
foreach ($fixtureName in $fixtureNames) {
    $sourcePath = Join-Path $FixtureRoot $fixtureName
    if ((Get-Sha256 -Path $sourcePath) -ne $fixtureHashes[$fixtureName]) {
        throw "Producer fixture changed during validation: $sourcePath"
    }
    $stagedPath = Join-Path $contentRoot $fixtureName
    Copy-Item -LiteralPath $sourcePath -Destination $stagedPath
    if ((Get-Sha256 -Path $stagedPath) -ne $fixtureHashes[$fixtureName]) {
        throw "Staged fixture hash differs from its immutable producer input: $fixtureName"
    }
}

$buildLogPath = Join-Path $buildRoot 'build.log'
$dockerArguments = @(
    'run', '--rm', '--network', 'none',
    '--mount', "type=bind,source=$repoRoot,target=/src,readonly",
    '--mount', "type=bind,source=$buildRoot,target=/out",
    '--mount', "type=bind,source=$FixtureRoot,target=/app_home/USRDIR/content,readonly",
    $lockedImageId,
    'make', '-f', '/src/tests/native/shader-reader.Makefile', 'SRC_ROOT=/src', 'BUILD_ROOT=/out'
)
& docker @dockerArguments *> $buildLogPath
$dockerExit = $LASTEXITCODE
$buildLog = Get-Content -LiteralPath $buildLogPath -Raw
if ($buildLog.Length -gt 6000) { $buildLog = $buildLog.Substring($buildLog.Length - 6000) }
Write-Output $buildLog
if ($dockerExit -ne 0) {
    throw "Locked-image PPU reader build failed with exit code $dockerExit. Full log: $buildLogPath"
}

foreach ($fixtureName in $fixtureNames) {
    $sourcePath = Join-Path $FixtureRoot $fixtureName
    if ((Get-Sha256 -Path $sourcePath) -ne $fixtureHashes[$fixtureName]) {
        throw "Producer fixture changed during native compilation: $sourcePath"
    }
}
if ((Get-Sha256 -Path $lockPath) -ne $lockHash) {
    throw 'toolchain-lock.json changed during native reader compilation.'
}

$launchName = 'ps3_shader_reader_tests.self'
$artifactNames = @('ps3_shader_reader_tests.elf', $launchName, 'ps3_shader_reader_tests.fake.self')
$artifactRecords = @()
foreach ($artifactName in $artifactNames) {
    $artifactPath = Join-Path $buildRoot $artifactName
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf) -or (Get-Item -LiteralPath $artifactPath).Length -eq 0) {
        throw "Successful PPU build omitted nonempty artifact $artifactName; see $buildLogPath"
    }
    $artifactRecords += [pscustomobject]@{
        Path = $artifactName
        Sha256 = Get-Sha256 -Path $artifactPath
    }
}

$manifest = [ordered]@{
    SchemaVersion = 1
    BuildId = $BuildId
    ToolchainLockSha256 = $lockHash
    ToolchainImageId = $lockedImageId
    TestKind = 'NativeCpuShaderArtifactReader'
    SourceHashes = $fixtureHashes
    LaunchArtifact = $launchName
    Artifacts = $artifactRecords
}
$manifestPath = Join-Path $buildRoot $manifestName
$manifestTemporaryPath = Join-Path $buildRoot ($manifestName + '.tmp')
$manifestJson = $manifest | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText($manifestTemporaryPath, $manifestJson, [Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $manifestTemporaryPath -Destination $manifestPath
Write-Output "BuildId=$BuildId"
Write-Output "BuildRoot=$buildRoot"
Write-Output "BuildManifest=$manifestPath"

if ($BuildOnly) { return }

if ([string]::IsNullOrWhiteSpace($RpcS3Path)) {
    throw 'RPCS3 path is required for runtime acceptance; pass -RpcS3Path or set HELENGINE_PS3_RPCS3_PATH.'
}
$RpcS3Path = [IO.Path]::GetFullPath($RpcS3Path)
if (-not (Test-Path -LiteralPath $RpcS3Path -PathType Leaf)) {
    throw "RPCS3 executable does not exist: $RpcS3Path"
}
$runningEmulators = @(Get-Process -Name rpcs3 -ErrorAction SilentlyContinue)
if ($runningEmulators.Count -ne 0) {
    $runningIds = ($runningEmulators | ForEach-Object { $_.Id }) -join ', '
    throw "RPCS3 is already running (PID $runningIds). Do not start a concurrent reader test; coordinate a launch slot with the parent."
}

$launcherPath = Join-Path $repoRoot 'scripts\launch_in_emulator.ps1'
& $launcherPath -ArtifactPath (Join-Path $buildRoot $launchName) -OutputRoot $OutputRoot -RpcS3Path $RpcS3Path
$runRoots = @(Get-ChildItem -LiteralPath (Join-Path $buildRoot 'emulator-runs') -Directory | Sort-Object LastWriteTimeUtc -Descending)
if ($runRoots.Count -eq 0) { throw 'Existing launcher did not create an emulator run directory.' }
$ttyLogPath = Join-Path $runRoots[0].FullName 'TTY.log'
if (-not (Test-Path -LiteralPath $ttyLogPath -PathType Leaf)) {
    throw "Existing launcher did not capture this reader run's TTY.log: $ttyLogPath"
}
$ttyText = Get-Content -LiteralPath $ttyLogPath -Raw
$summaryMatches = [regex]::Matches($ttyText, '(?m)^PS3_SHADER_READER_TESTS passed=(\d+) failed=0\s*$')
if ($summaryMatches.Count -ne 1) { throw "Reader TTY log must contain exactly one successful test summary: $ttyLogPath" }
$passMatches = [regex]::Matches($ttyText, '(?m)^PASS [A-Za-z0-9][A-Za-z0-9_-]*\s*$')
$failMatches = [regex]::Matches($ttyText, '(?m)^FAIL\s')
$reportedPasses = [int]$summaryMatches[0].Groups[1].Value
if ($reportedPasses -lt 20 -or $reportedPasses -ne $passMatches.Count -or $failMatches.Count -ne 0) {
    throw "Reader TTY summary disagrees with named case output (reported=$reportedPasses, PASS-lines=$($passMatches.Count), FAIL-lines=$($failMatches.Count)); see $ttyLogPath"
}
Write-Output "PS3_SHADER_READER_TESTS passed=$reportedPasses failed=0"
Write-Output "ReaderTtyLog=$ttyLogPath"
