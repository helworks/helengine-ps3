<#
.SYNOPSIS
    Builds and optionally runs the standalone PPU P3MT reader acceptance target.

.DESCRIPTION
    The locked PS3 SDK image compiles the native reader against immutable repository
    and producer-fixture mounts. Staged content, generated files and logs stay below
    the native-material build root. RPCS3 execution is CPU-only and uses the existing
    launcher after a parent-coordinated emulator slot is available.
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
$nativeMaterialRoot = [IO.Path]::GetFullPath('C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\native-material')
$lockedImageId = 'sha256:63a28f67dacb0a6d80c165645af126c7d6760e6a3b02e03b6e08aa5aeb96c40f'
$lockPath = Join-Path $repoRoot 'toolchain-lock.json'
$launchName = 'ps3_material_reader_tests.self'
$artifactNames = @('ps3_material_reader_tests.elf', $launchName, 'ps3_material_reader_tests.fake.self')
$fixtureNames = @(
    'material.p3mt',
    'shaders/76a43ab5bef0cb1c1deca6d3a596d9a5e6f2ff86ce65b03d009a0d63b207b615.ps3sp'
)

function Get-Sha256 {
    <# Returns the lowercase SHA-256 digest of one immutable task input or output. #>
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-StrictChildPath {
    <# Requires a fully qualified path to be a strict descendant of its named parent. #>
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Parent,
        [Parameter(Mandatory)][string]$Label
    )
    $fullyQualifiedPathPattern = '^(?:[A-Za-z]:[\\/]|(?:\\\\|//)(?![?.][\\/])[^\\/]+[\\/][^\\/]+(?:[\\/]|$))'
    if ($Path -notmatch $fullyQualifiedPathPattern) {
        throw "$Label must be a fully qualified drive-absolute or complete UNC path."
    }
    if ($Parent -notmatch $fullyQualifiedPathPattern) {
        throw "$Label parent must be a fully qualified drive-absolute or complete UNC path."
    }
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $fullParent = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
    $strictPrefix = $fullParent + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($strictPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be a strict child of $fullParent; found $fullPath"
    }
    return $fullPath
}

function Get-FixturePath {
    <# Resolves one fixed, validated producer relative path below the fixture root. #>
    param(
        [Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][string]$RelativePath
    )
    $localPath = $RelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar)
    $fullPath = [IO.Path]::GetFullPath((Join-Path $Root $localPath))
    $strictPrefix = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($strictPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Producer fixture path escaped its root: $RelativePath"
    }
    return $fullPath
}

$FixtureRoot = Assert-StrictChildPath -Path $FixtureRoot -Parent $nativeMaterialRoot -Label 'FixtureRoot'
$OutputRoot = Assert-StrictChildPath -Path $OutputRoot -Parent $nativeMaterialRoot -Label 'OutputRoot'
if (-not (Test-Path -LiteralPath $FixtureRoot -PathType Container)) {
    throw "FixtureRoot does not exist: $FixtureRoot"
}
if (-not (Test-Path -LiteralPath $OutputRoot -PathType Container)) {
    throw "OutputRoot does not exist: $OutputRoot"
}
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
    throw "Toolchain lock is missing: $lockPath"
}
$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($lock.ResolvedImage.ImageId -ne $lockedImageId) {
    throw "Toolchain lock image ID is not the task-pinned image: $($lock.ResolvedImage.ImageId)"
}
$lockHash = Get-Sha256 -Path $lockPath

$fixtureHashes = [ordered]@{}
foreach ($fixtureName in $fixtureNames) {
    $fixturePath = Get-FixturePath -Root $FixtureRoot -RelativePath $fixtureName
    if (-not (Test-Path -LiteralPath $fixturePath -PathType Leaf)) {
        throw "Required production fixture is missing: $fixturePath"
    }
    if ((Get-Item -LiteralPath $fixturePath).Length -eq 0) {
        throw "Required production fixture is empty: $fixturePath"
    }
    $fixtureHashes[$fixtureName] = Get-Sha256 -Path $fixturePath
}

$resolvedImageId = (& docker image inspect --format '{{.Id}}' $lockedImageId 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $resolvedImageId -ne $lockedImageId) {
    throw "Docker image ID does not match toolchain-lock.json: $resolvedImageId"
}

if ([string]::IsNullOrWhiteSpace($BuildId)) {
    $stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
    $BuildId = "material-reader-$stamp-$([guid]::NewGuid().ToString('N'))"
}
if ($BuildId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$' -or $BuildId -in @('.', '..')) {
    throw "BuildId must be a single safe directory name; found '$BuildId'."
}
$buildRoot = Join-Path $OutputRoot $BuildId
if (Test-Path -LiteralPath $buildRoot) {
    throw "Refusing to reuse an existing native material build directory: $buildRoot"
}
New-Item -ItemType Directory -Path $buildRoot -ErrorAction Stop | Out-Null
$tempRoot = Join-Path $buildRoot 'tmp'
New-Item -ItemType Directory -Path $tempRoot -ErrorAction Stop | Out-Null
$env:TEMP = $tempRoot
$env:TMP = $tempRoot

