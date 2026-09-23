# Task 2: Declared constant-buffer size reflection

## Files owned by this task

- `builder/shaders/Ps3ShaderUniformBuffer.cs`
- `builder/shaders/Ps3ShaderReflection.cs`
- `builder/shaders/Ps3ShaderTranslation.cs`
- `builder/shaders/Ps3ShaderSourceCompiler.cs`
- `builder.tests/Ps3ShaderUniformBufferTests.cs`
- `builder.tests/Ps3ShaderReflectionTests.cs`
- `builder.tests/Ps3ShaderBindingMapTests.cs`
- `builder.tests/Ps3DockerShaderCompilerTests.cs`
- `docs/Ps3ShaderCompiler.md`
- `docs/superpowers/execution/backend-foundations/task-2-report.md`

## Implementation

Added immutable `Ps3ShaderUniformBuffer` metadata for buffer name, SPIR-V binding and compiled declared byte size. Reflection queries `CompilerGetDeclaredStructSize` directly on each active resource's base type before generated renaming, then forwards a read-only buffer snapshot through `Ps3ShaderTranslation`. Duplicate bindings and unsupported descriptor spaces retain their existing failures. No leaf-size estimate, runtime upload claim, generated-source edit, or production constructor default was added.

The translation collection rejects null collections and null items. Tests cover DTO validation and collection snapshot behavior, real padded-buffer sizes in optimized and unoptimized modes, empty metadata for a constant-only shader, and the existing physical member layout assertions. Synthetic binding-map fixtures state their buffer metadata directly. Documentation identifies the compiled-structure size, separate physical member layout, pending runtime packing/upload, and the actual shared policy name `IShaderBackendCachePolicy.UseSharedCache`; it notes that a future PS3 backend must opt out until the shared cache identity covers relevant immutable inputs. No backend registration is claimed.

## Validation

RED used the required focused filter before implementation:

```powershell
dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations\dotnet --filter 'FullyQualifiedName~Ps3ShaderUniformBufferTests|FullyQualifiedName~Ps3ShaderReflectionTests' --verbosity quiet
```

- Result: expected build failure, exit code 1; `Ps3ShaderUniformBuffer` was not yet defined (`CS0246`), before test discovery.
- Log: `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations\task-2-red.log`

RED used this PowerShell wrapper; it restored the caller's process-local TEMP/TMP in `finally`:

```powershell
$ErrorActionPreference = 'Stop'
$buildRoot = 'C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations'
$fixtureRoot = Join-Path $buildRoot 'fixture-storage'
New-Item -ItemType Directory -Force -Path $fixtureRoot | Out-Null
$oldTemp = $env:TEMP; $oldTmp = $env:TMP
try {
    $env:TEMP = $fixtureRoot; $env:TMP = $fixtureRoot
    dotnet test builder.tests/helengine.ps3.builder.tests.csproj `
        --artifacts-path "$buildRoot\dotnet" `
        --filter 'FullyQualifiedName~Ps3ShaderUniformBufferTests|FullyQualifiedName~Ps3ShaderReflectionTests' `
        --verbosity quiet *> "$buildRoot\task-2-red.log"
    $testExit = $LASTEXITCODE
    Write-Output "TEST_EXIT_CODE=$testExit"
    exit $testExit
} finally {
    $env:TEMP = $oldTemp; $env:TMP = $oldTmp
}
```

The focused GREEN filter added binding-map and RSX program-reader tests:

```powershell
dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations\dotnet --filter 'FullyQualifiedName~Ps3ShaderUniformBufferTests|FullyQualifiedName~Ps3ShaderReflectionTests|FullyQualifiedName~Ps3ShaderBindingMapTests|FullyQualifiedName~Ps3RsxProgramReaderTests' --logger 'trx;LogFileName=focused.trx' --results-directory C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations\focused-results --verbosity quiet
```

- Result: 62 passed, 0 failed, 0 skipped; exit code 0.
- stdout log: `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations\task-2-green-focused.out.log`
- stderr log: `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations\task-2-green-focused.err.log`
- TRX: `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations\focused-results\focused.trx`

Focused GREEN used the same `fixture-storage` TEMP/TMP values and a `finally` restoration around this separate dotnet process invocation (stdout and stderr were captured separately):

```powershell
$buildRoot='C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations'
$fixtureRoot=Join-Path $buildRoot 'fixture-storage'
$resultRoot=Join-Path $buildRoot 'focused-results'
New-Item -ItemType Directory -Force -Path $fixtureRoot,$resultRoot | Out-Null
$oldTemp=$env:TEMP; $oldTmp=$env:TMP
try {
    $env:TEMP=$fixtureRoot; $env:TMP=$fixtureRoot
    $p=Start-Process -FilePath 'dotnet' -ArgumentList @(
        'test','builder.tests/helengine.ps3.builder.tests.csproj',
        '--artifacts-path',"$buildRoot\dotnet",
        '--filter','FullyQualifiedName~Ps3ShaderUniformBufferTests|FullyQualifiedName~Ps3ShaderReflectionTests|FullyQualifiedName~Ps3ShaderBindingMapTests|FullyQualifiedName~Ps3RsxProgramReaderTests',
        '--logger','trx;LogFileName=focused.trx','--results-directory',$resultRoot,'--verbosity','quiet'
    ) -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput "$buildRoot\task-2-green-focused.out.log" `
        -RedirectStandardError "$buildRoot\task-2-green-focused.err.log"
    Write-Output "TEST_EXIT_CODE=$($p.ExitCode)"
    exit $p.ExitCode
} finally {
    $env:TEMP=$oldTemp; $env:TMP=$oldTmp
}
```

One intermediate focused run exited 1 with 61/62 passing. Its detailed failure showed the existing negative output-root test used `Path.GetTempPath()` as an outside-build candidate. The required TEMP/TMP redirection resolved that path to `...\backend-foundations\fixture-storage`, which correctly passed the unchanged production guard. Only that test candidate was changed to `Path.Combine(repositoryRoot, "path-probe")`; it remains outside the allowed build root and creates no files. The failing run is preserved in `task-2-green-focused.log`, with the isolated diagnostic at `focused-results\single.trx`. The corrected focused run passed all 62 tests.

Final compiler acceptance ran once in a separate PowerShell process with output root `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations\acceptance`:

- Result: pure suite 125/125 passed; Docker suite 7/7 passed; acceptance exit code 0.
- Pinned Docker image: `sha256:da929def76aacc0a5f499e1b8be143912062ab4a93b206277ead4de9530fe411`
- Pinned compiler: `/opt/ps3dev/bin/cgcomp`
- Cg library SHA-256: `01c8bbbd3c373cb5f337ab0fd7a252b9d01212d35ffc1e2ea2ea8de9d1e557d3`
- Run ID: `108367ae8a814e8f84795f548e4b8889`
- Pure report/log: `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations\acceptance\runs\108367ae8a814e8f84795f548e4b8889\results\pure\pure.trx`; `...\pure-tests.log`
- Docker report/log: `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations\acceptance\runs\108367ae8a814e8f84795f548e4b8889\results\docker\docker.trx`; `...\docker-tests.log`
- Separate-process wrapper stdout/stderr: `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations\task-2-acceptance.stdout.log`; `...\task-2-acceptance.stderr.log`

The exact TEMP/TMP setup, separate-process invocation, and restoration wrapper used for final acceptance was:

```powershell
$buildRoot='C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations'
$fixtureRoot=Join-Path $buildRoot 'fixture-storage'
$acceptanceRoot=Join-Path $buildRoot 'acceptance'
New-Item -ItemType Directory -Force -Path $fixtureRoot,$acceptanceRoot | Out-Null
$oldTemp=$env:TEMP; $oldTmp=$env:TMP
try {
    $env:TEMP=$fixtureRoot; $env:TMP=$fixtureRoot
    $repoRoot='C:\dev\helworks\helengine-ps3'
    $p=Start-Process -FilePath 'powershell.exe' -ArgumentList @(
        '-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',
        "$repoRoot\scripts\tests\ps3-hlsl-compiler.tests.ps1",
        '-OutputRoot',$acceptanceRoot
    ) -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput "$buildRoot\task-2-acceptance.stdout.log" `
        -RedirectStandardError "$buildRoot\task-2-acceptance.stderr.log"
    Write-Output "ACCEPTANCE_EXIT_CODE=$($p.ExitCode)"
    Get-Content "$buildRoot\task-2-acceptance.stdout.log" | Select-Object -Last 25
    Get-Content "$buildRoot\task-2-acceptance.stderr.log" | Select-Object -Last 12
    exit $p.ExitCode
} finally {
    $env:TEMP=$oldTemp; $env:TMP=$oldTmp
}
```

No commits were made. The pre-existing unrelated working-tree changes were preserved.
