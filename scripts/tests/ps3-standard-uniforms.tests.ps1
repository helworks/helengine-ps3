<#
.SYNOPSIS
    Builds and optionally runs the standalone PPU standard-uniform provider tests.

.DESCRIPTION
    Compilation uses only the task-pinned PS3 SDK Docker image with networking
    disabled. Repository sources and the staged immutable P3SP are read-only
    container mounts; all logs, staged fixture bytes, manifests and build products
    remain under the task-owned standard-uniforms output root.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$FixturePath,
    [Parameter(Mandatory)][string]$OutputRoot,
    [string]$RpcS3Path = $env:HELENGINE_PS3_RPCS3_PATH,
    [switch]$BuildOnly,
    [string]$BuildId = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$taskRoot = [IO.Path]::GetFullPath('C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\standard-uniforms')
$expectedFixturePath = [IO.Path]::GetFullPath('C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\native-material\fixtures\producer-002\shaders\76a43ab5bef0cb1c1deca6d3a596d9a5e6f2ff86ce65b03d009a0d63b207b615.ps3sp')
$expectedFixtureBytes = 37501L
$expectedFixtureSha256 = '233615d7dc7cce7ccd3f2d57bfd1b67a969437c22be35e37033da1312a7f7cb3'
$lockedImageId = 'sha256:63a28f67dacb0a6d80c165645af126c7d6760e6a3b02e03b6e08aa5aeb96c40f'
$lockPath = Join-Path $repoRoot 'toolchain-lock.json'

function Get-Sha256 {
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Test-FullyQualifiedPath {
    param([Parameter(Mandatory)][string]$Path)
    return ($Path -match '^[A-Za-z]:[\\/]') -or ($Path -match '^\\\\[^\\]+\\[^\\]+(?:\\|$)')
}

if (-not $BuildOnly) {
    if ([string]::IsNullOrWhiteSpace($RpcS3Path)) {
        throw 'RPCS3 path is required for runtime acceptance; pass -RpcS3Path or set HELENGINE_PS3_RPCS3_PATH.'
    }
    if (-not (Test-FullyQualifiedPath -Path $RpcS3Path)) {
        throw "RPCS3 path must be fully qualified before normalization: $RpcS3Path"
    }
}

function Assert-TaskPath {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Label,
        [switch]$AllowTaskRoot
    )

    if (-not (Test-FullyQualifiedPath -Path $Path)) {
        throw "$Label must be a fully-qualified path: $Path"
    }
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $fullTaskRoot = $taskRoot.TrimEnd('\', '/')
    $prefix = $fullTaskRoot + [IO.Path]::DirectorySeparatorChar
    $isTaskRoot = [string]::Equals($fullPath, $fullTaskRoot, [StringComparison]::OrdinalIgnoreCase)
    if ((-not $AllowTaskRoot -and $isTaskRoot) -or
        (-not $isTaskRoot -and -not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase))) {
        throw "$Label must be $(if ($AllowTaskRoot) { 'at or below' } else { 'strictly below' }) $fullTaskRoot; found $fullPath"
    }
    return $fullPath
}

function Resolve-QuotedInclude {
    param(
        [Parameter(Mandatory)][string]$IncludingFile,
        [Parameter(Mandatory)][string]$IncludeName
    )

    $normalizedInclude = $IncludeName.Replace('/', [IO.Path]::DirectorySeparatorChar)
    $includingDirectory = [IO.Path]::GetDirectoryName($IncludingFile)
    $candidates = @(
        [IO.Path]::GetFullPath((Join-Path $includingDirectory $normalizedInclude)),
        [IO.Path]::GetFullPath((Join-Path (Join-Path $repoRoot 'src') $normalizedInclude)),
        [IO.Path]::GetFullPath((Join-Path $repoRoot $normalizedInclude))
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $fullCandidate = [IO.Path]::GetFullPath($candidate)
            $repoPrefix = $repoRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
            if (-not $fullCandidate.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Quoted include escapes the read-only repository mount: $IncludeName from $IncludingFile"
            }
            return $fullCandidate
        }
    }
    throw "Quoted include is missing from the source closure: $IncludeName from $IncludingFile"
}