$contentRoot = Join-Path $buildRoot 'USRDIR\content'
New-Item -ItemType Directory -Path $contentRoot -Force -ErrorAction Stop | Out-Null
foreach ($fixtureName in $fixtureNames) {
    $sourcePath = Get-FixturePath -Root $FixtureRoot -RelativePath $fixtureName
    if ((Get-Sha256 -Path $sourcePath) -ne $fixtureHashes[$fixtureName]) {
        throw "Producer fixture changed before staging: $fixtureName"
    }
    $stagedPath = Get-FixturePath -Root $contentRoot -RelativePath $fixtureName
    $stagedParent = Split-Path -Parent $stagedPath
    New-Item -ItemType Directory -Path $stagedParent -Force -ErrorAction Stop | Out-Null
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
    $lockedImageId,
    'make', '-f', '/src/tests/native/material-reader.Makefile', 'SRC_ROOT=/src', 'BUILD_ROOT=/out'
)
& docker @dockerArguments *> $buildLogPath
$dockerExit = $LASTEXITCODE
$buildLog = Get-Content -LiteralPath $buildLogPath -Raw
if ($buildLog.Length -gt 6000) { $buildLog = $buildLog.Substring($buildLog.Length - 6000) }
Write-Output $buildLog
if ($dockerExit -ne 0) {
    throw "Locked-image PPU material reader build failed with exit code $dockerExit. Full log: $buildLogPath"
}

foreach ($fixtureName in $fixtureNames) {
    $sourcePath = Get-FixturePath -Root $FixtureRoot -RelativePath $fixtureName
    if ((Get-Sha256 -Path $sourcePath) -ne $fixtureHashes[$fixtureName]) {
        throw "Producer fixture changed during native material compilation: $fixtureName"
    }
}
if ((Get-Sha256 -Path $lockPath) -ne $lockHash) {
    throw 'toolchain-lock.json changed during native material compilation.'
}

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
    TestKind = 'NativeCpuMaterialArtifactReader'
    SourceHashes = $fixtureHashes
    LaunchArtifact = $launchName
    Artifacts = $artifactRecords
}
$manifestPath = Join-Path $buildRoot 'build-manifest.json'
$manifestTemporaryPath = $manifestPath + '.tmp'
$manifestJson = $manifest | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText($manifestTemporaryPath, $manifestJson, [Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $manifestTemporaryPath -Destination $manifestPath
Write-Output "BuildId=$BuildId"
Write-Output "BuildRoot=$buildRoot"
Write-Output "BuildManifest=$manifestPath"

if ($BuildOnly) { return }
if ([string]::IsNullOrWhiteSpace($RpcS3Path)) {
    throw 'RPCS3 path is required for runtime acceptance; pass -RpcS3Path or set HELENGINE_PS3_RPCS3_PATH after the parent has assigned a launch slot.'
}
$RpcS3Path = [IO.Path]::GetFullPath($RpcS3Path)
if (-not (Test-Path -LiteralPath $RpcS3Path -PathType Leaf)) {
    throw "RPCS3 executable does not exist: $RpcS3Path"
}
$runningEmulators = @(Get-Process -Name rpcs3 -ErrorAction SilentlyContinue)
if ($runningEmulators.Count -ne 0) {
    $runningIds = ($runningEmulators | ForEach-Object { $_.Id }) -join ', '
    throw "RPCS3 is already running (PID $runningIds). Do not start a concurrent material test; coordinate the launch slot with the parent."
}

$launcherPath = Join-Path $repoRoot 'scripts\launch_in_emulator.ps1'
& $launcherPath -ArtifactPath (Join-Path $buildRoot $launchName) -OutputRoot $OutputRoot -RpcS3Path $RpcS3Path
$runRoot = Join-Path $buildRoot 'emulator-runs'
$runRoots = @(Get-ChildItem -LiteralPath $runRoot -Directory | Sort-Object LastWriteTimeUtc -Descending)
if ($runRoots.Count -eq 0) { throw "Existing launcher did not create an emulator run directory: $runRoot" }
$ttyLogPath = Join-Path $runRoots[0].FullName 'TTY.log'
if (-not (Test-Path -LiteralPath $ttyLogPath -PathType Leaf)) {
    throw "Existing launcher did not capture this material reader run's TTY.log: $ttyLogPath"
}
$ttyText = Get-Content -LiteralPath $ttyLogPath -Raw
$summaryMatches = [regex]::Matches($ttyText, '(?m)^PS3_MATERIAL_READER_TESTS passed=(\d+) failed=0\s*$')
if ($summaryMatches.Count -ne 1) { throw "Material reader TTY log must contain exactly one successful test summary: $ttyLogPath" }
$passMatches = [regex]::Matches($ttyText, '(?m)^PASS [A-Za-z0-9][A-Za-z0-9_-]*\s*$')
$failMatches = [regex]::Matches($ttyText, '(?m)^FAIL\s')
$reportedPasses = [int]$summaryMatches[0].Groups[1].Value
if ($reportedPasses -lt 25 -or $reportedPasses -ne $passMatches.Count -or $failMatches.Count -ne 0) {
    throw "Material reader TTY summary disagrees with named case output (reported=$reportedPasses, PASS-lines=$($passMatches.Count), FAIL-lines=$($failMatches.Count)); see $ttyLogPath"
}
foreach ($fixtureName in $fixtureNames) {
    $sourcePath = Get-FixturePath -Root $FixtureRoot -RelativePath $fixtureName
    if ((Get-Sha256 -Path $sourcePath) -ne $fixtureHashes[$fixtureName]) {
        throw "Producer fixture changed during native material tests: $fixtureName"
    }
}
Write-Output "PS3_MATERIAL_READER_TESTS passed=$reportedPasses failed=0"
Write-Output "ReaderTtyLog=$ttyLogPath"
