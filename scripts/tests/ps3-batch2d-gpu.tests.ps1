[CmdletBinding()]
param(
    [switch]$BuildOnly,
    [string]$RpcS3Path = $env:HELENGINE_PS3_RPCS3_PATH,
    [string]$StandardRoot = 'C:\dev\helworks\builds\helengine-ps3\2d-batch-20260924\standard\output\core-2c7f18d1a11343b3a21162969fa07513',
    [string]$GeneratedRoot = 'C:\dev\helworks\builds\helengine-ps3\2d-batch-20260924\standard\cache\v2\d87d222cd166d9049967e97c50c8e301\b\ps3\release\release\generated-core'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$buildParent = 'C:\dev\helworks\builds\helengine-ps3\2d-batch-20260924\native-task2-gpu'
$standardRoot = [IO.Path]::GetFullPath($StandardRoot)
$generatedRoot = [IO.Path]::GetFullPath($GeneratedRoot)
$standardContentRoot = Join-Path $standardRoot 'USRDIR\content'
$lockedImageId = 'sha256:63a28f67dacb0a6d80c165645af126c7d6760e6a3b02e03b6e08aa5aeb96c40f'
$shaderNames = @(
    '842895fde6e1db0816b78c52d176284de1fbb2d40dc819196b6ba7db7d09558a.ps3sp',
    '9a0867e0cecd7e04a48ecb452b32db7a42deecaab88912cbcc71427cf4b3233d.ps3sp'
)

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-GeneratedFingerprint([string]$Root) {
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

function Get-SourceSnapshot([string]$Root) {
    $seedPaths = @(
        'tests/Makefile.batch2d-gpu', 'tests/ps3_batch2d_gpu_tests.cpp',
        'scripts/tests/ps3-batch2d-gpu.tests.ps1', 'toolchain-lock.json',
        'src/platform/ps3/Ps3Batch2DRenderer.hpp', 'src/platform/ps3/Ps3Batch2DRenderer.cpp',
        'src/platform/ps3/Ps3Batch2DSink.hpp', 'src/platform/ps3/Ps3Batch2DSink.cpp',
        'src/platform/ps3/Ps3Batch2DPrograms.hpp', 'src/platform/ps3/Ps3Batch2DPrograms.cpp',
        'src/platform/ps3/Ps3Batch2DUniformProvider.hpp', 'src/platform/ps3/Ps3Batch2DUniformProvider.cpp',
        'src/platform/ps3/Ps3Batch2DResources.hpp', 'src/platform/ps3/Ps3Batch2DResources.cpp',
        'src/platform/ps3/Ps3RuntimeTexture.hpp', 'src/platform/ps3/Ps3RuntimeTexture.cpp',
        'src/platform/ps3/Ps3AuthoredProgram.hpp', 'src/platform/ps3/Ps3AuthoredProgram.cpp',
        'src/platform/ps3/Ps3AuthoredTexture.hpp', 'src/platform/ps3/Ps3AuthoredTexture.cpp',
        'src/platform/ps3/Ps3GtfTexture.hpp', 'src/platform/ps3/Ps3GtfTexture.cpp',
        'src/platform/ps3/Ps3TexturePrefixStream.hpp',
        'src/platform/ps3/Ps3ShaderArtifactReader.hpp', 'src/platform/ps3/Ps3ShaderArtifactReader.cpp',
        'src/platform/ps3/Ps3RsxProgramValidator.hpp', 'src/platform/ps3/Ps3RsxProgramValidator.cpp',
        'src/platform/ps3/Ps3ContentStreamSource.hpp', 'src/platform/ps3/Ps3ContentStreamSource.cpp',
        'src/platform/ps3/Ps3Video.hpp', 'src/platform/ps3/Ps3Video.cpp',
        'scripts/launch_in_emulator.ps1', 'scripts/ps3-paths.ps1'
    )
    $queue = [Collections.Generic.Queue[string]]::new()
    foreach ($relative in $seedPaths) { $queue.Enqueue((Join-Path $Root ($relative.Replace('/', '\')))) }
    $hashes = [Collections.Generic.SortedDictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
    while ($queue.Count -gt 0) {
        $current = [IO.Path]::GetFullPath($queue.Dequeue())
        if ($hashes.ContainsKey($current)) { continue }
        if (-not (Test-Path -LiteralPath $current -PathType Leaf)) { throw "Required native GPU input is missing: $current" }
        $hashes.Add($current, (Get-Sha256 $current))
        $sourceText = Get-Content -LiteralPath $current -Raw
        foreach ($match in [regex]::Matches($sourceText, '(?m)^\s*#\s*include\s*"([^"]+)"')) {
            $includeName = $match.Groups[1].Value
            $candidates = @(
                (Join-Path (Split-Path -Parent $current) $includeName),
                (Join-Path $Root $includeName),
                (Join-Path (Join-Path $Root 'src') $includeName)
            )
            foreach ($candidate in $candidates) {
                if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
                $resolved = [IO.Path]::GetFullPath($candidate)
                $rootPrefix = $Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
                if ($resolved.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) { $queue.Enqueue($resolved) }
                break
            }
        }
    }
    return $hashes
}

$lockPath = Join-Path $repoRoot 'toolchain-lock.json'
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
if ([string]$lock.ResolvedImage.ImageId -ne $lockedImageId) { throw 'Pinned SDK image differs from toolchain-lock.json.' }
foreach ($path in @(
    (Join-Path $standardRoot 'obj\generated\helengine_core_unity.o'),
    (Join-Path $standardContentRoot "cooked\shaders\$($shaderNames[0])"),
    (Join-Path $standardContentRoot "cooked\shaders\$($shaderNames[1])")
)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Approved standard-build input is missing: $path" }
}
if (-not (Test-Path -LiteralPath $buildParent -PathType Container)) { New-Item -ItemType Directory -Path $buildParent -ErrorAction Stop | Out-Null }
$buildId = 'gpu-' + (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N')
$buildRoot = Join-Path $buildParent $buildId
New-Item -ItemType Directory -Path $buildRoot -ErrorAction Stop | Out-Null
$temporaryRoot = Join-Path $buildRoot 'temp'
New-Item -ItemType Directory -Path $temporaryRoot -ErrorAction Stop | Out-Null
$contentRoot = Join-Path $buildRoot 'USRDIR\content\cooked\shaders'
New-Item -ItemType Directory -Path $contentRoot -Force -ErrorAction Stop | Out-Null
$sourceSnapshot = Get-SourceSnapshot $repoRoot
$generatedFingerprint = Get-GeneratedFingerprint $generatedRoot
$standardObject = Join-Path $standardRoot 'obj\generated\helengine_core_unity.o'
$standardObjectHash = Get-Sha256 $standardObject
$shaderHashes = [ordered]@{}
foreach ($shaderName in $shaderNames) {
    $sourceShader = Join-Path $standardContentRoot "cooked\shaders\$shaderName"
    $targetShader = Join-Path $contentRoot $shaderName
    Copy-Item -LiteralPath $sourceShader -Destination $targetShader
    $shaderHashes[$shaderName] = Get-Sha256 $sourceShader
    if ((Get-Sha256 $targetShader) -ne $shaderHashes[$shaderName]) { throw "Staged shader hash mismatch: $shaderName" }
}

$imageOutput = & docker image inspect --format '{{.Id}}' $lockedImageId 2>&1
if ($LASTEXITCODE -ne 0 -or ($imageOutput | Out-String).Trim() -cne $lockedImageId) { throw 'Pinned Docker SDK image is absent or mismatched.' }
$logPath = Join-Path $buildRoot 'build.log'
$stdoutPath = Join-Path $buildRoot 'build.stdout.log'
$stderrPath = Join-Path $buildRoot 'build.stderr.log'
$dockerArgs = @(
    'run', '--rm', '--pull=never', '--network', 'none',
    '--mount', "type=bind,source=$repoRoot,target=/src,readonly",
    '--mount', "type=bind,source=$buildRoot,target=/out",
    '--mount', "type=bind,source=$generatedRoot,target=/generated,readonly",
    '--mount', "type=bind,source=$standardRoot,target=/standard,readonly",
    '--mount', "type=bind,source=$standardContentRoot,target=/app_home/USRDIR/content,readonly",
    '--env', 'TMPDIR=/out/temp', '--env', 'TMP=/out/temp', '--env', 'TEMP=/out/temp',
    $lockedImageId, 'make', '-f', '/src/tests/Makefile.batch2d-gpu', 'all', 'SRC_ROOT=/src', 'BUILD_ROOT=/out'
)
$oldTemp = @{}
foreach ($name in @('TEMP', 'TMP', 'TMPDIR')) {
    $oldTemp[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    [Environment]::SetEnvironmentVariable($name, $temporaryRoot, 'Process')
}
try {
    $dockerProcess = Start-Process -FilePath 'docker.exe' -ArgumentList $dockerArgs -PassThru -Wait -NoNewWindow `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $buildExit = $dockerProcess.ExitCode
} finally {
    foreach ($name in @('TEMP', 'TMP', 'TMPDIR')) { [Environment]::SetEnvironmentVariable($name, $oldTemp[$name], 'Process') }
}
$buildLog = [IO.File]::ReadAllText($stdoutPath) + [IO.File]::ReadAllText($stderrPath)
[IO.File]::WriteAllText($logPath, $buildLog, [Text.UTF8Encoding]::new($false))
if ($buildLog.Length -gt 6500) { $buildLog = $buildLog.Substring($buildLog.Length - 6500) }
Write-Output $buildLog
if ($buildExit -ne 0) { throw "Pinned Docker PPU build failed with exit $buildExit; full log: $logPath" }
foreach ($path in $sourceSnapshot.Keys) {
    if ((Get-Sha256 $path) -ne $sourceSnapshot[$path]) { throw "Repository input changed during PPU build: $path" }
}
if ((Get-GeneratedFingerprint $generatedRoot).Hash -ne $generatedFingerprint.Hash -or
    (Get-Sha256 $standardObject) -ne $standardObjectHash) {
    throw 'Frozen generated Core or standard object changed during compilation.'
}
foreach ($shaderName in $shaderNames) {
    if ((Get-Sha256 (Join-Path $standardContentRoot "cooked\shaders\$shaderName")) -ne $shaderHashes[$shaderName]) {
        throw "Standard shader changed during compilation: $shaderName"
    }
}

$launchName = 'ps3_batch2d_gpu_tests.self'
$artifactNames = @($launchName, 'ps3_batch2d_gpu_tests.elf', 'ps3_batch2d_gpu_tests.fake.self')
$artifactRecords = foreach ($name in $artifactNames) {
    $path = Join-Path $buildRoot $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
        throw "Build omitted required artifact $name"
    }
    [ordered]@{ Path = $name; Sha256 = Get-Sha256 $path; Bytes = (Get-Item -LiteralPath $path).Length }
}
$sourceRecords = foreach ($path in $sourceSnapshot.Keys) {
    [ordered]@{
        Path = $path.Substring($repoRoot.TrimEnd('\', '/').Length).TrimStart('\', '/').Replace('\', '/')
        Sha256 = $sourceSnapshot[$path]
    }
}
$manifest = [ordered]@{
    SchemaVersion = 1
    BuildId = $buildId
    TestKind = 'Ps3Batch2DGpuSubmission'
    ToolchainLockSha256 = Get-Sha256 $lockPath
    ToolchainImageId = $lockedImageId
    GeneratedCoreSha256 = $generatedFingerprint.Hash
    GeneratedCoreFileCount = $generatedFingerprint.Count
    StandardCoreObjectSha256 = $standardObjectHash
    ShaderHashes = $shaderHashes
    SourceHashes = @($sourceRecords)
    LaunchArtifact = $launchName
    Artifacts = @($artifactRecords)
}
$manifestPath = Join-Path $buildRoot 'build-manifest.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
Write-Output "PINNED_DOCKER_BUILD=PASS BuildRoot=$buildRoot"
Write-Output "BuildManifest=$manifestPath"
if ($BuildOnly) { return }

if ([string]::IsNullOrWhiteSpace($RpcS3Path)) { throw 'RPCS3 executable is required: pass -RpcS3Path or set HELENGINE_PS3_RPCS3_PATH.' }
$RpcS3Path = [IO.Path]::GetFullPath($RpcS3Path)
if (-not (Test-Path -LiteralPath $RpcS3Path -PathType Leaf)) { throw "RPCS3 executable is missing: $RpcS3Path" }
if (@(Get-Process -Name rpcs3 -ErrorAction SilentlyContinue).Count -ne 0) { throw 'RPCS3 is already running; refusing to start a concurrent emulator.' }
$launchStartedUtc = [DateTime]::UtcNow
$launcher = Join-Path $repoRoot 'scripts\launch_in_emulator.ps1'
& $launcher -ArtifactPath (Join-Path $buildRoot $launchName) -OutputRoot $buildParent -RpcS3Path $RpcS3Path -Visible
$runRoots = @(Get-ChildItem -LiteralPath (Join-Path $buildRoot 'emulator-runs') -Directory)
if ($runRoots.Count -ne 1) { throw "Expected one fresh emulator run directory, found $($runRoots.Count)." }
$runRoot = $runRoots[0].FullName
$runRecord = Get-Content -LiteralPath (Join-Path $runRoot 'emulator-command.txt') -Raw -Encoding UTF8
$recordedExitCode = [regex]::Match($runRecord, '(?m)^ExitCode=(\d+)\s*$')
$ttyPath = Join-Path $runRoot 'TTY.log'
if (-not (Test-Path -LiteralPath $ttyPath -PathType Leaf)) { throw "Fresh RPCS3 TTY log is missing: $ttyPath" }
$emulatorTty = Join-Path (Split-Path -Parent $RpcS3Path) 'log\TTY.log'
if ((Get-Item -LiteralPath $emulatorTty).LastWriteTimeUtc -lt $launchStartedUtc.AddSeconds(-2) -or
    (Get-Sha256 $emulatorTty) -ne (Get-Sha256 $ttyPath) -or
    $runRecord -notmatch ('(?m)^BuildId=' + [regex]::Escape($buildId) + '\s*$') -or
    $runRecord -notmatch '(?m)^Visible=True\s*$' -or
    -not $recordedExitCode.Success -or [int]$recordedExitCode.Groups[1].Value -ne 0) {
    throw 'Captured TTY is stale or does not match the fresh visible build run.'
}
$tty = Get-Content -LiteralPath $ttyPath -Raw
$summary = [regex]::Matches($tty, '(?m)^PS3_BATCH2D_GPU_TESTS passed=(\d+) failed=0\s*$')
$passLines = [regex]::Matches($tty, '(?m)^PASS [A-Za-z0-9][A-Za-z0-9_-]*\s*$')
$failLines = [regex]::Matches($tty, '(?m)^FAIL\s')
if ($summary.Count -ne 1 -or [int]$summary[0].Groups[1].Value -ne $passLines.Count -or $failLines.Count -ne 0) {
    throw "RPCS3 fixture assertions failed or are incomplete; see $ttyPath"
}
if ($tty -notmatch '(?m)^PS3_BATCH2D_ALPHA_TARGET format=A8R8G8B8 ' -or
    $tty -notmatch '(?m)^PS3_BATCH2D_ATTRIBUTE .*sdkType=9 ' -or
    $tty -notmatch '(?m)^PS3_BATCH2D_RGBA_PIXEL name=a8r8g8b8-source-over-alpha expected=\(64,0,128,192\) actual=\(\d+,\d+,\d+,\d+\)\s*$') {
    throw "Fresh RPCS3 output is missing reflected attribute or independent alpha-target evidence: $ttyPath"
}
$requiredPixels = @(
    'checker-top-left', 'checker-top-right', 'checker-bottom-left', 'checker-bottom-right',
    'red-then-blue', 'blue-then-red', 'red-blue-red', 'translated-nonsquare-viewport',
    'rounded-fill-interior', 'rounded-border-interior', 'rounded-enabled-corner-alpha-outside',
    'rounded-disabled-corner-square', 'rounded-zero-border-fill', 'rounded-full-border-center',
    'nested-scissor-interior', 'nested-scissor-exterior', 'queued-frame-completes-before-texture-update',
    'updated-texture-next-frame'
)
foreach ($pixelName in $requiredPixels) {
    if ($tty -notmatch ('(?m)^PS3_BATCH2D_PIXEL name=' + [regex]::Escape($pixelName) + ' ')) {
        throw "Fresh RPCS3 output omitted predetermined pixel sample '$pixelName': $ttyPath"
    }
}
if (@(Get-Process -Name rpcs3 -ErrorAction SilentlyContinue).Count -ne 0) { throw 'RPCS3 remains running after the fixture.' }
Write-Output "FixtureTty=$ttyPath"
Write-Output "FixtureTtySha256=$(Get-Sha256 $ttyPath)"
Write-Output "FixtureAssertions=$($passLines.Count)"
