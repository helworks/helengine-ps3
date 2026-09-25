<#
.SYNOPSIS
    Builds and runs the low-level PS3 2D texture lifetime/pixel regression fixture.

.DESCRIPTION
    Uses only the pinned PS3 SDK Docker image, mounts repository sources read-only,
    stages the real authored-GPU producer fixture read-only for the guest, and requires
    fresh visible RPCS3 TTY pixel and ownership evidence for the selected normal/fault mode.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('normal', 'fault')][string]$Mode,
    [string]$FixtureRoot = 'C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\authored-gpu\fixture',
    [string]$OutputRoot = 'C:\dev\helworks\builds\helengine-ps3\2d-batch-20260924\native-task1-resources',
    [string]$RpcS3Path = $env:HELENGINE_PS3_RPCS3_PATH
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$allowedBuildParent = [IO.Path]::GetFullPath('C:\dev\helworks\builds\helengine-ps3\2d-batch-20260924')
$taskBuildRoot = [IO.Path]::GetFullPath((Join-Path $allowedBuildParent 'native-task1-resources'))
$fixtureParent = [IO.Path]::GetFullPath('C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\authored-gpu')
$lockedImageId = 'sha256:63a28f67dacb0a6d80c165645af126c7d6760e6a3b02e03b6e08aa5aeb96c40f'
$lockPath = Join-Path $repoRoot 'toolchain-lock.json'
$fixtureNames = @('fixture.ps3sp', 'vertex.bin', 'pixel.bin')
$launchName = 'ps3_batch2d_resources_tests.self'

