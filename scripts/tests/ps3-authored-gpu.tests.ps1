<#
.SYNOPSIS
    Builds and visibly pixel-tests the reusable authored RSX program and texture owners.

.DESCRIPTION
    Verifies the locked image, repository inputs, and producer artifacts before creating
    a fresh build directory. SDK work is network-disabled in Docker with read-only source
    and fixture mounts. Runtime uses the repository's verified launcher; the runner then
    requires this build's fresh RPCS3 TTY output, all named assertions, and ten exact
    phase/sample pixel comparisons. All generated files and logs stay below OutputRoot.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$FixtureRoot,
    [Parameter(Mandatory)][string]$OutputRoot,
    [string]$RpcS3Path = $env:HELENGINE_PS3_RPCS3_PATH
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$taskBuildRoot = [IO.Path]::GetFullPath('C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\authored-gpu')
$lockedImageId = 'sha256:63a28f67dacb0a6d80c165645af126c7d6760e6a3b02e03b6e08aa5aeb96c40f'
$lockPath = Join-Path $repoRoot 'toolchain-lock.json'
$fixtureNames = @('fixture.ps3sp', 'vertex.bin', 'pixel.bin')
$launchName = 'ps3_authored_gpu_tests.self'

function Get-Sha256 {
    <# Returns a lowercase SHA-256 digest for one immutable build input or output. #>
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-StrictlyContained {
    <# Resolves a drive-rooted or UNC-rooted path and rejects the parent itself and lexical escapes. #>
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
    $prefix = $fullParent + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be a strict descendant of $fullParent; found $fullPath"
    }
    return $fullPath
}

function Get-RepositoryInputSnapshot {
    <# Hashes the target, its native sources, and recursively included checkout headers. #>
    param([Parameter(Mandatory)][string]$Root)
    $seeds = @(
        'builder.tests/Ps3ShaderGpuFixtureTests.cs',
        'tests/native/authored-gpu.Makefile',
        'tests/native/Ps3AuthoredGpuTests.cpp',
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
            throw "Required authored-GPU repository input is missing: $current"
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
    <# Re-hashes every source input and refuses results from a concurrently edited checkout. #>
    param(
        [Parameter(Mandatory)][Collections.Generic.SortedDictionary[string, string]]$Snapshot,
        [Parameter(Mandatory)][string]$Phase
    )
    foreach ($path in $Snapshot.Keys) {
        $actual = Get-Sha256 -Path $path
        if ($actual -ne $Snapshot[$path]) {
            throw "Repository source changed $Phase; refusing to use mixed inputs: $path"
        }
    }
}

function Get-ExactCppDestructorBody {
    <# Returns one complete C++ destructor body with brace matching that ignores comments and literals. #>
    param(
        [Parameter(Mandatory)][string]$SourceText,
        [Parameter(Mandatory)][string]$OwnerType
    )
    $escapedType = [regex]::Escape($OwnerType)
    $signature = [regex]::Match($SourceText, "(?m)^[ \t]*$escapedType::~$escapedType\s*\(\s*\)\s*\{")
    if (-not $signature.Success) {
        throw "Authored-owner destructor source contract is missing the exact $OwnerType destructor definition."
    }

    $openingBrace = $signature.Index + $signature.Length - 1
    $bodyStart = $openingBrace + 1
    $braceDepth = 0
    $lexicalState = 'code'
    $cursor = $openingBrace
    while ($cursor -lt $SourceText.Length) {
        $character = $SourceText[$cursor]
        $nextCharacter = if ($cursor + 1 -lt $SourceText.Length) { $SourceText[$cursor + 1] } else { [char]0 }
        if ($lexicalState -eq 'code') {
            if ($character -eq [char]34) { $lexicalState = 'string' }
            elseif ($character -eq [char]39) { $lexicalState = 'character' }
            elseif ($character -eq [char]47 -and $nextCharacter -eq [char]47) { $lexicalState = 'line-comment'; ++$cursor }
            elseif ($character -eq [char]47 -and $nextCharacter -eq [char]42) { $lexicalState = 'block-comment'; ++$cursor }
            elseif ($character -eq [char]123) { ++$braceDepth }
            elseif ($character -eq [char]125) {
                --$braceDepth
                if ($braceDepth -eq 0) { return $SourceText.Substring($bodyStart, $cursor - $bodyStart) }
            }
        } elseif ($lexicalState -eq 'string' -or $lexicalState -eq 'character') {
            if ($character -eq [char]92) { ++$cursor }
            elseif (($lexicalState -eq 'string' -and $character -eq [char]34) -or
                ($lexicalState -eq 'character' -and $character -eq [char]39)) { $lexicalState = 'code' }
        } elseif ($lexicalState -eq 'line-comment') {
            if ($character -eq [char]10) { $lexicalState = 'code' }
        } elseif ($lexicalState -eq 'block-comment') {
            if ($character -eq [char]42 -and $nextCharacter -eq [char]47) { $lexicalState = 'code'; ++$cursor }
        }
        ++$cursor
    }
    throw "Authored-owner destructor source contract found an unterminated $OwnerType body."
}

function Assert-OwnerDestructorSafetyContract {
    <# Rejects destructors that inspect their borrowed video or release RSX before explicit drain shutdown. #>
    param([Parameter(Mandatory)][string]$Root)
    $owners = @(
        @{ Type = 'Ps3AuthoredProgram'; Path = 'src/platform/ps3/Ps3AuthoredProgram.cpp' },
        @{ Type = 'Ps3AuthoredTexture'; Path = 'src/platform/ps3/Ps3AuthoredTexture.cpp' }
    )
    foreach ($owner in $owners) {
        $sourceText = Get-Content -LiteralPath (Join-Path $Root $owner.Path) -Raw
        $body = Get-ExactCppDestructorBody -SourceText $sourceText -OwnerType $owner.Type
        if ($body -match '\bVideoValue\b') {
            throw "$($owner.Type) destructor fallback must not inspect borrowed VideoValue; require explicit ShutdownAfterDrain."
        }
        if ($body -match '\bReleaseOwnedResources\s*\(' -or $body -match '\brsxFree\s*\(') {
            throw "$($owner.Type) destructor fallback must retain RSX storage instead of releasing it."
        }
        if ($body -notmatch '\bInitialized\b' -or $body -notmatch 'missing-explicit-ShutdownAfterDrain' -or
            $body -notmatch 'retainedGpuBytes=%lu') {
            throw "$($owner.Type) destructor must report an initialized owner that missed explicit ShutdownAfterDrain with retained GPU bytes."
        }
        Write-Output "OWNER_DESTRUCTOR_CONTRACT name=$($owner.Type) result=PASS"
    }
}

$FixtureRoot = Assert-StrictlyContained -Path $FixtureRoot -Parent $taskBuildRoot -Label 'FixtureRoot'
$OutputRoot = Assert-StrictlyContained -Path $OutputRoot -Parent $taskBuildRoot -Label 'OutputRoot'
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
    if (-not (Test-Path -LiteralPath $fixturePath -PathType Leaf)) {
        throw "Required real producer artifact is missing: $fixturePath"
    }
    if ((Get-Item -LiteralPath $fixturePath).Length -eq 0) {
        throw "Required real producer artifact is empty: $fixturePath"
    }
    $fixtureHashes[$fixtureName] = Get-Sha256 -Path $fixturePath
}
$sourceSnapshot = Get-RepositoryInputSnapshot -Root $repoRoot
Assert-OwnerDestructorSafetyContract -Root $repoRoot

$nativeErrorPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $resolvedImageId = (& docker image inspect --format '{{.Id}}' $lockedImageId 2>&1 | Out-String).Trim()
    $inspectExit = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $nativeErrorPreference
}
if ($inspectExit -ne 0 -or $resolvedImageId -ne $lockedImageId) {
    throw "Docker image ID does not match the task lock: $resolvedImageId"
}
if ((Get-Sha256 -Path $lockPath) -ne $lockHash) {
    throw 'toolchain-lock.json changed during preflight.'
}
foreach ($fixtureName in $fixtureNames) {
    if ((Get-Sha256 -Path (Join-Path $FixtureRoot $fixtureName)) -ne $fixtureHashes[$fixtureName]) {
        throw "Producer fixture changed during preflight: $fixtureName"
    }
}
Assert-RepositorySnapshotUnchanged -Snapshot $sourceSnapshot -Phase 'during preflight'

if (-not (Test-Path -LiteralPath $OutputRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $OutputRoot -ErrorAction Stop | Out-Null
}
$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
$buildId = "authored-gpu-$stamp-$([guid]::NewGuid().ToString('N'))"
if ($buildId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$') {
    throw "Generated unsafe build ID: $buildId"
}
$buildRoot = Join-Path $OutputRoot $buildId
if (Test-Path -LiteralPath $buildRoot) {
    throw "Refusing to reuse a previous authored-GPU output directory: $buildRoot"
}
New-Item -ItemType Directory -Path $buildRoot -ErrorAction Stop | Out-Null
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
        throw "Staged artifact hash differs from its immutable producer input: $fixtureName"
    }
}