function Get-SourceClosureHashes {
    $rootInputs = @(
        'tests/native/standard-uniforms.Makefile',
        'src/platform/ps3/Ps3ShaderArtifactReader.cpp',
        'src/platform/ps3/Ps3RsxProgramValidator.cpp',
        'src/platform/ps3/Ps3StandardUniformProvider.cpp',
        'tests/native/Ps3StandardUniformProviderTests.cpp'
    )
    $pending = [Collections.Generic.Stack[string]]::new()
    foreach ($relativePath in $rootInputs) {
        $fullPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $relativePath))
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            throw "Required source input is missing: $relativePath"
        }
        $pending.Push($fullPath)
    }

    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    while ($pending.Count -gt 0) {
        $currentPath = $pending.Pop()
        if (-not $seen.Add($currentPath)) { continue }

        $repoPrefix = $repoRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
        if (-not $currentPath.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Source include escapes the read-only repository mount: $currentPath"
        }
        $sourceText = [IO.File]::ReadAllText($currentPath)
        $includeMatches = [regex]::Matches($sourceText, '(?m)^\s*#\s*include\s+"([^"]+)"')
        foreach ($includeMatch in $includeMatches) {
            $pending.Push((Resolve-QuotedInclude -IncludingFile $currentPath -IncludeName $includeMatch.Groups[1].Value))
        }
    }

    $hashes = [ordered]@{}
    foreach ($fullPath in @($seen | Sort-Object)) {
        $repoPrefix = $repoRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
        $relativePath = $fullPath.Substring($repoPrefix.Length).Replace('\', '/')
        $hashes[$relativePath] = Get-Sha256 -Path $fullPath
    }
    return $hashes
}

function Compare-SourceClosureHashes {
    param(
        [Parameter(Mandatory)][System.Collections.IDictionary]$Before,
        [Parameter(Mandatory)][System.Collections.IDictionary]$After
    )

    if ($Before.Count -ne $After.Count) { return $false }
    foreach ($path in $Before.Keys) {
        if (-not ($After.Keys -contains $path) -or $Before[$path] -cne $After[$path]) { return $false }
    }
    return $true
}

if (-not (Test-FullyQualifiedPath -Path $FixturePath)) {
    throw 'FixturePath must be fully qualified.'
}
$FixturePath = [IO.Path]::GetFullPath($FixturePath)
if (-not [string]::Equals($FixturePath, $expectedFixturePath, [StringComparison]::OrdinalIgnoreCase)) {
    throw "FixturePath must identify the immutable ForwardStandard artifact: $expectedFixturePath"
}
$OutputRoot = Assert-TaskPath -Path $OutputRoot -Label 'OutputRoot' -AllowTaskRoot
if (-not (Test-Path -LiteralPath $FixturePath -PathType Leaf)) {
    throw "Immutable standard fixture does not exist: $FixturePath"
}
if ((Get-Item -LiteralPath $FixturePath).Length -ne $expectedFixtureBytes) {
    throw "Immutable standard fixture length differs from the brief's $expectedFixtureBytes bytes."
}
$fixtureHash = Get-Sha256 -Path $FixturePath
if ($fixtureHash -cne $expectedFixtureSha256) {
    throw "Immutable standard fixture SHA-256 differs from the brief: $fixtureHash"
}
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
    throw "Toolchain lock is missing: $lockPath"
}
$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($lock.ResolvedImage.ImageId -ne $lockedImageId) {
    throw "Toolchain lock image ID differs from this task's pinned image: $($lock.ResolvedImage.ImageId)"
}
$lockHash = Get-Sha256 -Path $lockPath

$resolvedImageId = (& docker image inspect --format '{{.Id}}' $lockedImageId 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $resolvedImageId -ne $lockedImageId) {
    throw "Docker image ID does not match the task lock: $resolvedImageId"
}

if ([string]::IsNullOrWhiteSpace($BuildId)) {
    $BuildId = 'p1-' + (Get-Date).ToUniversalTime().ToString('HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 6)
}
if ($BuildId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,39}$' -or $BuildId -in @('.', '..')) {
    throw "BuildId must be one short safe directory name; found '$BuildId'."
}
$buildRoot = Assert-TaskPath -Path (Join-Path $OutputRoot $BuildId) -Label 'BuildRoot'
if (Test-Path -LiteralPath $buildRoot) {
    throw "Refusing to reuse an existing standard-uniform build directory: $buildRoot"
}

$sourceHashesBefore = Get-SourceClosureHashes
if ((Get-Sha256 -Path $FixturePath) -cne $fixtureHash) {
    throw 'Immutable standard fixture changed during pre-build validation.'
}