function Get-Sha256 {
    <# Returns a lowercase SHA-256 digest for one immutable build input or result. #>
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-StrictlyContained {
    <# Resolves a fully qualified path and rejects the parent itself and lexical escapes. #>
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Parent,
        [Parameter(Mandatory)][string]$Label
    )
    $pathRoot = [IO.Path]::GetPathRoot($Path)
    if (-not [IO.Path]::IsPathRooted($Path) -or [string]::IsNullOrEmpty($pathRoot) -or $pathRoot -eq [IO.Path]::DirectorySeparatorChar.ToString() -or $pathRoot.EndsWith(':')) {
        throw "$Label must be fully qualified: $Path"
    }
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $fullParent = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
    if (-not $fullPath.StartsWith($fullParent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be a strict descendant of $fullParent; found $fullPath"
    }
    return $fullPath
}

function Get-RepositoryInputSnapshot {
    <# Hashes this fixture's native sources and every recursively included checkout header. #>
    param([Parameter(Mandatory)][string]$Root)
    $seeds = @(
        'tests/Makefile.batch2d-resources',
        'tests/ps3_batch2d_resources_tests.cpp',
        'src/platform/ps3/Ps3AuthoredProgram.cpp',
        'src/platform/ps3/Ps3AuthoredTexture.cpp',
        'src/platform/ps3/Ps3ShaderArtifactReader.cpp',
        'src/platform/ps3/Ps3RsxProgramValidator.cpp',
        'src/platform/ps3/Ps3Video.cpp'
    )
    $queue = [Collections.Generic.Queue[string]]::new()
    foreach ($seed in $seeds) { $queue.Enqueue((Join-Path $Root $seed)) }
    $paths = [Collections.Generic.SortedDictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
    while ($queue.Count -gt 0) {
        $current = [IO.Path]::GetFullPath($queue.Dequeue())
        if ($paths.ContainsKey($current)) { continue }
        if (-not (Test-Path -LiteralPath $current -PathType Leaf)) {
            throw "Required low-level resource-test input is missing: $current"
        }
        $paths.Add($current, (Get-Sha256 -Path $current))
        $text = Get-Content -LiteralPath $current -Raw
        foreach ($match in [regex]::Matches($text, '(?m)^\s*#\s*include\s*"([^"]+)"')) {
            $includeName = $match.Groups[1].Value
            $candidates = @(
                (Join-Path (Split-Path -Parent $current) $includeName),
                (Join-Path $Root $includeName),
                (Join-Path (Join-Path $Root 'src') $includeName)
            )
            foreach ($candidate in $candidates) {
                if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
                $resolved = [IO.Path]::GetFullPath($candidate)
                $repoPrefix = $Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
                if ($resolved.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                    $queue.Enqueue($resolved)
                }
                break
            }
        }
    }
    return $paths
}

function Assert-RepositorySnapshotUnchanged {
    <# Refuses evidence if an input changed while its build or emulator phase was running. #>
    param(
        [Parameter(Mandatory)][Collections.Generic.SortedDictionary[string, string]]$Snapshot,
        [Parameter(Mandatory)][string]$Phase
    )
    foreach ($path in $Snapshot.Keys) {
        if ((Get-Sha256 -Path $path) -ne $Snapshot[$path]) {
            throw "Repository source changed $Phase; refusing mixed-input evidence: $path"
        }
    }
}

$FixtureRoot = Assert-StrictlyContained -Path $FixtureRoot -Parent $fixtureParent -Label 'FixtureRoot'
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (-not $OutputRoot.Equals($taskBuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must equal the approved visible task build root $taskBuildRoot."
}
if (-not (Test-Path -LiteralPath $FixtureRoot -PathType Container)) {
    throw "Producer fixture directory does not exist: $FixtureRoot"
}
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
    throw "Toolchain lock is missing: $lockPath"
}
if ([string]::IsNullOrWhiteSpace($RpcS3Path)) {
    throw 'RPCS3 path is required: pass -RpcS3Path or set HELENGINE_PS3_RPCS3_PATH.'
}
$RpcS3Path = [IO.Path]::GetFullPath($RpcS3Path)
if (-not (Test-Path -LiteralPath $RpcS3Path -PathType Leaf)) {
    throw "RPCS3 executable does not exist: $RpcS3Path"
}

$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($lock.ResolvedImage.ImageId -ne $lockedImageId) {
    throw "toolchain-lock.json image ID differs from this task's pinned image: $($lock.ResolvedImage.ImageId)"
}
$lockHash = Get-Sha256 -Path $lockPath
$fixtureHashes = [ordered]@{}
foreach ($fixtureName in $fixtureNames) {
    $fixturePath = Join-Path $FixtureRoot $fixtureName
    if (-not (Test-Path -LiteralPath $fixturePath -PathType Leaf) -or (Get-Item -LiteralPath $fixturePath).Length -eq 0) {
        throw "Required nonempty producer fixture is missing: $fixturePath"
    }
    $fixtureHashes[$fixtureName] = Get-Sha256 -Path $fixturePath
}
$sourceSnapshot = Get-RepositoryInputSnapshot -Root $repoRoot

$nativeErrorPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $imageOutput = & docker image inspect --format '{{.Id}}' $lockedImageId 2>&1
    $inspectExit = $LASTEXITCODE
    $resolvedImageId = ($imageOutput | Out-String).Trim()
} finally {
    $ErrorActionPreference = $nativeErrorPreference
}
if ($inspectExit -ne 0 -or $resolvedImageId -cne $lockedImageId) {
    throw "Docker image ID does not match the task lock: $resolvedImageId"
}
if ((Get-Sha256 -Path $lockPath) -ne $lockHash) {
    throw 'toolchain-lock.json changed during preflight.'
}
Assert-RepositorySnapshotUnchanged -Snapshot $sourceSnapshot -Phase 'during preflight'

if (-not (Test-Path -LiteralPath $OutputRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $OutputRoot -ErrorAction Stop | Out-Null
}
$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
$buildId = "native-task1-$Mode-$stamp-$([guid]::NewGuid().ToString('N'))"
if ($buildId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$') {
    throw "Generated unsafe build ID: $buildId"
}
$buildRoot = Assert-StrictlyContained -Path (Join-Path $OutputRoot $buildId) -Parent $OutputRoot -Label 'BuildRoot'
if (Test-Path -LiteralPath $buildRoot) {
    throw "Refusing to reuse a previous task output directory: $buildRoot"
}
New-Item -ItemType Directory -Path $buildRoot -ErrorAction Stop | Out-Null
New-Item -ItemType Directory -Path (Join-Path $buildRoot 'temp') -ErrorAction Stop | Out-Null
$contentRoot = Join-Path $buildRoot 'USRDIR\content'
New-Item -ItemType Directory -Path $contentRoot -ErrorAction Stop | Out-Null
foreach ($fixtureName in $fixtureNames) {
    $sourcePath = Join-Path $FixtureRoot $fixtureName
    if ((Get-Sha256 -Path $sourcePath) -ne $fixtureHashes[$fixtureName]) {
        throw "Producer fixture changed before staging: $fixtureName"
    }
    $stagedPath = Join-Path $contentRoot $fixtureName
    Copy-Item -LiteralPath $sourcePath -Destination $stagedPath -ErrorAction Stop
    if ((Get-Sha256 -Path $stagedPath) -ne $fixtureHashes[$fixtureName]) {
        throw "Staged fixture hash differs from its producer input: $fixtureName"
    }
}

$buildLogPath = Join-Path $buildRoot 'build.log'
$withholdRuntimeLabel = if ($Mode -eq 'fault') { '1' } else { '0' }
$dockerArguments = @(
    'run', '--rm', '--network', 'none',
    '--mount', "type=bind,source=$repoRoot,target=/src,readonly",
    '--mount', "type=bind,source=$buildRoot,target=/out",
    '--mount', "type=bind,source=$contentRoot,target=/app_home/USRDIR/content,readonly",
    '--env', 'TMPDIR=/out/temp', '--env', 'TMP=/out/temp', '--env', 'TEMP=/out/temp',
    $lockedImageId,
    'make', '-f', '/src/tests/Makefile.batch2d-resources', 'SRC_ROOT=/src', 'BUILD_ROOT=/out',
    "TEST_WITHHOLD_RUNTIME_LABEL=$withholdRuntimeLabel"
)
$oldTemporaryEnvironment = @{}
foreach ($name in @('TEMP', 'TMP', 'TMPDIR')) {
    $oldTemporaryEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    [Environment]::SetEnvironmentVariable($name, (Join-Path $buildRoot 'temp'), 'Process')
}
$nativeErrorPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & docker @dockerArguments *> $buildLogPath
    $buildExit = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $nativeErrorPreference
    foreach ($name in @('TEMP', 'TMP', 'TMPDIR')) {
        [Environment]::SetEnvironmentVariable($name, $oldTemporaryEnvironment[$name], 'Process')
    }
}
$buildLog = Get-Content -LiteralPath $buildLogPath -Raw
if ($buildLog.Length -gt 7000) { $buildLog = $buildLog.Substring($buildLog.Length - 7000) }
Write-Output $buildLog
if ($buildExit -ne 0) {
    throw "Locked-image low-level resource PPU build failed with exit code $buildExit. Full log: $buildLogPath"
}

Assert-RepositorySnapshotUnchanged -Snapshot $sourceSnapshot -Phase 'during PPU compilation'
if ((Get-Sha256 -Path $lockPath) -ne $lockHash) {
    throw 'toolchain-lock.json changed during PPU compilation.'
}
foreach ($fixtureName in $fixtureNames) {
    $sourcePath = Join-Path $FixtureRoot $fixtureName
    $stagedPath = Join-Path $contentRoot $fixtureName
    if ((Get-Sha256 -Path $sourcePath) -ne $fixtureHashes[$fixtureName] -or (Get-Sha256 -Path $stagedPath) -ne $fixtureHashes[$fixtureName]) {
        throw "Producer or staged fixture changed during native compilation: $fixtureName"
    }
}

$artifactNames = @('ps3_batch2d_resources_tests.elf', $launchName, 'ps3_batch2d_resources_tests.fake.self')
$artifactRecords = @()
foreach ($artifactName in $artifactNames) {
    $artifactPath = Join-Path $buildRoot $artifactName
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf) -or (Get-Item -LiteralPath $artifactPath).Length -eq 0) {
        throw "Successful PPU build omitted nonempty artifact $artifactName; see $buildLogPath"
    }
    $artifactRecords += [pscustomobject]@{ Path = $artifactName; Sha256 = Get-Sha256 -Path $artifactPath }
}
$sourceRecords = @(
    foreach ($sourcePath in $sourceSnapshot.Keys) {
        [pscustomobject]@{
            Path = $sourcePath.Substring($repoRoot.TrimEnd('\', '/').Length).TrimStart('\', '/').Replace('\', '/')
            Sha256 = $sourceSnapshot[$sourcePath]
        }
    }
)
$manifest = [ordered]@{
    SchemaVersion = 1
    BuildId = $buildId
    ToolchainLockSha256 = $lockHash
    ToolchainImageId = $lockedImageId
    TestKind = "Ps3Batch2DTextureResources-$Mode"
    SourceHashes = $sourceRecords
    FixtureHashes = $fixtureHashes
    LaunchArtifact = $launchName
    Artifacts = $artifactRecords
}
$manifestPath = Join-Path $buildRoot 'build-manifest.json'
$manifestTemporaryPath = Join-Path $buildRoot 'build-manifest.json.tmp'
[IO.File]::WriteAllText($manifestTemporaryPath, ($manifest | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
[IO.File]::Move($manifestTemporaryPath, $manifestPath)
Write-Output "BuildId=$buildId"
Write-Output "BuildRoot=$buildRoot"
Write-Output "BuildManifest=$manifestPath"
Write-Output "ToolchainImageId=$lockedImageId"
Write-Output "ToolchainLockSha256=$lockHash"

if (@(Get-Process -Name rpcs3 -ErrorAction SilentlyContinue).Count -ne 0) {
    throw 'RPCS3 is already running; refusing to take a concurrent emulator slot.'
}
$emulatorLogPath = Join-Path (Split-Path -Parent $RpcS3Path) 'log\TTY.log'
$launchStartedUtc = [DateTime]::UtcNow
$launcherPath = Join-Path $repoRoot 'scripts\launch_in_emulator.ps1'
$launcherLogPath = Join-Path $buildRoot 'launcher.log'
try {
    & $launcherPath -ArtifactPath (Join-Path $buildRoot $launchName) -OutputRoot $OutputRoot -RpcS3Path $RpcS3Path -Visible *> $launcherLogPath
} catch {
    Add-Content -LiteralPath $launcherLogPath -Encoding UTF8 -Value $_.ToString()
    throw "Verified visible emulator launch failed. See $launcherLogPath"
}

$runRoots = @(Get-ChildItem -LiteralPath (Join-Path $buildRoot 'emulator-runs') -Directory)
if ($runRoots.Count -ne 1) {
    throw "Fresh build must contain exactly one emulator run directory; found $($runRoots.Count)."
}
$runRoot = $runRoots[0].FullName
$runRecord = Get-Content -LiteralPath (Join-Path $runRoot 'emulator-command.txt') -Raw -Encoding UTF8
if ($runRecord -notmatch [regex]::Escape("BuildId=$buildId") -or $runRecord -notmatch [regex]::Escape('Visible=True')) {
    throw "Launcher run record does not identify this fresh visible build: $runRoot"
}
$ttyLogPath = Join-Path $runRoot 'TTY.log'
if (-not (Test-Path -LiteralPath $ttyLogPath -PathType Leaf) -or -not (Test-Path -LiteralPath $emulatorLogPath -PathType Leaf)) {
    throw "Fresh emulator TTY output is missing; refusing compile-only acceptance: $runRoot"
}
$sourceTtyInfo = Get-Item -LiteralPath $emulatorLogPath
if ($sourceTtyInfo.LastWriteTimeUtc -lt $launchStartedUtc.AddSeconds(-2)) {
    throw "RPCS3 TTY log was not updated by this run: $emulatorLogPath"
}
$freshTtyHash = Get-Sha256 -Path $ttyLogPath
if ($freshTtyHash -ne (Get-Sha256 -Path $emulatorLogPath)) {
    throw 'Captured TTY log differs from the current visible RPCS3 run.'
}
if (@(Get-Process -Name rpcs3 -ErrorAction SilentlyContinue).Count -ne 0) {
    throw 'RPCS3 remains running after the visible test; coordinate its release before another emulator gate.'
}
Assert-RepositorySnapshotUnchanged -Snapshot $sourceSnapshot -Phase 'during visible RPCS3 execution'

$ttyText = Get-Content -LiteralPath $ttyLogPath -Raw
$modeRecord = [regex]::Matches($ttyText, "(?m)^PS3_BATCH2D_RESOURCES_MODE mode=$Mode\s*$")
$summaryMatches = [regex]::Matches($ttyText, "(?m)^PS3_BATCH2D_RESOURCES_TESTS mode=$Mode passed=(\d+) failed=0\s*$")
if ($modeRecord.Count -ne 1 -or $summaryMatches.Count -ne 1) {
    throw "Fresh TTY must identify exactly one successful $Mode fixture summary: $ttyLogPath"
}
$passMatches = [regex]::Matches($ttyText, '(?m)^PASS [A-Za-z0-9][A-Za-z0-9_-]*\s*$')
$failMatches = [regex]::Matches($ttyText, '(?m)^FAIL\s')
$reportedPasses = [int]$summaryMatches[0].Groups[1].Value
if ($reportedPasses -lt 10 -or $reportedPasses -ne $passMatches.Count -or $failMatches.Count -ne 0) {
    throw "Named assertion lines disagree with the fixture summary (reported=$reportedPasses, PASS-lines=$($passMatches.Count), FAIL-lines=$($failMatches.Count))."
}
$artifactIdentity = [regex]::Matches($ttyText,
    '(?m)^PS3_BATCH2D_ARTIFACT asset=authored-gpu-fixture vertex=gpu\.vs pixel=gpu\.ps variant=default\s*$')
if ($artifactIdentity.Count -ne 1) {
    throw 'Fresh GPU output did not identify the exact authored-GPU producer fixture.'
}
$mappingMatches = [regex]::Matches($ttyText,
    '(?m)^PS3_BATCH2D_MAPPING positionSlot=(\d+) uvSlot=(\d+) textureUnit=(\d+)\s*$')
if ($mappingMatches.Count -ne 1 -or [int]$mappingMatches[0].Groups[1].Value -gt 15 -or [int]$mappingMatches[0].Groups[2].Value -gt 15 -or [int]$mappingMatches[0].Groups[3].Value -gt 15) {
    throw 'GPU output is missing valid SDK-mapped vertex attributes or Albedo texture unit.'
}

if ($Mode -eq 'normal') {
    $expectedPixels = @(
        @{ Phase = 'initial-nearest'; Name = 'top-left'; Sample = '0.5000,0.3750'; Rgb = @(255, 0, 0); Tolerance = 3 },
        @{ Phase = 'initial-nearest'; Name = 'top-right'; Sample = '0.7500,0.3750'; Rgb = @(0, 255, 0); Tolerance = 3 },
        @{ Phase = 'initial-nearest'; Name = 'bottom-left'; Sample = '0.5000,0.7500'; Rgb = @(0, 0, 255); Tolerance = 3 },
        @{ Phase = 'initial-nearest'; Name = 'bottom-right'; Sample = '0.7500,0.7500'; Rgb = @(255, 255, 0); Tolerance = 3 },
        @{ Phase = 'initial-nearest'; Name = 'nearest-midpoint-selects-left-texel'; Sample = '0.5625,0.3750'; Rgb = @(255, 0, 0); Tolerance = 3 },
        @{ Phase = 'padded-update'; Name = 'top-left'; Sample = '0.5000,0.3750'; Rgb = @(255, 0, 0); Tolerance = 3 },
        @{ Phase = 'padded-update'; Name = 'top-right'; Sample = '0.7500,0.3750'; Rgb = @(255, 0, 255); Tolerance = 3 },
        @{ Phase = 'padded-update'; Name = 'bottom-left'; Sample = '0.5000,0.7500'; Rgb = @(0, 0, 255); Tolerance = 3 },
        @{ Phase = 'padded-update'; Name = 'bottom-right'; Sample = '0.7500,0.7500'; Rgb = @(0, 255, 255); Tolerance = 3 },
        @{ Phase = 'padded-update'; Name = 'nearest-path-remains-nearest'; Sample = '0.5625,0.3750'; Rgb = @(255, 0, 0); Tolerance = 3 },
        @{ Phase = 'linear-clamp'; Name = 'linear-filter-mixes-neighbor-texels'; Sample = '0.5833,0.3750'; Rgb = @(255, 0, 64); Tolerance = 5 },
        @{ Phase = 'linear-clamp'; Name = 'linear-clamp-keeps-outside-u-on-edge-texel'; Sample = '0.4250,0.3750'; Rgb = @(255, 0, 0); Tolerance = 5 }
    )
} else {
    $expectedPixels = @(
        @{ Phase = 'fault-before-timeout'; Name = 'top-left'; Sample = '0.5000,0.3750'; Rgb = @(255, 0, 0); Tolerance = 3 },
        @{ Phase = 'fault-before-timeout'; Name = 'top-right'; Sample = '0.7500,0.3750'; Rgb = @(0, 255, 0); Tolerance = 3 },
        @{ Phase = 'fault-before-timeout'; Name = 'bottom-left'; Sample = '0.5000,0.7500'; Rgb = @(0, 0, 255); Tolerance = 3 },
        @{ Phase = 'fault-before-timeout'; Name = 'bottom-right'; Sample = '0.7500,0.7500'; Rgb = @(255, 255, 0); Tolerance = 3 },
        @{ Phase = 'fault-after-timeout'; Name = 'top-left'; Sample = '0.5000,0.3750'; Rgb = @(255, 0, 0); Tolerance = 3 },
        @{ Phase = 'fault-after-timeout'; Name = 'top-right'; Sample = '0.7500,0.3750'; Rgb = @(0, 255, 0); Tolerance = 3 },
        @{ Phase = 'fault-after-timeout'; Name = 'bottom-left'; Sample = '0.5000,0.7500'; Rgb = @(0, 0, 255); Tolerance = 3 },
        @{ Phase = 'fault-after-timeout'; Name = 'bottom-right'; Sample = '0.7500,0.7500'; Rgb = @(255, 255, 0); Tolerance = 3 }
    )
}
$pixelMatches = [regex]::Matches($ttyText,
    '(?m)^PS3_BATCH2D_PIXEL phase=([^ ]+) name=([^ ]+) sample=\(([0-9.]+),([0-9.]+)\) expected=\((\d+),(\d+),(\d+)\) actual=\((\d+),(\d+),(\d+)\)\s*$')
if ($pixelMatches.Count -ne $expectedPixels.Count) {
    throw "Expected exactly $($expectedPixels.Count) concrete $Mode RGB pixel records; found $($pixelMatches.Count)."
}
$seenPixels = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($pixelMatch in $pixelMatches) {
    $sample = "$($pixelMatch.Groups[3].Value),$($pixelMatch.Groups[4].Value)"
    $key = "$($pixelMatch.Groups[1].Value):$($pixelMatch.Groups[2].Value)"
    if (-not $seenPixels.Add($key)) { throw "Pixel evidence repeats sample $key." }
    $expected = @($expectedPixels | Where-Object {
        $_.Phase -eq $pixelMatch.Groups[1].Value -and $_.Name -eq $pixelMatch.Groups[2].Value
    })
    if ($expected.Count -ne 1 -or $expected[0].Sample -ne $sample) {
        throw "Pixel evidence contains an unplanned phase/name/sample: $key ($sample)."
    }
    $expectedRgb = @($expected[0].Rgb)
    $reportedRgb = @([int]$pixelMatch.Groups[5].Value, [int]$pixelMatch.Groups[6].Value, [int]$pixelMatch.Groups[7].Value)
    $actualRgb = @([int]$pixelMatch.Groups[8].Value, [int]$pixelMatch.Groups[9].Value, [int]$pixelMatch.Groups[10].Value)
    for ($channel = 0; $channel -lt 3; ++$channel) {
        if ($reportedRgb[$channel] -ne $expectedRgb[$channel] -or [Math]::Abs($actualRgb[$channel] - $expectedRgb[$channel]) -gt $expected[0].Tolerance) {
            throw "Pixel $key differs from its independent RGB expectation: expected=$($expectedRgb -join ','), actual=$($actualRgb -join ',')"
        }
    }
}
if ($seenPixels.Count -ne $expectedPixels.Count) {
    throw "GPU output omitted a planned pixel sample; found $($seenPixels.Count) of $($expectedPixels.Count)."
}

$ownerMatches = [regex]::Matches($ttyText,
    "(?m)^PS3_BATCH2D_RESOURCE_OWNERS mode=$Mode textureBytes=(\d+) programBytes=(\d+)(?: cubeBytes=(\d+))? videoDrained=(\d+)\s*$")
if ($ownerMatches.Count -ne 1) {
    throw "Fresh $Mode output must contain one terminal texture/program ownership record."
}
$textureBytes = [int64]$ownerMatches[0].Groups[1].Value
$programBytes = [int64]$ownerMatches[0].Groups[2].Value
$cubeBytes = if ($ownerMatches[0].Groups[3].Success) { [int64]$ownerMatches[0].Groups[3].Value } else { 0 }
$videoDrained = [int]$ownerMatches[0].Groups[4].Value
$shutdownPattern = if ($Mode -eq 'normal') {
    '(?m)^PS3_VIDEO_SHUTDOWN drained=1 drainFailed=0 freedBytes=(\d+) retainedRsxBytes=(\d+) retainedHostBytes=(\d+) frames=(\d+)\s+'
} else {
    '(?m)^PS3_VIDEO_SHUTDOWN drained=0 drainFailed=1 freedBytes=0 retainedRsxBytes=(\d+) retainedHostBytes=(\d+) frames=(\d+)\s+'
}
$shutdownMatches = [regex]::Matches($ttyText, $shutdownPattern)
if ($shutdownMatches.Count -ne 1) {
    throw "Video shutdown ownership accounting does not match the $Mode lifetime path."
}
$videoFreedRsxBytes = if ($Mode -eq 'normal') { [int64]$shutdownMatches[0].Groups[1].Value } else { 0 }
$videoRetainedRsxBytes = if ($Mode -eq 'normal') { [int64]$shutdownMatches[0].Groups[2].Value } else { [int64]$shutdownMatches[0].Groups[1].Value }
$videoRetainedHostBytes = if ($Mode -eq 'normal') { [int64]$shutdownMatches[0].Groups[3].Value } else { [int64]$shutdownMatches[0].Groups[2].Value }
if ($Mode -eq 'normal') {
    if ($textureBytes -ne 0 -or $programBytes -ne 0 -or $cubeBytes -ne 0 -or $videoDrained -ne 1) {
        throw 'Normal final drain did not return texture/program/cube ownership to zero.'
    }
    if ($videoFreedRsxBytes -le 0 -or $videoRetainedRsxBytes -le 0 -or $videoRetainedHostBytes -le 0) {
        throw 'Normal shutdown did not distinguish released depth memory from retained display/context allocations.'
    }
    if ($ttyText -notmatch '(?m)^PS3_AUTHORED_TEXTURE_RETIRED\s*$' -or $ttyText -notmatch '(?m)^PS3_DRAIN_COMPLETE label=\d+ frames=\d+\s*$' -or ([regex]::Matches($ttyText, '(?m)^PS3_SUBMITTED_WORK_COMPLETE label=\d+ frames=\d+ drained=0\s*$')).Count -lt 1) {
        throw 'Normal output is missing successful runtime-wait, retirement, or final-drain evidence.'
    }
} else {
    if ($textureBytes -ne 16 -or $programBytes -le 0 -or $videoDrained -ne 0 -or $videoRetainedRsxBytes -le 0 -or $videoRetainedHostBytes -le 0) {
        throw 'Fault output did not retain the 16-byte texture and live shader allocation without drain authorization.'
    }
    if ($ttyText -notmatch '(?m)^PS3_TEST_FAULT_WITHHELD_RUNTIME_LABEL label=\d+\s*$' -or $ttyText -notmatch '(?m)^PS3_DRAIN_TIMEOUT label=\d+ observed=\d+ waitedMicroseconds=\d+\s*$' -or $ttyText -notmatch '(?m)^PS3_DRAIN_SKIPPED reason=video-unavailable drainFailed=1\s*$') {
        throw 'Fault output is missing the injected timeout or latched-drain rejection evidence.'
    }
}

Write-Output "PS3_BATCH2D_RESOURCES_TESTS mode=$Mode passed=$reportedPasses failed=0"
Write-Output "PixelsVerified=$($pixelMatches.Count)"
Write-Output "TextureOwnedGpuBytes=$textureBytes"
Write-Output "ProgramOwnedGpuBytes=$programBytes"
Write-Output "CubeOwnedGpuBytes=$cubeBytes"
Write-Output "VideoDrained=$videoDrained"
Write-Output "VideoFreedRsxBytes=$videoFreedRsxBytes"
Write-Output "VideoRetainedRsxBytes=$videoRetainedRsxBytes"
Write-Output "VideoRetainedHostBytes=$videoRetainedHostBytes"
Write-Output "ResourceVisibleRun=$runRoot"
Write-Output "ResourceFreshTtySha256=$freshTtyHash"
