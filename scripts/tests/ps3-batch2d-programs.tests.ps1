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
$buildParent = 'C:\dev\helworks\builds\helengine-ps3\2d-batch-20260924\native-task1-programs'
$standardRoot = 'C:\dev\helworks\builds\helengine-ps3\2d-batch-20260924\standard\output\core-2c7f18d1a11343b3a21162969fa07513'
$generatedRoot = 'C:\dev\helworks\builds\helengine-ps3\2d-batch-20260924\standard\cache\v2\d87d222cd166d9049967e97c50c8e301\b\ps3\release\release\generated-core'
$contentRoot = Join-Path $standardRoot 'USRDIR\content'
$lockedImageId = 'sha256:63a28f67dacb0a6d80c165645af126c7d6760e6a3b02e03b6e08aa5aeb96c40f'
$expectedGeneratedHash = 'bc7e043f374bb2f21de9dc57d019f6fb43e8726121e2fa919b1ab8be5efa04c3'
$expectedGeneratedCount = 877

function Get-Sha256([string]$Path) {
    <# Computes a lowercase SHA-256 for one explicit immutable build input or output. #>
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-GeneratedAggregate([string]$Root) {
    <# Reproduces the standard build's sorted relative-path plus per-file generated-Core fingerprint. #>
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $records = foreach ($file in (Get-ChildItem -LiteralPath $rootPath -File -Recurse | Sort-Object FullName)) {
        $relative = $file.FullName.Substring($rootPath.Length).TrimStart('\', '/').Replace('\', '/')
        "$relative=$(Get-Sha256 $file.FullName)"
    }
    $payload = [string]::Join("`n", [string[]]$records)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $aggregate = [BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($payload))).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
    return $aggregate, @($records).Count
}

function Test-FreshProgramTtyEvidence {
    <# Requires matching captured/current TTY bytes, a post-launch timestamp, and the exact visible build record. #>
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
    $buildPattern = '(?m)^BuildId=' + [regex]::Escape($BuildId) + '\s*$'
    return $RunRecord -match $buildPattern -and $RunRecord -match '(?m)^Visible=True\s*$'
}

$lock = Get-Content -LiteralPath (Join-Path $repoRoot 'toolchain-lock.json') -Raw | ConvertFrom-Json
if ([string]$lock.ResolvedImage.ImageId -ne $lockedImageId) { throw 'Pinned SDK image differs from toolchain-lock.json.' }
$imageId = (& docker image inspect --format '{{.Id}}' $lockedImageId 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $imageId -ne $lockedImageId) { throw "Pinned Docker image unavailable or mismatched: $imageId" }
if (-not $Red) {
    $generatedFingerprint = Get-GeneratedAggregate $generatedRoot
    if ($generatedFingerprint[0] -ne $expectedGeneratedHash -or [int]$generatedFingerprint[1] -ne $expectedGeneratedCount) {
        throw "Approved generated Core fingerprint mismatch: sha256=$($generatedFingerprint[0]) files=$($generatedFingerprint[1])"
    }
    foreach ($path in @(
        (Join-Path $contentRoot 'cooked\shaders\842895fde6e1db0816b78c52d176284de1fbb2d40dc819196b6ba7db7d09558a.ps3sp'),
        (Join-Path $contentRoot 'cooked\shaders\9a0867e0cecd7e04a48ecb452b32db7a42deecaab88912cbcc71427cf4b3233d.ps3sp'),
        (Join-Path $standardRoot 'obj\generated\helengine_core_unity.o')
    )) { if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Approved standard input is absent: $path" } }
}
if (-not (Test-Path -LiteralPath $buildParent -PathType Container)) { New-Item -ItemType Directory -Path $buildParent -ErrorAction Stop | Out-Null }
$buildRoot = Join-Path $buildParent ('programs-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $buildRoot -ErrorAction Stop | Out-Null
$temporaryRoot = Join-Path $buildRoot 'temp'
New-Item -ItemType Directory -Path $temporaryRoot -ErrorAction Stop | Out-Null
$oldTemporaryEnvironment = @{}
foreach ($name in @('TEMP', 'TMP', 'TMPDIR', 'DockerTMPDIR')) {
    $oldTemporaryEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$inputPaths = [ordered]@{}
$sourceRelativePaths = @(
    'src/platform/ps3/Ps3Batch2DPrograms.hpp',
    'src/platform/ps3/Ps3Batch2DPrograms.cpp',
    'src/platform/ps3/Ps3Batch2DUniformProvider.hpp',
    'src/platform/ps3/Ps3Batch2DUniformProvider.cpp',
    'src/platform/ps3/Ps3AuthoredProgram.hpp',
    'src/platform/ps3/Ps3AuthoredProgram.cpp',
    'src/platform/ps3/Ps3ShaderArtifactReader.hpp',
    'src/platform/ps3/Ps3ShaderArtifactReader.cpp',
    'src/platform/ps3/Ps3RsxProgramValidator.hpp',
    'src/platform/ps3/Ps3RsxProgramValidator.cpp',
    'src/platform/ps3/Ps3ArtifactReadUtilities.hpp',
    'src/platform/ps3/Ps3CookedShaderProgram.hpp',
    'src/platform/ps3/Ps3CookedShaderStage.hpp',
    'src/platform/ps3/Ps3CookedShaderUniform.hpp',
    'src/platform/ps3/Ps3CookedShaderBuffer.hpp',
    'src/platform/ps3/Ps3CookedShaderAttribute.hpp',
    'src/platform/ps3/Ps3CookedShaderSampler.hpp',
    'src/platform/ps3/Ps3ContentStreamSource.hpp',
    'src/platform/ps3/Ps3ContentStreamSource.cpp',
    'src/platform/ps3/Ps3Video.hpp',
    'src/platform/ps3/Ps3Video.cpp',
    'tests/ps3_batch2d_programs_tests.cpp',
    'tests/Makefile.batch2d-programs',
    'scripts/tests/ps3-batch2d-programs.tests.ps1',
    'toolchain-lock.json'
)
foreach ($relativePath in $sourceRelativePaths) {
    $sourcePath = Join-Path $repoRoot ($relativePath.Replace('/', '\'))
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "Required source input is absent: $relativePath" }
    $inputPaths[$relativePath] = $sourcePath
}
$inputPaths['generated-core/IContentStreamSource.hpp'] = Join-Path $generatedRoot 'IContentStreamSource.hpp'
$inputPaths['generated-core/runtime/native_exceptions.hpp'] = Join-Path $generatedRoot 'runtime\native_exceptions.hpp'
$inputPaths['generated-core/system/io/stream.hpp'] = Join-Path $generatedRoot 'system\io\stream.hpp'
$inputPaths['standard/obj/generated/helengine_core_unity.o'] = Join-Path $standardRoot 'obj\generated\helengine_core_unity.o'
$inputPaths['standard/content/cooked/shaders/842895fde6e1db0816b78c52d176284de1fbb2d40dc819196b6ba7db7d09558a.ps3sp'] = Join-Path $contentRoot 'cooked\shaders\842895fde6e1db0816b78c52d176284de1fbb2d40dc819196b6ba7db7d09558a.ps3sp'
$inputPaths['standard/content/cooked/shaders/9a0867e0cecd7e04a48ecb452b32db7a42deecaab88912cbcc71427cf4b3233d.ps3sp'] = Join-Path $contentRoot 'cooked\shaders\9a0867e0cecd7e04a48ecb452b32db7a42deecaab88912cbcc71427cf4b3233d.ps3sp'
$sourceHashes = [ordered]@{}
foreach ($label in $inputPaths.Keys) {
    if (-not (Test-Path -LiteralPath $inputPaths[$label] -PathType Leaf)) { throw "Required immutable build input is absent: $label" }
    $sourceHashes[$label] = Get-Sha256 $inputPaths[$label]
}
$generatedFingerprint = Get-GeneratedAggregate $generatedRoot
$sourceHashes['generated-core/aggregate-sha256'] = $generatedFingerprint[0]
$generatedFileCount = [int]$generatedFingerprint[1]
$logPath = Join-Path $buildRoot 'build.log'
$target = if ($Red) { 'red' } else { 'all' }
$dockerArguments = @(
    'run', '--rm', '--pull=never', '--network', 'none',
    '--mount', "type=bind,source=$repoRoot,target=/src,readonly",
    '--mount', "type=bind,source=$buildRoot,target=/out",
    '--mount', "type=bind,source=$generatedRoot,target=/generated,readonly",
    '--env', 'TMPDIR=/out/temp', '--env', 'TMP=/out/temp', '--env', 'TEMP=/out/temp', '--env', 'DockerTMPDIR=/out/temp'
)
if (-not $Red) {
    $dockerArguments += @(
        '--mount', "type=bind,source=$standardRoot,target=/standard,readonly",
        '--mount', "type=bind,source=$contentRoot,target=/app_home/USRDIR/content,readonly"
    )
}
$dockerArguments += @($lockedImageId, 'make', '-f', '/src/tests/Makefile.batch2d-programs', $target, 'SRC_ROOT=/src', 'BUILD_ROOT=/out')
foreach ($name in @('TEMP', 'TMP', 'TMPDIR', 'DockerTMPDIR')) {
    [Environment]::SetEnvironmentVariable($name, $temporaryRoot, 'Process')
}
$priorErrorPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & docker @dockerArguments *> $logPath
    $buildExit = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $priorErrorPreference
    foreach ($name in @('TEMP', 'TMP', 'TMPDIR', 'DockerTMPDIR')) {
        [Environment]::SetEnvironmentVariable($name, $oldTemporaryEnvironment[$name], 'Process')
    }
}
$log = Get-Content -LiteralPath $logPath -Raw
if ($log.Length -gt 5000) { $log = $log.Substring($log.Length - 5000) }
Write-Output $log
Write-Output "BuildRoot=$buildRoot"
Write-Output "BuildExit=$buildExit"
if ($Red) {
    if ($buildExit -ne 0) { throw "Focused test translation unit failed to compile; full log: $logPath" }
    Write-Output 'PROGRAM_FIXTURE_SYNTAX=PASS'
    return
}
if ($buildExit -ne 0) { throw "Pinned Docker PPU build failed; full log: $logPath" }
$postGeneratedFingerprint = Get-GeneratedAggregate $generatedRoot
if (($postGeneratedFingerprint[0] -ne $sourceHashes['generated-core/aggregate-sha256']) -or
    ([int]$postGeneratedFingerprint[1] -ne $generatedFileCount)) {
    throw 'Generated Core changed during the PPU build.'
}
foreach ($label in $inputPaths.Keys) {
    if ((Get-Sha256 $inputPaths[$label]) -ne $sourceHashes[$label]) { throw "Build input changed during the PPU build: $label" }
}
$launchName = 'ps3_batch2d_programs_tests.self'
$shaderStage = Join-Path $buildRoot 'USRDIR\content\cooked\shaders'
New-Item -ItemType Directory -Path $shaderStage -Force -ErrorAction Stop | Out-Null
$shaderFiles = @(
    '842895fde6e1db0816b78c52d176284de1fbb2d40dc819196b6ba7db7d09558a.ps3sp',
    '9a0867e0cecd7e04a48ecb452b32db7a42deecaab88912cbcc71427cf4b3233d.ps3sp'
)
foreach ($shaderFile in $shaderFiles) {
    Copy-Item -LiteralPath (Join-Path $contentRoot "cooked\shaders\$shaderFile") -Destination (Join-Path $shaderStage $shaderFile)
}
$artifactRecords = foreach ($artifactName in @($launchName, 'ps3_batch2d_programs_tests.fake.self')) {
    $artifactPath = Join-Path $buildRoot $artifactName
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf) -or (Get-Item -LiteralPath $artifactPath).Length -eq 0) {
        throw "Successful PPU build omitted nonempty artifact $artifactName"
    }
    [ordered]@{
        Path = $artifactName
        Sha256 = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
        Bytes = (Get-Item -LiteralPath $artifactPath).Length
    }
}
$lockHash = (Get-FileHash -LiteralPath (Join-Path $repoRoot 'toolchain-lock.json') -Algorithm SHA256).Hash.ToLowerInvariant()
$buildManifest = [ordered]@{
    SchemaVersion = 1
    BuildId = (Split-Path -Leaf $buildRoot)
    TestKind = 'Ps3Batch2DPrograms'
    ToolchainLockSha256 = $lockHash
    ToolchainImageId = $lockedImageId
    GeneratedCoreSha256 = $sourceHashes['generated-core/aggregate-sha256']
    GeneratedCoreFileCount = $generatedFileCount
    StandardCoreObjectSha256 = $sourceHashes['standard/obj/generated/helengine_core_unity.o']
    SourceHashes = $sourceHashes
    LaunchArtifact = $launchName
    Artifacts = @($artifactRecords)
}
$manifestPath = Join-Path $buildRoot 'build-manifest.json'
[IO.File]::WriteAllText($manifestPath, ($buildManifest | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
Write-Output 'PINNED_DOCKER_BUILD=PASS'
Write-Output "BuildManifest=$manifestPath"
if ($Visible) {
    if ([string]::IsNullOrWhiteSpace($RpcS3Path)) { $RpcS3Path = [Environment]::GetEnvironmentVariable('HELENGINE_PS3_RPCS3_PATH') }
    if ([string]::IsNullOrWhiteSpace($RpcS3Path)) { throw 'Visible mode requires the parent-assigned RPCS3 executable path.' }
    if (@(Get-Process -Name rpcs3 -ErrorAction SilentlyContinue).Count -ne 0) { throw 'RPCS3 is already running.' }
    $launchStartedUtc = [DateTime]::UtcNow
    $launcher = Join-Path $repoRoot 'scripts\launch_in_emulator.ps1'
    & $launcher -ArtifactPath (Join-Path $buildRoot $launchName) -OutputRoot $buildParent -RpcS3Path $RpcS3Path -Visible
    if ($LASTEXITCODE -ne 0) { throw "Visible launcher returned $LASTEXITCODE" }
    $runRoots = @(Get-ChildItem -LiteralPath (Join-Path $buildRoot 'emulator-runs') -Directory)
    if ($runRoots.Count -ne 1) { throw "Expected one fresh emulator run, found $($runRoots.Count)." }
    $runRoot = $runRoots[0].FullName
    $runRecord = Get-Content -LiteralPath (Join-Path $runRoot 'emulator-command.txt') -Raw -Encoding UTF8
    $ttyPath = Join-Path $runRoot 'TTY.log'
    $currentTtyPath = Join-Path (Split-Path -Parent $RpcS3Path) 'log\TTY.log'
    if (-not (Test-Path -LiteralPath $ttyPath -PathType Leaf) -or -not (Test-Path -LiteralPath $currentTtyPath -PathType Leaf)) {
        throw 'Fresh captured or current RPCS3 TTY output is absent.'
    }
    $capturedHash = Get-Sha256 $ttyPath
    $currentTty = Get-Item -LiteralPath $currentTtyPath
    if (-not (Test-FreshProgramTtyEvidence -CapturedHash $capturedHash -CurrentHash (Get-Sha256 $currentTtyPath) `
        -CurrentTtyLastWriteUtc $currentTty.LastWriteTimeUtc -LaunchStartedUtc $launchStartedUtc `
        -BuildId (Split-Path -Leaf $buildRoot) -RunRecord $runRecord)) {
        throw 'Captured TTY is stale or does not match the current log, timestamp, build ID, and visible run record.'
    }
    $tty = Get-Content -LiteralPath $ttyPath -Raw
    $summary = [regex]::Matches($tty, '(?m)^PS3_BATCH2D_PROGRAMS_TESTS passed=(\d+) failed=0\s*$')
    $passLines = [regex]::Matches($tty, '(?m)^PASS [A-Za-z0-9][A-Za-z0-9_-]*\s*$')
    $failLines = [regex]::Matches($tty, '(?m)^FAIL\s')
    if ($summary.Count -ne 1 -or [int]$summary[0].Groups[1].Value -ne $passLines.Count -or $failLines.Count -ne 0) {
        throw "Fresh RPCS3 output shows a failed/incomplete programs fixture; see $ttyPath"
    }
    if (@(Get-Process -Name rpcs3 -ErrorAction SilentlyContinue).Count -ne 0) { throw 'RPCS3 remains running after the fixture.' }
    Write-Output "FixtureTty=$ttyPath"
    Write-Output "FixtureTtySha256=$capturedHash"
    Write-Output "FixtureAssertions=$($passLines.Count)"
}