New-Item -ItemType Directory -Path $OutputRoot -Force -ErrorAction Stop | Out-Null
New-Item -ItemType Directory -Path $buildRoot -ErrorAction Stop | Out-Null
$fixtureContentRoot = Join-Path $buildRoot 'USRDIR\content'
New-Item -ItemType Directory -Path $fixtureContentRoot -Force -ErrorAction Stop | Out-Null
$stagedFixturePath = Join-Path $fixtureContentRoot 'standard.ps3sp'
Copy-Item -LiteralPath $FixturePath -Destination $stagedFixturePath -ErrorAction Stop
if ((Get-Item -LiteralPath $stagedFixturePath).Length -ne $expectedFixtureBytes -or
    (Get-Sha256 -Path $stagedFixturePath) -cne $fixtureHash) {
    throw 'Staged standard fixture bytes differ from the immutable P3SP input.'
}

$buildLogPath = Join-Path $buildRoot 'build.log'
$dockerArguments = @(
    'run', '--rm', '--network', 'none',
    '--mount', "type=bind,source=$repoRoot,target=/src,readonly",
    '--mount', "type=bind,source=$buildRoot,target=/out",
    '--mount', "type=bind,source=$fixtureContentRoot,target=/app_home/USRDIR/content,readonly",
    $lockedImageId,
    'make', '-f', '/src/tests/native/standard-uniforms.Makefile', 'SRC_ROOT=/src', 'BUILD_ROOT=/out'
)
& docker @dockerArguments *> $buildLogPath
$dockerExit = $LASTEXITCODE
$buildLog = Get-Content -LiteralPath $buildLogPath -Raw
if ($buildLog.Length -gt 6000) { $buildLog = $buildLog.Substring($buildLog.Length - 6000) }
Write-Output $buildLog

$sourceHashesAfter = Get-SourceClosureHashes
if (-not (Compare-SourceClosureHashes -Before $sourceHashesBefore -After $sourceHashesAfter)) {
    $changedPaths = @($sourceHashesBefore.Keys + $sourceHashesAfter.Keys | Sort-Object -Unique | Where-Object {
        (-not ($sourceHashesBefore.Keys -contains $_)) -or (-not ($sourceHashesAfter.Keys -contains $_))
            -or $sourceHashesBefore[$_] -cne $sourceHashesAfter[$_]
    })
    throw "Source include closure changed during compilation; mixed-source build rejected: $($changedPaths -join ', ')"
}
if ((Get-Sha256 -Path $FixturePath) -cne $fixtureHash -or (Get-Sha256 -Path $stagedFixturePath) -cne $fixtureHash) {
    throw 'The immutable or staged standard fixture changed during compilation.'
}
if ((Get-Sha256 -Path $lockPath) -cne $lockHash) {
    throw 'toolchain-lock.json changed during native compilation.'
}
if ($dockerExit -ne 0) {
    throw "Locked-image PPU standard-uniform build failed with exit code $dockerExit. Full log: $buildLogPath"
}

