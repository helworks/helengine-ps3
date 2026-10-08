<#
.SYNOPSIS
    Contract tests for scripts/build.ps1 and scripts/launch_in_emulator.ps1.

.DESCRIPTION
    Exercises the build wrapper and launcher against test-owned fixtures under a fresh
    repository-local .ps3\contract-tests\<id> directory by default. A fake `docker`
    command (a .cmd shim ahead of
    the real one on PATH for this process only) simulates the SDK container so that
    nonzero builds, zero-byte outputs and successful builds can be checked without the
    real toolchain; a fake emulator script records whether it was launched and with which
    arguments. There is no production skip-verification flag: the fakes are injected
    purely through PATH and explicit path parameters.

    Requires a prepared toolchain-lock.json (ResolvedImage present) because build.ps1
    reads the locked image identity and the Cg mount contract.

    Command contract:
        powershell -NoProfile -File scripts/tests/build-contract.tests.ps1
#>
[CmdletBinding()]
param(
    [string]$ExternalRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$ExternalRoot = if ([string]::IsNullOrWhiteSpace($ExternalRoot)) {
    Join-Path $repoRoot '.ps3\contract-tests'
} else {
    [IO.Path]::GetFullPath($ExternalRoot)
}
$buildScript = Join-Path $repoRoot 'scripts\build.ps1'
$launcherScript = Join-Path $repoRoot 'scripts\launch_in_emulator.ps1'
$lockPath = Join-Path $repoRoot 'toolchain-lock.json'
$testId = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
$testsRoot = Join-Path ([IO.Path]::GetFullPath($ExternalRoot)) "script-tests-$testId"
$outputRoot = Join-Path $testsRoot 'out put'
$testStateRoot = Join-Path $testsRoot 'state'
$fakeDockerDirectory = Join-Path $testsRoot 'fake-docker'
$fakeEmulatorDirectory = Join-Path $testsRoot 'fake rpcs3'
$fakeEmulator = Join-Path $fakeEmulatorDirectory 'rpcs3 fake.cmd'
$fakeLaunchMarker = Join-Path $fakeEmulatorDirectory 'launched.txt'
$failures = [Collections.Generic.List[string]]::new()
$passes = [Collections.Generic.List[string]]::new()

foreach ($required in @($buildScript, $launcherScript, $lockPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing: $required" }
}
$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not ($lock.ResolvedImage.PSObject.Properties.Name -contains 'ImageId')) {
    throw 'toolchain-lock.json has no ResolvedImage; run scripts/prepare_toolchain.ps1 before the contract tests.'
}

New-Item -ItemType Directory -Path $outputRoot, $fakeDockerDirectory, $fakeEmulatorDirectory | Out-Null
$repoCgLibrary = Join-Path $repoRoot '.ps3\dependencies\cg\usr\lib64\libCg.so'
$repoCgLibraryHashBefore = if (Test-Path -LiteralPath $repoCgLibrary -PathType Leaf) {
    (Get-FileHash -LiteralPath $repoCgLibrary -Algorithm SHA256).Hash.ToLowerInvariant()
} else {
    $null
}
$testCgLibrary = Join-Path $testStateRoot 'dependencies\cg\usr\lib64\libCg.so'
New-Item -ItemType Directory -Path (Split-Path -Parent $testCgLibrary) -Force | Out-Null
[IO.File]::WriteAllBytes($testCgLibrary, [byte[]]@(1, 2, 3, 4))

# --- Fake docker shim ------------------------------------------------------------------
Set-Content -LiteralPath (Join-Path $fakeDockerDirectory 'docker.cmd') -Encoding Ascii -Value @(
    '@echo off',
    'powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0fake-docker.ps1" %*',
    'exit /b %ERRORLEVEL%'
)
Set-Content -LiteralPath (Join-Path $fakeDockerDirectory 'fake-docker.ps1') -Encoding UTF8 -Value @'
$arguments = @($args | ForEach-Object { [string]$_ })
$scenario = $env:HELENGINE_PS3_TEST_SCENARIO
if ($arguments.Count -ge 2 -and $arguments[0] -eq 'image' -and $arguments[1] -eq 'inspect') {
    Write-Output $arguments[2]
    exit 0
}
if ($arguments.Count -ge 1 -and $arguments[0] -eq 'run') {
    if ($arguments -contains '-h') {
        Write-Output 'ELF Header:'
        Write-Output '  Class:                             ELF64'
        Write-Output '  Data:                              2''s complement, big endian'
        Write-Output '  Machine:                           PowerPC64'
        exit 0
    }
    $outSource = $null
    foreach ($argument in $arguments) {
        if ($argument -match '^type=bind,source=(.+),target=/out$') { $outSource = $Matches[1] }
    }
    if ($null -eq $outSource) { Write-Error 'fake docker: no /out mount'; exit 9 }
    Add-Content -LiteralPath (Join-Path $outSource 'fake-docker-args.txt') -Value ($arguments -join "`n")
    switch ($scenario) {
        'fail' {
            Write-Output 'fake docker: simulated compiler error'
            exit 3
        }
        'zero-elf' {
            [IO.File]::WriteAllBytes((Join-Path $outSource 'helengine_ps3.elf'), [byte[]]@())
            [IO.File]::WriteAllBytes((Join-Path $outSource 'helengine_ps3.self'), [byte[]]@(1,2,3))
            [IO.File]::WriteAllBytes((Join-Path $outSource 'helengine_ps3.map'), [byte[]]@(4,5,6))
            exit 0
        }
        'success' {
            [IO.File]::WriteAllBytes((Join-Path $outSource 'helengine_ps3.elf'), [byte[]]@(0x7f,0x45,0x4c,0x46,1,2,3,4))
            [IO.File]::WriteAllBytes((Join-Path $outSource 'helengine_ps3.self'), [byte[]]@(0x53,0x43,0x45,0,9,9,9))
            [IO.File]::WriteAllBytes((Join-Path $outSource 'helengine_ps3.map'), [Text.Encoding]::ASCII.GetBytes('fake map'))
            New-Item -ItemType Directory -Path (Join-Path $outSource 'shaders') -Force | Out-Null
            [IO.File]::WriteAllBytes((Join-Path $outSource 'shaders\unlit.vpo'), [byte[]]@(7,7,7))
            [IO.File]::WriteAllBytes((Join-Path $outSource 'shaders\unlit.fpo'), [byte[]]@(8,8,8))
            Write-Output 'fake docker: build ok'
            exit 0
        }
        default {
            Write-Error "fake docker: unknown scenario '$scenario'"
            exit 8
        }
    }
}
Write-Error "fake docker: unsupported invocation: $($arguments -join ' ')"
exit 7
'@

# --- Fake emulator -----------------------------------------------------------------------
Set-Content -LiteralPath $fakeEmulator -Encoding Ascii -Value @(
    '@echo off',
    'echo %* > "%~dp0launched.txt"',
    'exit /b 0'
)

$originalPath = $env:PATH
$env:PATH = "$fakeDockerDirectory;$originalPath"

function Assert-Rejected {
    <#
        Asserts that an action was rejected, and optionally that it was rejected for the
        stated reason. Without ExpectedMessage a case passes on any rejection, which can
        hide a call that failed for an unrelated reason such as a parameter-binding error
        rather than the validation under test.
    #>
    param([string]$Name, [scriptblock]$Action, [string]$ExpectedMessage = '')
    $rejected = $false
    $message = ''
    try {
        & $Action | Out-Null
    } catch {
        $rejected = $true
        $message = [string]$_.Exception.Message
    }
    if (-not $rejected) {
        $failures.Add("$Name was accepted.")
        return
    }
    if (Test-Path -LiteralPath $fakeLaunchMarker) {
        $failures.Add("$Name : emulator launched before validation.")
        Remove-Item -LiteralPath $fakeLaunchMarker -Force
        return
    }
    if (-not [string]::IsNullOrEmpty($ExpectedMessage) -and $message -notmatch $ExpectedMessage) {
        $failures.Add("$Name was rejected for the wrong reason. Expected a message matching '$ExpectedMessage' but got: $message")
        return
    }
    $passes.Add("$Name rejected: $message")
}

function Assert-True {
    param([string]$Name, [bool]$Condition, [string]$Detail = '')
    if ($Condition) { $passes.Add($Name) } else { $failures.Add("$Name failed. $Detail") }
}

try {
    $repoLocalScripts = @(
        'scripts\build.ps1',
        'scripts\test-native.ps1',
        'scripts\probe-core-abi.ps1',
        'scripts\register-platform.ps1',
        'scripts\launch_in_emulator.ps1'
    )
    foreach ($relativeScript in $repoLocalScripts) {
        $scriptText = Get-Content -LiteralPath (Join-Path $repoRoot $relativeScript) -Raw -Encoding UTF8
        Assert-True "No external PS3 default in $relativeScript" ($scriptText -notmatch 'C:\\dev\\helworks\\builds\\helengine-ps3')
    }

    # Existing BuildId is rejected before any build starts.
    $env:HELENGINE_PS3_TEST_SCENARIO = 'success'
    New-Item -ItemType Directory -Path (Join-Path $outputRoot 'existing-01') | Out-Null
    Assert-Rejected 'Existing BuildId' { & $buildScript -StateRoot $testStateRoot -BuildId 'existing-01' -Mode Clear -OutputRoot $outputRoot }
    Assert-True 'Existing BuildId left no manifest' (-not (Test-Path -LiteralPath (Join-Path $outputRoot 'existing-01\build-manifest.json')))

    # Nonzero build exit produces no manifest.
    $env:HELENGINE_PS3_TEST_SCENARIO = 'fail'
    Assert-Rejected 'Nonzero build' { & $buildScript -StateRoot $testStateRoot -BuildId 'fail-01' -Mode Clear -OutputRoot $outputRoot }
    Assert-True 'Nonzero build left no manifest' (-not (Test-Path -LiteralPath (Join-Path $outputRoot 'fail-01\build-manifest.json')))
    Assert-True 'Nonzero build kept its log' ((Test-Path -LiteralPath (Join-Path $outputRoot 'fail-01\build.log')) -and ((Get-Content -LiteralPath (Join-Path $outputRoot 'fail-01\build.log') -Raw) -match 'simulated compiler error'))

    # Core mode must reject a generated-core root that does not exist, before any container
    # starts. A missing root would otherwise surface as an obscure failure inside make.
    # Each expectation names the reason, so a case cannot pass because the parameter failed
    # to bind or because an unrelated error happened to be thrown first.
    $env:HELENGINE_PS3_TEST_SCENARIO = 'success'
    Assert-Rejected 'Core mode without generated root' { & $buildScript -StateRoot $testStateRoot -BuildId 'core-missing-01' -Mode Core -GeneratedCoreRoot (Join-Path $testsRoot 'absent-core') -OutputRoot $outputRoot } 'Generated Core root'
    Assert-True 'Core mode without generated root left no manifest' (-not (Test-Path -LiteralPath (Join-Path $outputRoot 'core-missing-01\build-manifest.json')))

    # A generated root that exists but holds no unity translation unit is equally invalid.
    $emptyCoreRoot = Join-Path $testsRoot 'empty-core'
    New-Item -ItemType Directory -Path $emptyCoreRoot -Force | Out-Null
    Assert-Rejected 'Core mode without a unity source' { & $buildScript -StateRoot $testStateRoot -BuildId 'core-empty-01' -Mode Core -GeneratedCoreRoot $emptyCoreRoot -OutputRoot $outputRoot } 'unity'
    Assert-True 'Core mode without a unity source left no manifest' (-not (Test-Path -LiteralPath (Join-Path $outputRoot 'core-empty-01\build-manifest.json')))

    # The parameter is meaningless outside Core mode and must be refused rather than ignored.
    Assert-Rejected 'Generated root outside Core mode' { & $buildScript -StateRoot $testStateRoot -BuildId 'core-misuse-01' -Mode Cube -GeneratedCoreRoot $emptyCoreRoot -OutputRoot $outputRoot } 'only valid in Core mode'

    # A Core failure point outside Core mode would compile away with no Core present, so the
    # build would appear to honour a request it silently dropped.
    Assert-Rejected 'Core failure point outside Core mode' { & $buildScript -StateRoot $testStateRoot -BuildId 'core-fp-misuse-01' -Mode Cube -CoreFailurePoint 'DuringCoreDraw' -OutputRoot $outputRoot } '-CoreFailurePoint is only valid in Core mode'
    Assert-True 'Core failure point outside Core mode left no manifest' (-not (Test-Path -LiteralPath (Join-Path $outputRoot 'core-fp-misuse-01\build-manifest.json')))

    # A successful Core-mode build must fingerprint the generated tree and record it. This
    # ran only during a real build before, so a failure in it was invisible to these tests
    # until a ten-minute compile had already completed.
    $fakeCoreRoot = Join-Path $testsRoot 'fake-core'
    New-Item -ItemType Directory -Path (Join-Path $fakeCoreRoot 'runtime') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $fakeCoreRoot 'helengine_core_unity.cpp') -Value '#include "runtime/thing.cpp"' -Encoding Ascii
    Set-Content -LiteralPath (Join-Path $fakeCoreRoot 'runtime\thing.cpp') -Value 'int thing() { return 1; }' -Encoding Ascii
    & $buildScript -StateRoot $testStateRoot -BuildId 'core-ok-01' -Mode Core -GeneratedCoreRoot $fakeCoreRoot -OutputRoot $outputRoot | Out-Null
    $coreManifestPath = Join-Path $outputRoot 'core-ok-01\build-manifest.json'
    Assert-True 'Core mode wrote a manifest' (Test-Path -LiteralPath $coreManifestPath)
    if (Test-Path -LiteralPath $coreManifestPath) {
        $coreManifest = Get-Content -LiteralPath $coreManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
        Assert-True 'Core manifest records the mode' ($coreManifest.Mode -eq 'Core')
        Assert-True 'Core manifest records the unity file' ($coreManifest.GeneratedCoreUnity -eq 'helengine_core_unity.cpp')
        Assert-True 'Core manifest records the Core failure point' ($coreManifest.CoreFailurePoint -eq 'None')
        Assert-True 'Core manifest counts generated files' ($coreManifest.GeneratedCoreFileCount -eq 2)
        Assert-True 'Core manifest fingerprints the tree' ($coreManifest.GeneratedCoreSha256 -match '^[0-9a-f]{64}$')
        Assert-True 'Core mode wrote the generated hash list' (Test-Path -LiteralPath (Join-Path $outputRoot 'core-ok-01\generated-core-hashes.txt'))
    }

    # Zero-byte output is rejected even though the container exited zero.
    $env:HELENGINE_PS3_TEST_SCENARIO = 'zero-elf'
    Assert-Rejected 'Zero-byte ELF' { & $buildScript -StateRoot $testStateRoot -BuildId 'zero-01' -Mode Clear -OutputRoot $outputRoot }
    Assert-True 'Zero-byte ELF left no manifest' (-not (Test-Path -LiteralPath (Join-Path $outputRoot 'zero-01\build-manifest.json')))

    # Successful build in a path with spaces writes a complete manifest.
    $env:HELENGINE_PS3_TEST_SCENARIO = 'success'
    & $buildScript -StateRoot $testStateRoot -BuildId 'spaces-01' -Mode Cube -FixedPose -FixedPoseRadians 1.5708 -RunSeconds 30 -FailurePoint AfterRenderer -OutputRoot $outputRoot | Out-Null
    $successRoot = Join-Path $outputRoot 'spaces-01'
    $manifestPath = Join-Path $successRoot 'build-manifest.json'
    Assert-True 'Success manifest exists' (Test-Path -LiteralPath $manifestPath)
    Assert-True 'Temporary manifest was renamed' (-not (Test-Path -LiteralPath (Join-Path $successRoot 'build-manifest.json.tmp')))
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-True 'Manifest records mode settings' ($manifest.Mode -eq 'Cube' -and $manifest.FixedPose -eq $true -and $manifest.RunSeconds -eq 30 -and $manifest.FailurePoint -eq 'AfterRenderer')
    Assert-True 'Manifest records lock hash' ($manifest.ToolchainLockSha256 -eq (Get-FileHash -LiteralPath $lockPath -Algorithm SHA256).Hash.ToLowerInvariant())
    Assert-True 'Manifest records shader artifacts' (@($manifest.Artifacts | Where-Object { $_.Path -eq 'shaders/unlit.vpo' }).Count -eq 1)
    $dockerArgsText = Get-Content -LiteralPath (Join-Path $successRoot 'fake-docker-args.txt') -Raw
    Assert-True 'Build passed explicit definitions' ($dockerArgsText -match 'HELENGINE_PS3_FIXED_POSE=1' -and $dockerArgsText -match 'HELENGINE_PS3_FIXED_POSE_RADIANS=1\.5708f' -and $dockerArgsText -match 'HELENGINE_PS3_RUN_SECONDS=30' -and $dockerArgsText -match 'HELENGINE_PS3_FAILURE_POINT=2' -and $dockerArgsText -match 'HELENGINE_PS3_CLEAR_ONLY=0')
    Assert-True 'Manifest records the fixed pose' ($manifest.FixedPoseRadians -eq 1.5708)
    Assert-True 'Build disabled networking' ($dockerArgsText -match '--network\nnone')
    Assert-True 'Build mounted source read-only' ($dockerArgsText -match 'target=/src,readonly')
    Assert-True 'Build mounted Cg read-only' ($dockerArgsText -match 'target=/opt/cg,readonly')
    Assert-True 'Build mounted the isolated test Cg toolkit' ($dockerArgsText -match [regex]::Escape($testStateRoot))

    # The default output root is the repository-local .ps3/builds directory.
    $defaultBuildId = "default-local-$testId"
    & $buildScript -StateRoot $testStateRoot -BuildId $defaultBuildId -Mode Cube | Out-Null
    $defaultBuildRoot = Join-Path $testStateRoot "builds\$defaultBuildId"
    Assert-True 'Default build root is repo-local' (Test-Path -LiteralPath (Join-Path $defaultBuildRoot 'build-manifest.json'))
    $defaultDockerArgsText = Get-Content -LiteralPath (Join-Path $defaultBuildRoot 'fake-docker-args.txt') -Raw
    Assert-True 'Default build passed repo-local output mount' ($defaultDockerArgsText -match [regex]::Escape('target=/out'))
    $repoCgLibraryHashAfter = if (Test-Path -LiteralPath $repoCgLibrary -PathType Leaf) {
        (Get-FileHash -LiteralPath $repoCgLibrary -Algorithm SHA256).Hash.ToLowerInvariant()
    } else {
        $null
    }
    Assert-True 'Build contract tests preserve the repository Cg toolkit' ($repoCgLibraryHashAfter -eq $repoCgLibraryHashBefore)

    $artifactPath = Join-Path $successRoot 'helengine_ps3.self'

    # Launcher negatives: none may start the emulator.
    Assert-Rejected 'Missing artifact' { & $launcherScript -ArtifactPath (Join-Path $successRoot 'missing.self') -RpcS3Path $fakeEmulator -OutputRoot $outputRoot }
    Assert-Rejected 'Wrong artifact name' { & $launcherScript -ArtifactPath (Join-Path $successRoot 'helengine_ps3.elf') -RpcS3Path $fakeEmulator -OutputRoot $outputRoot }
    Assert-Rejected 'Artifact outside build root' { & $launcherScript -ArtifactPath $fakeEmulator -RpcS3Path $fakeEmulator -OutputRoot $outputRoot }
    Assert-Rejected 'Missing emulator' { & $launcherScript -ArtifactPath $artifactPath -RpcS3Path (Join-Path $fakeEmulatorDirectory 'absent.exe') -OutputRoot $outputRoot }

    $noManifestRoot = Join-Path $outputRoot 'nomanifest-01'
    Copy-Item -LiteralPath $successRoot -Destination $noManifestRoot -Recurse
    Remove-Item -LiteralPath (Join-Path $noManifestRoot 'build-manifest.json')
    Assert-Rejected 'Missing manifest' { & $launcherScript -ArtifactPath (Join-Path $noManifestRoot 'helengine_ps3.self') -RpcS3Path $fakeEmulator -OutputRoot $outputRoot }

    $renamedRoot = Join-Path $outputRoot 'renamed-01'
    Copy-Item -LiteralPath $successRoot -Destination $renamedRoot -Recurse
    Assert-Rejected 'Manifest BuildId mismatch' { & $launcherScript -ArtifactPath (Join-Path $renamedRoot 'helengine_ps3.self') -RpcS3Path $fakeEmulator -OutputRoot $outputRoot }

    $tamperedRoot = Join-Path $outputRoot 'tamper-01'
    Copy-Item -LiteralPath $successRoot -Destination $tamperedRoot -Recurse
    $tamperedManifest = Get-Content -LiteralPath (Join-Path $tamperedRoot 'build-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $tamperedManifest.BuildId = 'tamper-01'
    [IO.File]::WriteAllText((Join-Path $tamperedRoot 'build-manifest.json'), ($tamperedManifest | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllBytes((Join-Path $tamperedRoot 'helengine_ps3.self'), [byte[]]@(0x53,0x43,0x45,0,9,9,8))
    Assert-Rejected 'Wrong artifact hash' { & $launcherScript -ArtifactPath (Join-Path $tamperedRoot 'helengine_ps3.self') -RpcS3Path $fakeEmulator -OutputRoot $outputRoot }

    $staleRoot = Join-Path $outputRoot 'stale-01'
    Copy-Item -LiteralPath $successRoot -Destination $staleRoot -Recurse
    $staleManifest = Get-Content -LiteralPath (Join-Path $staleRoot 'build-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $staleManifest.BuildId = 'stale-01'
    $staleManifest.ToolchainLockSha256 = ('0' * 64)
    [IO.File]::WriteAllText((Join-Path $staleRoot 'build-manifest.json'), ($staleManifest | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
    Assert-Rejected 'Stale toolchain lock' { & $launcherScript -ArtifactPath (Join-Path $staleRoot 'helengine_ps3.self') -RpcS3Path $fakeEmulator -OutputRoot $outputRoot }

    # Launcher positive: valid artifact in a path with spaces launches the emulator with a quoted path.
    & $launcherScript -ArtifactPath $artifactPath -RpcS3Path $fakeEmulator -OutputRoot $outputRoot | Out-Null
    Assert-True 'Valid artifact launched emulator' (Test-Path -LiteralPath $fakeLaunchMarker)
    if (Test-Path -LiteralPath $fakeLaunchMarker) {
        $markerText = (Get-Content -LiteralPath $fakeLaunchMarker -Raw).Trim()
        Assert-True 'Launcher quoted the artifact path' ($markerText -match '--headless' -and $markerText.Contains('"' + $artifactPath + '"')) "Marker: $markerText"
        Remove-Item -LiteralPath $fakeLaunchMarker -Force
    }
    $runDirectories = @(Get-ChildItem -LiteralPath (Join-Path $successRoot 'emulator-runs') -Directory)
    Assert-True 'Launcher recorded a run directory' ($runDirectories.Count -eq 1 -and (Test-Path -LiteralPath (Join-Path $runDirectories[0].FullName 'emulator-command.txt')))

    & $launcherScript -ArtifactPath (Join-Path $defaultBuildRoot 'helengine_ps3.self') -RpcS3Path $fakeEmulator -OutputRoot (Join-Path $testStateRoot 'builds') | Out-Null
    Assert-True 'Launcher accepts the isolated build state root' (Test-Path -LiteralPath $fakeLaunchMarker)
    if (Test-Path -LiteralPath $fakeLaunchMarker) { Remove-Item -LiteralPath $fakeLaunchMarker -Force }
}
finally {
    $env:PATH = $originalPath
    Remove-Item Env:HELENGINE_PS3_TEST_SCENARIO -ErrorAction SilentlyContinue
}

Write-Output "Script contract tests under $testsRoot"
foreach ($pass in $passes) { Write-Output "PASS $pass" }
foreach ($failure in $failures) { Write-Output "FAIL $failure" }
Write-Output "$($passes.Count) passed, $($failures.Count) failed."
if ($failures.Count -gt 0) {
    throw "$($failures.Count) script contract test(s) failed."
}