$buildLogPath = Join-Path $buildRoot 'build.log'
$dockerArguments = @(
    'run', '--rm', '--network', 'none',
    '--mount', "type=bind,source=$repoRoot,target=/src,readonly",
    '--mount', "type=bind,source=$buildRoot,target=/out",
    '--mount', "type=bind,source=$contentRoot,target=/app_home/USRDIR/content,readonly",
    $lockedImageId,
    'make', '-f', '/src/tests/native/authored-gpu.Makefile', 'SRC_ROOT=/src', 'BUILD_ROOT=/out'
)
$nativeErrorPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & docker @dockerArguments *> $buildLogPath
    $buildExit = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $nativeErrorPreference
}
$buildLog = Get-Content -LiteralPath $buildLogPath -Raw
if ($buildLog.Length -gt 7000) { $buildLog = $buildLog.Substring($buildLog.Length - 7000) }
Write-Output $buildLog
if ($buildExit -ne 0) {
    throw "Locked-image authored-GPU PPU build failed with exit code $buildExit. Full log: $buildLogPath"
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

$artifactNames = @('ps3_authored_gpu_tests.elf', $launchName, 'ps3_authored_gpu_tests.fake.self')
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
    TestKind = 'Ps3AuthoredGpuPixelFixture'
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

if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable('HELENGINE_PS3_RPCS3_PATH')) -and [string]::IsNullOrWhiteSpace($RpcS3Path)) {
    throw 'RPCS3 path is required for visible pixel acceptance.'
}
$runningEmulators = @(Get-Process -Name rpcs3 -ErrorAction SilentlyContinue)
if ($runningEmulators.Count -ne 0) {
    $runningIds = ($runningEmulators | ForEach-Object { $_.Id }) -join ', '
    throw "RPCS3 is already running (PID $runningIds); refusing a concurrent GPU test launch."
}

