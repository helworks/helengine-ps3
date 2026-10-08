[CmdletBinding()]
param(
    [string]$StandardRoot = 'C:\dev\helworks\builds\helengine-ps3\2d-batch-20260924\standard\output\core-95352c521d1f4f769cceb59a1f59b7da',
    [string]$GeneratedRoot = 'C:\dev\helworks\builds\helengine-ps3\2d-batch-20260924\standard\cache\v2\d87d222cd166d9049967e97c50c8e301\b\ps3\release\release\generated-core',
    [switch]$SourceSnapshotOnly,
    [switch]$Visible,
    [string]$RpcS3Path = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$buildParent = 'C:\dev\helworks\builds\helengine-ps3\2d-batch-20260924\native-task3-camera'
$standardRoot = [IO.Path]::GetFullPath($StandardRoot)
$generatedRoot = [IO.Path]::GetFullPath($GeneratedRoot)
$standardManifestPath = Join-Path $standardRoot 'build-manifest.json'
$standardContentRoot = Join-Path $standardRoot 'USRDIR\content'
$coreObject = Join-Path $standardRoot 'obj\generated\helengine_core_unity.o'
$nativeShaderHeaders = @(
    (Join-Path $standardRoot 'shaders\unlit_vpo.h'),
    (Join-Path $standardRoot 'shaders\unlit_fpo.h')
)
$nativeShaderObjects = @(
    (Join-Path $standardRoot 'shaders\unlit.vpo.o'),
    (Join-Path $standardRoot 'shaders\unlit.fpo.o')
)
$generatedShaderHeader = Join-Path $generatedRoot 'ps3_renderer_shaders.hpp'
$lockedImageId = 'sha256:63a28f67dacb0a6d80c165645af126c7d6760e6a3b02e03b6e08aa5aeb96c40f'
$expectedGeneratedHash = '8b897bb2dc47ab4cf661d2854d1979ceb43dc429ca3db310ed8cb3cca91062f8'
$expectedGeneratedCount = 878
$expectedCoreObjectHash = '62286860ade9be73da1722f12ad12b8b508a2c7fd36e768b10b9b13011c4d272'

function Get-Sha256([string]$Path) {
    <# Computes a stable lowercase SHA-256 for one immutable source or build input. #>
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-GeneratedAggregate([string]$Root) {
    <# Fingerprints the full sorted generated tree with the native build's relative-path convention. #>
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $records = foreach ($file in (Get-ChildItem -LiteralPath $rootPath -File -Recurse | Sort-Object FullName)) {
        $relative = $file.FullName.Substring($rootPath.Length).TrimStart('\', '/').Replace('\', '/')
        "$relative=$(Get-Sha256 $file.FullName)"
    }
    $payload = [string]::Join("`n", [string[]]$records)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $hash = [BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($payload))).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
    return [pscustomobject]@{ Hash = $hash; Count = @($records).Count }
}

function Get-RepositorySourceSnapshot([string]$Root) {
    <# Captures every repository-local quoted include reachable from the fixture and make-listed native sources. #>
    $makefilePath = Join-Path $Root 'tests\Makefile.batch2d-camera'
    $makefileText = Get-Content -LiteralPath $makefilePath -Raw
    $seeds = [Collections.Generic.List[string]]::new()
    foreach ($relative in @(
        'tests/Makefile.batch2d-camera', 'tests/ps3_batch2d_camera_tests.cpp',
        'tests/native/Ps3Batch2DCameraRuntime.hpp', 'scripts/tests/ps3-batch2d-camera.tests.ps1',
        'toolchain-lock.json'
    )) { $seeds.Add((Join-Path $Root ($relative.Replace('/', '\')))) }
    foreach ($match in [regex]::Matches($makefileText, '(?m)^\s*\$\(SRC_ROOT\)/([^\s\\]+\.cpp)')) {
        $seeds.Add((Join-Path $Root $match.Groups[1].Value.Replace('/', '\')))
    }

    $queue = [Collections.Generic.Queue[string]]::new()
    foreach ($seed in $seeds) { $queue.Enqueue([IO.Path]::GetFullPath($seed)) }
    $hashes = [Collections.Generic.SortedDictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
    $rootPrefix = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    while ($queue.Count -gt 0) {
        $current = $queue.Dequeue()
        if ($hashes.ContainsKey($current)) { continue }
        if (-not (Test-Path -LiteralPath $current -PathType Leaf)) { throw "Required camera fixture source is absent: $current" }
        $hashes.Add($current, (Get-Sha256 $current))
        $sourceText = Get-Content -LiteralPath $current -Raw
        foreach ($include in [regex]::Matches($sourceText, '(?m)^\s*#\s*include\s*"([^"]+)"')) {
            $name = $include.Groups[1].Value
            $candidates = @(
                (Join-Path (Split-Path -Parent $current) $name),
                (Join-Path $Root $name),
                (Join-Path (Join-Path $Root 'src') $name),
                (Join-Path (Join-Path $Root 'tests') $name)
            )
            foreach ($candidate in $candidates) {
                if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
                $resolved = [IO.Path]::GetFullPath($candidate)
                if ($resolved.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) { $queue.Enqueue($resolved) }
                break
            }
        }
    }
    return $hashes
}

function Test-FreshCameraTtyEvidence {
    <# Accepts an unchanged-but-fresh TTY only when captured/current hashes, timestamp, build ID, and visibility agree. #>
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
    return $RunRecord -match ('(?m)^BuildId=' + [regex]::Escape($BuildId) + '\s*$') -and
        $RunRecord -match '(?m)^Visible=True\s*$'
}

if ($SourceSnapshotOnly) {
    $snapshot = Get-RepositorySourceSnapshot $repoRoot
    $unresolved = @($snapshot.Keys | Where-Object { -not $_.StartsWith(
        [IO.Path]::GetFullPath($repoRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) })
    if ($unresolved.Count -ne 0) { throw 'Source snapshot contains a path outside the repository.' }
    Write-Output "CAMERA_SOURCE_SNAPSHOT=PASS Files=$($snapshot.Count)"
    Write-Output "FixtureSource=$([IO.Path]::GetFullPath((Join-Path $repoRoot 'tests\ps3_batch2d_camera_tests.cpp')))"
    return
}

foreach ($requiredPath in @($standardManifestPath, $coreObject, $generatedShaderHeader, $standardContentRoot) + $nativeShaderHeaders + $nativeShaderObjects) {
    if (-not (Test-Path -LiteralPath $requiredPath)) { throw "Matching fresh standard-build input is missing: $requiredPath" }
}
$standardManifest = Get-Content -LiteralPath $standardManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
$lock = Get-Content -LiteralPath (Join-Path $repoRoot 'toolchain-lock.json') -Raw | ConvertFrom-Json
if ([string]$lock.ResolvedImage.ImageId -cne $lockedImageId -or [string]$standardManifest.ImageId -cne $lockedImageId) {
    throw 'Standard build and repository lock do not identify the approved pinned SDK image.'
}
if ([IO.Path]::GetFullPath([string]$standardManifest.GeneratedCoreRoot).TrimEnd('\', '/') -ine $generatedRoot.TrimEnd('\', '/')) {
    throw 'Standard manifest GeneratedCoreRoot does not match the supplied GeneratedRoot.'
}
if ([int]$standardManifest.InstalledContentFileCount -ne 76) {
    throw "Expected the approved 76-file installed cooked content set; found $($standardManifest.InstalledContentFileCount)."
}
$generatedFingerprint = Get-GeneratedAggregate $generatedRoot
if ($generatedFingerprint.Hash -cne $expectedGeneratedHash -or $generatedFingerprint.Count -ne $expectedGeneratedCount -or
    [string]$standardManifest.GeneratedCoreSha256 -cne $generatedFingerprint.Hash -or
    [int]$standardManifest.GeneratedCoreFileCount -ne $generatedFingerprint.Count) {
    throw 'Supplied generated Core does not match the fresh standard build manifest and approved aggregate.'
}
$actualCoreObjectHash = Get-Sha256 $coreObject
if ($actualCoreObjectHash -cne $expectedCoreObjectHash) { throw 'Fresh standard Core unity object hash does not match the parent-provided input.' }

$shaderHeader = Get-Content -LiteralPath $generatedShaderHeader -Raw
$shaderPaths = @(
    [regex]::Match($shaderHeader, 'BatchTexturedShaderPath\[\]\s*=\s*"([^"]+)"').Groups[1].Value,
    [regex]::Match($shaderHeader, 'BatchRoundedShapeShaderPath\[\]\s*=\s*"([^"]+)"').Groups[1].Value
)
if ($shaderPaths.Count -ne 2 -or @($shaderPaths | Where-Object { $_ -notmatch '^cooked/shaders/[0-9a-f]{64}\.ps3sp$' }).Count -ne 0) {
    throw 'Generated production shader header did not contain exactly the two canonical 2D P3SP paths.'
}
foreach ($shaderPath in $shaderPaths) {
    if (-not (Test-Path -LiteralPath (Join-Path $standardContentRoot $shaderPath.Replace('/', '\')) -PathType Leaf)) {
        throw "Standard cooked content is missing generated shader input: $shaderPath"
    }
}
foreach ($assetPath in @(
    'cooked/imported/01199c4588eaad6253c3c88a9ce1182f48d94477c36225fe4124b060c21fc679',
    'cooked/engine/models/cube.hasset',
    'cooked/materials/rendering/cube_test/cubetestsolid.hasset'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $standardContentRoot $assetPath.Replace('/', '\')) -PathType Leaf)) {
        throw "Standard content is missing a fixture asset: $assetPath"
    }
}

$contentFiles = @(Get-ChildItem -LiteralPath $standardContentRoot -File -Recurse | Sort-Object FullName)
if ($contentFiles.Count -ne [int]$standardManifest.InstalledContentFileCount) {
    throw "Installed cooked content file count mismatch: manifest=$($standardManifest.InstalledContentFileCount), disk=$($contentFiles.Count)."
}
if (-not (Test-Path -LiteralPath $buildParent -PathType Container)) { New-Item -ItemType Directory -Path $buildParent -ErrorAction Stop | Out-Null }
$buildId = 'camera-' + [guid]::NewGuid().ToString('N')
$buildRoot = Join-Path $buildParent $buildId
New-Item -ItemType Directory -Path $buildRoot -ErrorAction Stop | Out-Null
$temporaryRoot = Join-Path $buildRoot 'temp'
New-Item -ItemType Directory -Path $temporaryRoot -ErrorAction Stop | Out-Null
$sourceSnapshot = Get-RepositorySourceSnapshot $repoRoot
$inputHashes = [ordered]@{
    'standard/build-manifest.json' = Get-Sha256 $standardManifestPath
    'standard/obj/generated/helengine_core_unity.o' = $actualCoreObjectHash
    'generated-core/aggregate-sha256' = $generatedFingerprint.Hash
}
foreach ($header in $nativeShaderHeaders) {
    $relative = $header.Substring($standardRoot.TrimEnd('\', '/').Length).TrimStart('\', '/').Replace('\', '/')
    $inputHashes['standard/' + $relative] = Get-Sha256 $header
}
foreach ($shaderObject in $nativeShaderObjects) {
    $relative = $shaderObject.Substring($standardRoot.TrimEnd('\', '/').Length).TrimStart('\', '/').Replace('\', '/')
    $inputHashes['standard/' + $relative] = Get-Sha256 $shaderObject
}
foreach ($shaderPath in $shaderPaths) {
    $label = 'standard/content/' + $shaderPath
    $inputHashes[$label] = Get-Sha256 (Join-Path $standardContentRoot $shaderPath.Replace('/', '\'))
}
$contentHashes = [ordered]@{}
foreach ($file in $contentFiles) {
    $relative = $file.FullName.Substring($standardContentRoot.Length).TrimStart('\', '/').Replace('\', '/')
    $contentHashes['standard/content/' + $relative] = Get-Sha256 $file.FullName
}

$imageOutput = & docker image inspect --format '{{.Id}}' $lockedImageId 2>&1
if ($LASTEXITCODE -ne 0 -or ($imageOutput | Out-String).Trim() -cne $lockedImageId) { throw 'Pinned Docker SDK image is absent or mismatched.' }
$dockerArgs = @(
    'run', '--rm', '--pull=never', '--network', 'none',
    '--mount', "type=bind,source=$repoRoot,target=/src,readonly",
    '--mount', "type=bind,source=$buildRoot,target=/out",
    '--mount', "type=bind,source=$generatedRoot,target=/generated,readonly",
    '--mount', "type=bind,source=$standardRoot,target=/standard,readonly",
    '--mount', "type=bind,source=$standardContentRoot,target=/app_home/USRDIR/content,readonly",
    '--env', 'TMPDIR=/out/temp', '--env', 'TMP=/out/temp', '--env', 'TEMP=/out/temp', '--env', 'DockerTMPDIR=/out/temp',
    $lockedImageId, 'make', '-f', '/src/tests/Makefile.batch2d-camera', 'all',
    'SRC_ROOT=/src', 'BUILD_ROOT=/out', 'STANDARD_ROOT=/standard'
)
$oldTemporaryEnvironment = @{}
foreach ($name in @('TEMP', 'TMP', 'TMPDIR', 'DockerTMPDIR')) {
    $oldTemporaryEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    [Environment]::SetEnvironmentVariable($name, $temporaryRoot, 'Process')
}
$logPath = Join-Path $buildRoot 'build.log'
$priorErrorPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & docker @dockerArgs *> $logPath
    $buildExit = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $priorErrorPreference
    foreach ($name in @('TEMP', 'TMP', 'TMPDIR', 'DockerTMPDIR')) {
        [Environment]::SetEnvironmentVariable($name, $oldTemporaryEnvironment[$name], 'Process')
    }
}
$buildLog = Get-Content -LiteralPath $logPath -Raw
if ($buildLog.Length -gt 6500) { $buildLog = $buildLog.Substring($buildLog.Length - 6500) }
Write-Output $buildLog
Write-Output "BuildRoot=$buildRoot"
if ($buildExit -ne 0) { throw "Pinned Docker camera fixture build failed with exit $buildExit; full log: $logPath" }

foreach ($path in $sourceSnapshot.Keys) {
    if ((Get-Sha256 $path) -cne $sourceSnapshot[$path]) { throw "Repository build input changed during native compilation: $path" }
}
$postGeneratedFingerprint = Get-GeneratedAggregate $generatedRoot
if ($postGeneratedFingerprint.Hash -cne $generatedFingerprint.Hash -or $postGeneratedFingerprint.Count -ne $generatedFingerprint.Count) {
    throw 'Generated Core changed during the camera fixture build.'
}
if ((Get-Sha256 $coreObject) -cne $actualCoreObjectHash -or (Get-Sha256 $standardManifestPath) -cne $inputHashes['standard/build-manifest.json']) {
    throw 'Standard manifest or Core unity object changed during the camera fixture build.'
}
foreach ($header in $nativeShaderHeaders) {
    $relative = $header.Substring($standardRoot.TrimEnd('\', '/').Length).TrimStart('\', '/').Replace('\', '/')
    if ((Get-Sha256 $header) -cne $inputHashes['standard/' + $relative]) {
        throw "Generated embedded diagnostic shader header changed during compilation: $relative"
    }
}
foreach ($key in $contentHashes.Keys) {
    $relative = $key.Substring('standard/content/'.Length).Replace('/', '\')
    if ((Get-Sha256 (Join-Path $standardContentRoot $relative)) -cne $contentHashes[$key]) {
        throw "Cooked standard content changed during native compilation: $relative"
    }
}
foreach ($path in $sourceSnapshot.Keys) {
    $relative = $path.Substring($repoRoot.TrimEnd('\', '/').Length).TrimStart('\', '/').Replace('\', '/')
    $inputHashes['repository/' + $relative] = $sourceSnapshot[$path]
}
$inputHashes['toolchain-lock.json'] = Get-Sha256 (Join-Path $repoRoot 'toolchain-lock.json')

$stagedContentRoot = Join-Path $buildRoot 'USRDIR\content'
New-Item -ItemType Directory -Path $stagedContentRoot -Force -ErrorAction Stop | Out-Null
Copy-Item -Path (Join-Path $standardContentRoot '*') -Destination $stagedContentRoot -Recurse -Force -ErrorAction Stop
foreach ($file in $contentFiles) {
    $relative = $file.FullName.Substring($standardContentRoot.Length).TrimStart('\', '/')
    $staged = Join-Path $stagedContentRoot $relative
    if ((Get-Sha256 $staged) -cne $contentHashes['standard/content/' + $relative.Replace('\', '/')]) {
        throw "Staged cooked content hash mismatch: $relative"
    }
}
$artifactNames = @('ps3_batch2d_camera_tests.elf', 'ps3_batch2d_camera_tests.self', 'ps3_batch2d_camera_tests.fake.self')
$artifactRecords = foreach ($name in $artifactNames) {
    $artifactPath = Join-Path $buildRoot $name
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf) -or (Get-Item -LiteralPath $artifactPath).Length -eq 0) {
        throw "Successful PPU build omitted nonempty artifact $name"
    }
    [ordered]@{ Path = $name; Sha256 = Get-Sha256 $artifactPath; Bytes = (Get-Item -LiteralPath $artifactPath).Length }
}
$manifest = [ordered]@{
    SchemaVersion = 1
    BuildId = $buildId
    TestKind = 'Ps3Batch2DCamera'
    StandardBuildId = [string]$standardManifest.BuildId
    StandardManifestSha256 = Get-Sha256 $standardManifestPath
    ToolchainLockSha256 = Get-Sha256 (Join-Path $repoRoot 'toolchain-lock.json')
    ToolchainImageId = $lockedImageId
    GeneratedCoreRoot = $generatedRoot
    GeneratedCoreSha256 = $generatedFingerprint.Hash
    GeneratedCoreFileCount = $generatedFingerprint.Count
    StandardCoreObjectSha256 = $actualCoreObjectHash
    ShaderPaths = $shaderPaths
    SourceHashes = $inputHashes
    InstalledContentFileCount = $contentFiles.Count
    LaunchArtifact = 'ps3_batch2d_camera_tests.self'
    Artifacts = @($artifactRecords)
}
$manifestPath = Join-Path $buildRoot 'build-manifest.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
Write-Output 'CAMERA_PINNED_DOCKER_BUILD=PASS'
Write-Output "BuildManifest=$manifestPath"
if (-not $Visible) { return }

if ([string]::IsNullOrWhiteSpace($RpcS3Path)) { throw 'Visible mode requires the parent-assigned RPCS3 executable path.' }
$RpcS3Path = [IO.Path]::GetFullPath($RpcS3Path)
if (-not (Test-Path -LiteralPath $RpcS3Path -PathType Leaf)) { throw "RPCS3 executable is missing: $RpcS3Path" }
if (@(Get-Process -Name rpcs3 -ErrorAction SilentlyContinue).Count -ne 0) { throw 'RPCS3 is already running; refusing to start a concurrent emulator.' }
$launchStartedUtc = [DateTime]::UtcNow
$launcher = Join-Path $repoRoot 'scripts\launch_in_emulator.ps1'
& $launcher -ArtifactPath (Join-Path $buildRoot 'ps3_batch2d_camera_tests.self') -OutputRoot $buildParent -RpcS3Path $RpcS3Path -Visible
if ($LASTEXITCODE -ne 0) { throw "Visible launcher returned $LASTEXITCODE" }
$runRoots = @(Get-ChildItem -LiteralPath (Join-Path $buildRoot 'emulator-runs') -Directory)
if ($runRoots.Count -ne 1) { throw "Expected exactly one fresh emulator run, found $($runRoots.Count)." }
$runRoot = $runRoots[0].FullName
$runRecord = Get-Content -LiteralPath (Join-Path $runRoot 'emulator-command.txt') -Raw -Encoding UTF8
$capturedTtyPath = Join-Path $runRoot 'TTY.log'
$currentTtyPath = Join-Path (Split-Path -Parent $RpcS3Path) 'log\TTY.log'
if (-not (Test-Path -LiteralPath $capturedTtyPath -PathType Leaf) -or -not (Test-Path -LiteralPath $currentTtyPath -PathType Leaf)) {
    throw 'Fresh captured or current RPCS3 TTY output is missing.'
}
$currentTty = Get-Item -LiteralPath $currentTtyPath
$capturedTtyHash = Get-Sha256 $capturedTtyPath
if (-not (Test-FreshCameraTtyEvidence -CapturedHash $capturedTtyHash -CurrentHash (Get-Sha256 $currentTtyPath) `
    -CurrentTtyLastWriteUtc $currentTty.LastWriteTimeUtc -LaunchStartedUtc $launchStartedUtc `
    -BuildId $buildId -RunRecord $runRecord)) {
    throw 'Captured TTY failed build ID, freshness, visibility, or captured/current hash checks.'
}
$tty = Get-Content -LiteralPath $capturedTtyPath -Raw
$summary = [regex]::Matches($tty, '(?m)^PS3_BATCH2D_CAMERA_TESTS passed=(\d+) failed=0\s*$')
$passLines = [regex]::Matches($tty, '(?m)^PASS [A-Za-z0-9][A-Za-z0-9_-]*\s*$')
$failLines = [regex]::Matches($tty, '(?m)^FAIL\s')
if ($summary.Count -ne 1 -or [int]$summary[0].Groups[1].Value -ne $passLines.Count -or $failLines.Count -ne 0) {
    throw "Fresh RPCS3 output reports failed or incomplete camera fixture assertions: $capturedTtyPath"
}
foreach ($pixelName in @(
    'first-camera-red-sprite-survives-following-no-clear-camera',
    'second-ui-only-camera-adds-green-sprite-without-clearing',
    'nested-clip-chain-renders-inside-pixel-center-intersection',
    'nested-clip-chain-excludes-pixels-outside-inner-clip',
    'empty-clip-region-preserves-camera-clear-pixel',
    'shared-text-emitter-samples-dynamically-updated-font-atlas',
    'third-camera-cube-overwrites-intervening-ui-green-pixel-after-depth-clear'
)) {
    if ($tty -notmatch ('(?m)^PASS ' + [regex]::Escape($pixelName) + '\s*$')) { throw "Fresh TTY omitted required camera evidence '$pixelName'." }
}
if (@(Get-Process -Name rpcs3 -ErrorAction SilentlyContinue).Count -ne 0) { throw 'RPCS3 remains running after the camera fixture.' }
Write-Output "FixtureTty=$capturedTtyPath"
Write-Output "FixtureTtySha256=$capturedTtyHash"
Write-Output "FixtureAssertions=$($passLines.Count)"