$launchName = 'ps3_standard_uniforms_tests.self'
$artifactNames = @('ps3_standard_uniforms_tests.elf', $launchName, 'ps3_standard_uniforms_tests.fake.self')
$artifactRecords = @()
foreach ($artifactName in $artifactNames) {
    $artifactPath = Join-Path $buildRoot $artifactName
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf) -or
        (Get-Item -LiteralPath $artifactPath).Length -eq 0) {
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
    TestKind = 'NativeCpuStandardUniformProvider'
    FixtureSha256 = $fixtureHash
    FixtureBytes = $expectedFixtureBytes
    SourceHashes = $sourceHashesBefore
    LaunchArtifact = $launchName
    Artifacts = $artifactRecords
}
$manifestPath = Join-Path $buildRoot 'build-manifest.json'
$manifestTemporaryPath = $manifestPath + '.tmp'
[IO.File]::WriteAllText($manifestTemporaryPath, ($manifest | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $manifestTemporaryPath -Destination $manifestPath
Write-Output "BuildId=$BuildId"
Write-Output "BuildRoot=$buildRoot"
Write-Output "BuildManifest=$manifestPath"
Write-Output "FixtureStage=$stagedFixturePath"

if ($BuildOnly) { return }
$RpcS3Path = [IO.Path]::GetFullPath($RpcS3Path)
if (-not (Test-Path -LiteralPath $RpcS3Path -PathType Leaf)) {
    throw "RPCS3 executable does not exist: $RpcS3Path"
}
$runningEmulators = @(Get-Process -Name rpcs3 -ErrorAction SilentlyContinue)
if ($runningEmulators.Count -ne 0) {
    $runningIds = ($runningEmulators | ForEach-Object { $_.Id }) -join ', '
    throw "RPCS3 is already running (PID $runningIds). Coordinate the serialized launch slot with the parent."
}

$launcherPath = Join-Path $repoRoot 'scripts\launch_in_emulator.ps1'
& $launcherPath -ArtifactPath (Join-Path $buildRoot $launchName) -OutputRoot $OutputRoot -RpcS3Path $RpcS3Path
$runRoots = @(Get-ChildItem -LiteralPath (Join-Path $buildRoot 'emulator-runs') -Directory | Sort-Object LastWriteTimeUtc -Descending)
if ($runRoots.Count -eq 0) { throw 'Existing launcher did not create an emulator run directory.' }
$ttyLogPath = Join-Path $runRoots[0].FullName 'TTY.log'
if (-not (Test-Path -LiteralPath $ttyLogPath -PathType Leaf)) {
    throw "Existing launcher did not capture this provider run's TTY.log: $ttyLogPath"
}
$ttyText = Get-Content -LiteralPath $ttyLogPath -Raw
$summaryMatches = [regex]::Matches($ttyText, '(?m)^PS3_STANDARD_UNIFORMS_TESTS passed=(\d+) failed=0\s*$')
$passMatches = [regex]::Matches($ttyText, '(?m)^PASS ([A-Za-z0-9][A-Za-z0-9_-]*)\s*$')
$failMatches = [regex]::Matches($ttyText, '(?m)^FAIL\s')
$expectedPassNames = @(
    'real-artifact-identity', 'every-active-mapping-has-explicit-values',
    'coverage-rejects-duplicate-replacing-member', 'coverage-rejects-missing-expected-identity',
    'lit-ambient-uses-caller-radiance',
    'directional-count-type-and-direction', 'lit-without-directional-light-clears-slots',
    'unlit-uses-white-ambient-and-no-lights', 'consecutive-draws-do-not-retain-values',
    'double-sided-remains-raster-state', 'double-sided-does-not-invent-uniform',
    'lit-unlit-transition-does-not-retain-ambient', 'packed-matrix-preserves-element-order',
    'unknown-buffer-rejected-without-writes', 'unknown-member-rejected-without-writes',
    'wrong-binding-rejected-without-writes', 'wrong-scalar-type-rejected-without-writes',
    'wrong-vector-shape-rejected-without-writes', 'wrong-matrix-shape-rejected-without-writes',
    'wrong-array-length-rejected-without-writes', 'wrong-sdk-type-rejected-without-writes',
    'wrong-sdk-element-count-rejected-without-writes', 'insufficient-byte-size-rejected-without-writes',
    'null-destination-rejected', 'insufficient-capacity-rejected-without-writes',
    'nonfinite-required-value-rejected-without-writes'
)
$reportedNames = @($passMatches | ForEach-Object { $_.Groups[1].Value })
$duplicates = @($reportedNames | Group-Object | Where-Object Count -gt 1 | ForEach-Object Name)
$missing = @($expectedPassNames | Where-Object { $reportedNames -notcontains $_ })
$unexpected = @($reportedNames | Where-Object { $expectedPassNames -notcontains $_ })
if ($summaryMatches.Count -ne 1) { throw "Provider TTY log must contain exactly one successful summary: $ttyLogPath" }
$reportedPasses = [int]$summaryMatches[0].Groups[1].Value
if ($reportedPasses -lt 15 -or $reportedPasses -ne $passMatches.Count -or
    $passMatches.Count -ne $expectedPassNames.Count -or $duplicates.Count -ne 0 -or
    $missing.Count -ne 0 -or $unexpected.Count -ne 0 -or $failMatches.Count -ne 0) {
    throw "Provider TTY assertions disagree (reported=$reportedPasses, PASS-lines=$($passMatches.Count), duplicates=$($duplicates -join ','), missing=$($missing -join ','), unexpected=$($unexpected -join ','), FAIL-lines=$($failMatches.Count)); see $ttyLogPath"
}
Write-Output "PS3_STANDARD_UNIFORMS_TESTS passed=$reportedPasses failed=0"
Write-Output "ProviderTtyLog=$ttyLogPath"