$emulatorLogPath = Join-Path (Split-Path -Parent $RpcS3Path) 'log\TTY.log'
$ttyHashBefore = if (Test-Path -LiteralPath $emulatorLogPath -PathType Leaf) { Get-Sha256 -Path $emulatorLogPath } else { '' }
$launchStartedUtc = [DateTime]::UtcNow
$launcherPath = Join-Path $repoRoot 'scripts\launch_in_emulator.ps1'
$launcherLogPath = Join-Path $buildRoot 'launcher.log'
try {
    & $launcherPath -ArtifactPath (Join-Path $buildRoot $launchName) -OutputRoot $OutputRoot `
        -RpcS3Path $RpcS3Path -Visible *> $launcherLogPath
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
if ($runRecord -notmatch [regex]::Escape("BuildId=$buildId") -or $runRecord -notmatch [regex]::Escape("Visible=True")) {
    throw "Launcher run record does not identify this fresh visible build: $runRoot"
}
$ttyLogPath = Join-Path $runRoot 'TTY.log'
if (-not (Test-Path -LiteralPath $ttyLogPath -PathType Leaf)) {
    throw "Existing launcher did not capture this GPU run's TTY.log: $ttyLogPath"
}
if (-not (Test-Path -LiteralPath $emulatorLogPath -PathType Leaf)) {
    throw "RPCS3 did not leave its per-run source TTY log: $emulatorLogPath"
}
$sourceTtyInfo = Get-Item -LiteralPath $emulatorLogPath
if ($sourceTtyInfo.LastWriteTimeUtc -lt $launchStartedUtc.AddSeconds(-2)) {
    throw "RPCS3 TTY log was not updated by this run; refusing potentially stale pixel evidence: $emulatorLogPath"
}
$freshTtyHash = Get-Sha256 -Path $ttyLogPath
if ($freshTtyHash -ne (Get-Sha256 -Path $emulatorLogPath)) {
    throw 'Captured TTY log differs from the current RPCS3 run log.'
}
if ($ttyHashBefore -and $freshTtyHash -eq $ttyHashBefore) {
    throw 'RPCS3 TTY log is byte-identical to its pre-launch state; refusing stale evidence.'
}

$ttyText = Get-Content -LiteralPath $ttyLogPath -Raw
$summaryMatches = [regex]::Matches($ttyText, '(?m)^PS3_AUTHORED_GPU_TESTS passed=(\d+) failed=0\s*$')
if ($summaryMatches.Count -ne 1) {
    throw "Fresh GPU TTY log must contain exactly one successful terminal summary: $ttyLogPath"
}
$passMatches = [regex]::Matches($ttyText, '(?m)^PASS [A-Za-z0-9][A-Za-z0-9_-]*\s*$')
$failMatches = [regex]::Matches($ttyText, '(?m)^FAIL\s')
$reportedPasses = [int]$summaryMatches[0].Groups[1].Value
if ($reportedPasses -lt 30 -or $reportedPasses -ne $passMatches.Count -or $failMatches.Count -ne 0) {
    throw "GPU named assertions disagree with the terminal summary (reported=$reportedPasses, PASS-lines=$($passMatches.Count), FAIL-lines=$($failMatches.Count)); see $ttyLogPath"
}
$identityMatches = [regex]::Matches($ttyText, '(?m)^PS3_AUTHORED_GPU_ARTIFACT asset=authored-gpu-fixture vertex=gpu\.vs pixel=gpu\.ps variant=default\s*$')
if ($identityMatches.Count -ne 1) {
    throw 'Fresh GPU output did not identify the exact producer fixture artifact.'
}
$mappingMatches = [regex]::Matches($ttyText, '(?m)^PS3_AUTHORED_GPU_MAPPING positionSlot=(\d+) uvSlot=(\d+) textureUnit=(\d+)\s*$')
if ($mappingMatches.Count -ne 1 -or [int]$mappingMatches[0].Groups[1].Value -gt 15 -or [int]$mappingMatches[0].Groups[2].Value -gt 15 -or [int]$mappingMatches[0].Groups[3].Value -gt 15) {
    throw 'GPU output is missing valid SDK-mapped attribute slots or Albedo unit.'
}

$expectedPixels = @(
    @{ Phase = 'A'; Sample = '0.500,0.375'; Rgb = '255,0,0' },
    @{ Phase = 'A'; Sample = '0.750,0.375'; Rgb = '0,255,0' },
    @{ Phase = 'A'; Sample = '0.500,0.750'; Rgb = '0,0,255' },
    @{ Phase = 'A'; Sample = '0.750,0.750'; Rgb = '255,255,0' },
    @{ Phase = 'A'; Sample = '0.100,0.100'; Rgb = '100,149,237' },
    @{ Phase = 'B'; Sample = '0.500,0.375'; Rgb = '128,0,0' },
    @{ Phase = 'B'; Sample = '0.750,0.375'; Rgb = '0,128,0' },
    @{ Phase = 'B'; Sample = '0.500,0.750'; Rgb = '0,0,255' },
    @{ Phase = 'B'; Sample = '0.750,0.750'; Rgb = '128,128,0' },
    @{ Phase = 'B'; Sample = '0.100,0.100'; Rgb = '100,149,237' }
)
$pixelMatches = [regex]::Matches($ttyText,
    '(?m)^PS3_AUTHORED_GPU_PIXEL phase=([AB]) sample=\((\d\.\d{3}),(\d\.\d{3})\) pixel=\((\d+),(\d+)\) expected=\((\d+),(\d+),(\d+)\) actual=\((\d+),(\d+),(\d+)\)\s*$')
if ($pixelMatches.Count -ne $expectedPixels.Count) {
    throw "Expected exactly $($expectedPixels.Count) concrete phase/sample pixel records; found $($pixelMatches.Count)."
}
$seenPixels = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($pixelMatch in $pixelMatches) {
    $sample = "$($pixelMatch.Groups[2].Value),$($pixelMatch.Groups[3].Value)"
    $key = "$($pixelMatch.Groups[1].Value):$sample"
    if (-not $seenPixels.Add($key)) { throw "GPU pixel evidence repeats sample $key." }
    $expected = @($expectedPixels | Where-Object { $_.Phase -eq $pixelMatch.Groups[1].Value -and $_.Sample -eq $sample })
    if ($expected.Count -ne 1) { throw "GPU pixel evidence contains an unplanned phase/sample: $key" }
    $expectedRgb = $expected[0].Rgb.Split(',') | ForEach-Object { [int]$_ }
    $reportedRgb = @([int]$pixelMatch.Groups[6].Value, [int]$pixelMatch.Groups[7].Value, [int]$pixelMatch.Groups[8].Value)
    $actualRgb = @([int]$pixelMatch.Groups[9].Value, [int]$pixelMatch.Groups[10].Value, [int]$pixelMatch.Groups[11].Value)
    for ($channel = 0; $channel -lt 3; ++$channel) {
        if ($reportedRgb[$channel] -ne $expectedRgb[$channel] -or [Math]::Abs($actualRgb[$channel] - $expectedRgb[$channel]) -gt 2) {
            throw "Pixel $key differs from the expected RGB tolerance: expected=$($expected[0].Rgb), actual=$($actualRgb -join ',')"
        }
    }
}
if ($seenPixels.Count -ne $expectedPixels.Count) {
    throw "GPU output omitted a planned phase/sample pixel; found $($seenPixels.Count) of $($expectedPixels.Count)."
}
if (@(Get-Process -Name rpcs3 -ErrorAction SilentlyContinue).Count -ne 0) {
    throw 'RPCS3 remains running after the visible test; coordinate its release before another emulator gate.'
}

Write-Output "PS3_AUTHORED_GPU_TESTS passed=$reportedPasses failed=0"
Write-Output "GPUArtifact=authored-gpu-fixture/gpu.vs/gpu.ps/default"
Write-Output "GPUTextureUnit=$($mappingMatches[0].Groups[3].Value)"
Write-Output "GPUVisibleRun=$runRoot"
Write-Output "GPUFreshTtySha256=$freshTtyHash"
