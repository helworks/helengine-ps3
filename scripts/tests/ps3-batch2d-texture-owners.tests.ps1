[CmdletBinding()]
param(
    [switch]$Red,
    [switch]$Visible,
    [string]$RpcS3Path = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$buildParent = 'C:\dev\helworks\builds\helengine-ps3\2d-batch-20260924\native-task1-texture-owners'
$standardRoot = 'C:\dev\helworks\builds\helengine-ps3\2d-batch-20260924\standard\output\core-2c7f18d1a11343b3a21162969fa07513'
$generatedRoot = 'C:\dev\helworks\builds\helengine-ps3\2d-batch-20260924\standard\cache\v2\d87d222cd166d9049967e97c50c8e301\b\ps3\release\release\generated-core'
$lockedImageId = 'sha256:63a28f67dacb0a6d80c165645af126c7d6760e6a3b02e03b6e08aa5aeb96c40f'
$expectedGeneratedHash = 'bc7e043f374bb2f21de9dc57d019f6fb43e8726121e2fa919b1ab8be5efa04c3'
$expectedGeneratedCount = 877

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Test-FreshOwnerTtyEvidence {
    param(
        [Parameter(Mandatory)][string]$CapturedHash,
        [Parameter(Mandatory)][string]$CurrentHash,
        [Parameter(Mandatory)][datetime]$CurrentTtyLastWriteUtc,
        [Parameter(Mandatory)][datetime]$LaunchStartedUtc,
        [Parameter(Mandatory)][string]$BuildId,
        [Parameter(Mandatory)][string]$RunRecord
    )

    if ([string]::IsNullOrWhiteSpace($BuildId) -or $CapturedHash -ne $CurrentHash) { return $false }
    if ($CurrentTtyLastWriteUtc -lt $LaunchStartedUtc.AddSeconds(-2)) { return $false }
    $buildRecordPattern = '(?m)^BuildId=' + [regex]::Escape($BuildId) + '\s*$'
    return $RunRecord -match $buildRecordPattern -and $RunRecord -match '(?m)^Visible=True\s*$'
}

function Get-GeneratedAggregate([string]$Root) {
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $records = foreach ($file in (Get-ChildItem -LiteralPath $rootPath -File -Recurse | Sort-Object FullName)) {
        $relative = $file.FullName.Substring($rootPath.Length).TrimStart('\', '/').Replace('\', '/')
        "$relative=$(Get-Sha256 $file.FullName)"
    }
    $payload = [string]::Join("`n", [string[]]$records)
    $bytes = [Text.Encoding]::UTF8.GetBytes($payload)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $aggregate = [BitConverter]::ToString($algorithm.ComputeHash($bytes)).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
    return $aggregate, @($records).Count
}

function Assert-StandardInputs {
    $manifestPath = Join-Path $standardRoot 'build-manifest.json'
    $coreObject = Join-Path $standardRoot 'obj\generated\helengine_core_unity.o'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf) -or -not (Test-Path -LiteralPath $coreObject -PathType Leaf)) {
        throw "Completed standard build inputs are absent under $standardRoot"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ([IO.Path]::GetFullPath([string]$manifest.GeneratedCoreRoot) -ne [IO.Path]::GetFullPath($generatedRoot) -or
        [string]$manifest.GeneratedCoreSha256 -ne $expectedGeneratedHash -or
        [int]$manifest.GeneratedCoreFileCount -ne $expectedGeneratedCount) {
        throw 'Standard manifest does not identify the approved generated Core snapshot.'
    }
    $aggregate = Get-GeneratedAggregate $generatedRoot
    if ($aggregate[0] -ne $expectedGeneratedHash -or [int]$aggregate[1] -ne $expectedGeneratedCount) {
        throw "Generated Core fingerprint mismatch: sha256=$($aggregate[0]) files=$($aggregate[1])"
    }
    return [pscustomobject]@{ Manifest = $manifestPath; CoreObject = $coreObject; GeneratedHash = $aggregate[0]; GeneratedCount = [int]$aggregate[1] }
}

if ($Red -and $Visible) { throw 'RED syntax-check mode cannot also launch an emulator.' }
$lockPath = Join-Path $repoRoot 'toolchain-lock.json'
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
if ([string]$lock.ResolvedImage.ImageId -ne $lockedImageId) { throw 'Pinned SDK image differs from toolchain-lock.json.' }
$imageId = (& docker image inspect --format '{{.Id}}' $lockedImageId 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $imageId -ne $lockedImageId) { throw "Pinned Docker image unavailable or mismatched: $imageId" }

$standard = $null
if (-not $Red) { $standard = Assert-StandardInputs }
if (-not (Test-Path -LiteralPath $buildParent -PathType Container)) { New-Item -ItemType Directory -Path $buildParent -ErrorAction Stop | Out-Null }
$buildId = 'texture-owners-' + (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$buildRoot = Join-Path $buildParent $buildId
New-Item -ItemType Directory -Path $buildRoot -ErrorAction Stop | Out-Null
$temporaryRoot = Join-Path $buildRoot 'temp'
New-Item -ItemType Directory -Path $temporaryRoot -ErrorAction Stop | Out-Null
$oldTemporaryEnvironment = @{}
foreach ($name in @('TEMP', 'TMP', 'TMPDIR', 'DockerTMPDIR')) {
    $oldTemporaryEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

try {
foreach ($name in @('TEMP', 'TMP', 'TMPDIR', 'DockerTMPDIR')) {
    [Environment]::SetEnvironmentVariable($name, $temporaryRoot, 'Process')
}
$logPath = Join-Path $buildRoot 'build.log'
$sourceRelativePaths = @(
    'src/platform/ps3/Ps3RuntimeTexture.hpp',
    'src/platform/ps3/Ps3RuntimeTexture.cpp',
    'src/platform/ps3/Ps3Batch2DResources.hpp',
    'src/platform/ps3/Ps3Batch2DResources.cpp',
    'src/platform/ps3/Ps3Batch2DFlushGuard.hpp',
    'src/platform/ps3/Ps3AuthoredTexture.hpp',
    'src/platform/ps3/Ps3AuthoredTexture.cpp',
    'src/platform/ps3/Ps3Video.hpp',
    'src/platform/ps3/Ps3Video.cpp',
    'src/platform/ps3/Ps3ContentStreamSource.hpp',
    'src/platform/ps3/Ps3ContentStreamSource.cpp',
    'src/platform/ps3/Ps3ArtifactReadUtilities.hpp',
    'tests/ps3_batch2d_texture_owners_tests.cpp',
    'tests/Makefile.batch2d-texture-owners',
    'tests/native/Ps3Batch2DOwnerStream.hpp',
    'tests/native/Ps3Batch2DOwnerContentSource.hpp',
    'tests/native/Ps3Batch2DOwnerReleaseManager.hpp',
    'scripts/tests/ps3-batch2d-texture-owners.tests.ps1',
    'toolchain-lock.json'
)
$sourceHashes = [ordered]@{}
foreach ($relativePath in $sourceRelativePaths) {
    $sourcePath = Join-Path $repoRoot ($relativePath.Replace('/', '\'))
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "Required immutable build input is absent: $relativePath" }
    $sourceHashes[$relativePath] = Get-Sha256 $sourcePath
}
$target = if ($Red) { 'red' } else { 'all' }
$dockerArguments = @(
    'run', '--rm', '--pull=never', '--network', 'none',
    '--mount', "type=bind,source=$repoRoot,target=/src,readonly",
    '--mount', "type=bind,source=$buildRoot,target=/out",
    '--mount', "type=bind,source=$generatedRoot,target=/generated,readonly",
    '--env', 'TMPDIR=/out/temp', '--env', 'TMP=/out/temp', '--env', 'TEMP=/out/temp', '--env', 'DockerTMPDIR=/out/temp'
)
if (-not $Red) { $dockerArguments += @('--mount', "type=bind,source=$standardRoot,target=/standard,readonly") }
$dockerArguments += @($lockedImageId, 'make', '-f', '/src/tests/Makefile.batch2d-texture-owners', $target, 'SRC_ROOT=/src', 'BUILD_ROOT=/out')
$nativeErrorPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & docker @dockerArguments *> $logPath
    $buildExit = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $nativeErrorPreference
}
$log = Get-Content -LiteralPath $logPath -Raw
if ($log.Length -gt 5000) { $log = $log.Substring($log.Length - 5000) }
Write-Output $log
Write-Output "BuildRoot=$buildRoot"
Write-Output "BuildExit=$buildExit"
if ($Red) {
    if ($buildExit -eq 0 -or $log -notmatch 'Ps3Batch2DResources\.hpp|Ps3RuntimeTexture\.hpp') {
        throw 'Expected RED compile failure for the not-yet-implemented renderer-owned wrapper/resource API did not occur.'
    }
    Write-Output 'RED_EXPECTED_MISSING_OWNER_API=PASS'
    return
}
if ($buildExit -ne 0) { throw "Pinned Docker PPU build failed with exit $buildExit; full log: $logPath" }
$standardAfterBuild = Assert-StandardInputs
if ($standardAfterBuild.GeneratedHash -ne $standard.GeneratedHash -or
    (Get-Sha256 $standardAfterBuild.CoreObject) -ne (Get-Sha256 $standard.CoreObject)) {
    throw 'Approved generated Core or standard unity object changed during native compilation.'
}
$standard = $standardAfterBuild
$artifacts = @(
    foreach ($artifactName in @('ps3_batch2d_texture_owners_tests.elf', 'ps3_batch2d_texture_owners_tests.self', 'ps3_batch2d_texture_owners_tests.fake.self')) {
        $artifactPath = Join-Path $buildRoot $artifactName
        if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf) -or (Get-Item -LiteralPath $artifactPath).Length -eq 0) {
            throw "Successful PPU build omitted nonempty artifact $artifactName"
        }
        [pscustomobject]@{ Path = $artifactName; Sha256 = Get-Sha256 $artifactPath; Bytes = (Get-Item -LiteralPath $artifactPath).Length }
    }
)
foreach ($relativePath in $sourceRelativePaths) {
    $sourcePath = Join-Path $repoRoot ($relativePath.Replace('/', '\'))
    if ((Get-Sha256 $sourcePath) -ne $sourceHashes[$relativePath]) { throw "Source changed during PPU build: $relativePath" }
}
$lockHash = Get-Sha256 $lockPath
$manifest = [ordered]@{
    SchemaVersion = 1
    BuildId = $buildId
    TestKind = 'Ps3Batch2DTextureOwners'
    ToolchainLockSha256 = $lockHash
    ToolchainImageId = $lockedImageId
    GeneratedCoreSha256 = $standard.GeneratedHash
    GeneratedCoreFileCount = $standard.GeneratedCount
    StandardCoreObjectSha256 = Get-Sha256 $standard.CoreObject
    LaunchArtifact = 'ps3_batch2d_texture_owners_tests.self'
    SourceHashes = $sourceHashes
    Artifacts = $artifacts
}
$manifestPath = Join-Path $buildRoot 'build-manifest.json'
$temporaryManifestPath = Join-Path $buildRoot 'build-manifest.json.tmp'
[IO.File]::WriteAllText($temporaryManifestPath, ($manifest | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
[IO.File]::Move($temporaryManifestPath, $manifestPath)
Write-Output "GeneratedCoreSha256=$($standard.GeneratedHash)"
Write-Output "GeneratedCoreFileCount=$($standard.GeneratedCount)"
Write-Output "CoreObjectSha256=$(Get-Sha256 $standard.CoreObject)"
Write-Output "BuildManifest=$manifestPath"

if ($Visible) {
    if ([string]::IsNullOrWhiteSpace($RpcS3Path)) { $RpcS3Path = [Environment]::GetEnvironmentVariable('HELENGINE_PS3_RPCS3_PATH') }
    if ([string]::IsNullOrWhiteSpace($RpcS3Path)) { throw 'Visible mode requires the parent-coordinated RPCS3 executable path.' }
    if (@(Get-Process -Name rpcs3 -ErrorAction SilentlyContinue).Count -ne 0) { throw 'RPCS3 is already running; refusing a concurrent launch.' }
    $emulatorLogPath = Join-Path (Split-Path -Parent $RpcS3Path) 'log\TTY.log'
    $launchStartedUtc = [DateTime]::UtcNow
    $launcher = Join-Path $repoRoot 'scripts\launch_in_emulator.ps1'
    & $launcher -ArtifactPath (Join-Path $buildRoot 'ps3_batch2d_texture_owners_tests.self') -OutputRoot $buildParent -RpcS3Path $RpcS3Path -Visible
    if ($LASTEXITCODE -ne 0) { throw "Visible launch script returned $LASTEXITCODE" }
    $runRoots = @(Get-ChildItem -LiteralPath (Join-Path $buildRoot 'emulator-runs') -Directory)
    if ($runRoots.Count -ne 1) { throw "Fresh owner fixture must contain exactly one emulator run; found $($runRoots.Count)" }
    $runRoot = $runRoots[0].FullName
    $runRecord = Get-Content -LiteralPath (Join-Path $runRoot 'emulator-command.txt') -Raw -Encoding UTF8
    $ttyPath = Join-Path $runRoot 'TTY.log'
    if (-not (Test-Path -LiteralPath $ttyPath -PathType Leaf) -or -not (Test-Path -LiteralPath $emulatorLogPath -PathType Leaf)) {
        throw 'Fresh RPCS3 TTY output is absent from the run directory or emulator log root.'
    }
    $ttyHash = Get-Sha256 $ttyPath
    $currentTty = Get-Item -LiteralPath $emulatorLogPath
    if (-not (Test-FreshOwnerTtyEvidence -CapturedHash $ttyHash -CurrentHash (Get-Sha256 $emulatorLogPath) `
        -CurrentTtyLastWriteUtc $currentTty.LastWriteTimeUtc -LaunchStartedUtc $launchStartedUtc `
        -BuildId $buildId -RunRecord $runRecord)) {
        throw 'Captured TTY is stale or does not match the current run timestamp, hash, and unique visible build record.'
    }
    $ttyText = Get-Content -LiteralPath $ttyPath -Raw
    $summaries = [regex]::Matches($ttyText, '(?m)^PS3_BATCH2D_TEXTURE_OWNERS_TESTS passed=(\d+) failed=0\s*$')
    $passLines = [regex]::Matches($ttyText, '(?m)^PASS [A-Za-z0-9][A-Za-z0-9_-]*\s*$')
    $failLines = [regex]::Matches($ttyText, '(?m)^FAIL\s')
    $fixtureSource = Get-Content -LiteralPath (Join-Path $repoRoot 'tests/ps3_batch2d_texture_owners_tests.cpp') -Raw
    $expectedAssertions = [regex]::Matches($fixtureSource, '\bExpect\s*\(').Count - 1
    if ($summaries.Count -ne 1 -or [int]$summaries[0].Groups[1].Value -ne $expectedAssertions -or
        $passLines.Count -ne $expectedAssertions -or $failLines.Count -ne 0) {
        throw "Fresh TTY assertions disagree with fixture summary (summaryCount=$($summaries.Count), PASS=$($passLines.Count), FAIL=$($failLines.Count)); see $ttyPath"
    }
    if (@(Get-Process -Name rpcs3 -ErrorAction SilentlyContinue).Count -ne 0) { throw 'RPCS3 remains running after the fixture launch.' }
    Write-Output "FixtureTty=$ttyPath"
    Write-Output "FixtureTtySha256=$ttyHash"
    Write-Output "FixtureAssertions=$expectedAssertions"
}
} finally {
    foreach ($name in @('TEMP', 'TMP', 'TMPDIR', 'DockerTMPDIR')) {
        [Environment]::SetEnvironmentVariable($name, $oldTemporaryEnvironment[$name], 'Process')
    }
}
