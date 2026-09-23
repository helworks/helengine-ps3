# Task5 full uncommitted source review package

Base HEAD ef27238ed85fe3218618a752c11d9eeede0a75a2; no commits. All five files below are new; full source is their added-file diff. Requirements task-5-brief.md, report task-5-report.md. Acceptance run17e32b778ea047cfbcfaa6ab45c235b1:98/98 non-Docker,7/7 Docker. Final command log acceptance-command-task5-final.log in standard shader-integration-20260923 build root. Existing shader product code was not changed in Task5. Pre-review parent findings about stale TRX and unused class definitions addressed with regressions.

### scripts/tests/ps3-hlsl-compiler.tests.ps1
```powershell
param(
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'ps3-hlsl-compiler-acceptance.helpers.ps1')
$outputRootFull = Assert-Ps3AcceptanceOutputRoot -Candidate $OutputRoot -RepositoryRoot $repoRoot

$engineRoot = [Environment]::GetEnvironmentVariable('HELENGINE_ROOT')
if ([string]::IsNullOrWhiteSpace($engineRoot)) { $engineRoot = Join-Path (Split-Path -Parent $repoRoot) 'helengine' }
$engineProject = Join-Path $engineRoot 'engine/helengine.baseplatform/helengine.baseplatform.csproj'
if (-not (Test-Path -LiteralPath $engineProject -PathType Leaf)) {
    throw "Sibling helengine checkout is missing. Set HELENGINE_ROOT to its root; expected $engineProject"
}

$toolchain = Assert-Ps3AcceptanceToolchain -RepositoryRoot $repoRoot
if (-not [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Windows) -or -not [Environment]::Is64BitProcess) {
    throw 'PS3 source compiler acceptance requires a Windows x64 host with the packaged SPIRV-Cross frontend.'
}

$dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
if ($null -eq $dotnet) { throw 'The .NET 9 SDK command dotnet is not available on PATH.' }
$sdkList = & $dotnet.Source --list-sdks 2>&1
if ($LASTEXITCODE -ne 0 -or -not ($sdkList | Where-Object { $_ -match '^9\.' })) {
    throw 'The .NET 9 SDK is not installed. Acceptance does not download SDKs or prepare the PS3 Docker image.'
}
$testProject = Join-Path $repoRoot 'builder.tests/helengine.ps3.builder.tests.csproj'
$dotnetArtifacts = Join-Path $outputRootFull 'dotnet'
$runId = [Guid]::NewGuid().ToString('N')
$runPaths = Get-Ps3AcceptanceRunPaths -OutputRoot $outputRootFull -RunId $runId
$shaderArtifacts = Join-Path $outputRootFull 'shader-artifacts'
foreach ($directory in @($outputRootFull, $dotnetArtifacts, $runPaths.RunRoot, $runPaths.PureResults, $runPaths.DockerResults, $shaderArtifacts)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

$env:HELENGINE_PS3_REPOSITORY_ROOT = $repoRoot
$env:HELENGINE_PS3_SHADER_TEST_OUTPUT = $shaderArtifacts
$env:HELENGINE_ROOT = [IO.Path]::GetFullPath($engineRoot)

$commonArguments = @('test', $testProject, '--artifacts-path', $dotnetArtifacts, '--verbosity', 'quiet')
$runs = @(
    [PSCustomObject]@{
        Name = 'pure'
        Filter = 'Category!=Docker'
        Results = $runPaths.PureResults
        Report = $runPaths.PureReport
        Log = $runPaths.PureLog
        RequiredClasses = @(
            'Ps3DockerShaderCompilerTests',
            'Ps3RsxProgramReaderTests',
            'Ps3ShaderBindingMapTests',
            'Ps3ShaderPairCompilerTests',
            'Ps3ShaderReflectionTests',
            'Ps3ShaderSourceCompilerTests',
            'Ps3ShaderTargetCompatibilityTests'
        )
    }
    [PSCustomObject]@{
        Name = 'docker'
        Filter = 'Category=Docker'
        Results = $runPaths.DockerResults
        Report = $runPaths.DockerReport
        Log = $runPaths.DockerLog
        RequiredClasses = @('Ps3DockerShaderCompilerTests', 'Ps3ShaderPairCompilerTests')
    }
)

$validatedRuns = @()
foreach ($run in $runs) {
    New-Item -ItemType Directory -Path $run.Results -Force | Out-Null
    $arguments = @($commonArguments + @(
        '--filter', $run.Filter,
        '--logger', "trx;LogFileName=$($run.Name).trx",
        '--results-directory', $run.Results
    ))
    & $dotnet.Source @arguments *> $run.Log
    $processExitCode = $LASTEXITCODE
    $reportSummary = Assert-Ps3AcceptanceTestReport -ExitCode $processExitCode -ReportPath $run.Report -RequiredClasses $run.RequiredClasses
    $validatedRuns += [PSCustomObject]@{ Name = $run.Name; Summary = $reportSummary; Log = $run.Log; Report = $run.Report }
}

Write-Output "PS3 compiler acceptance passed."
Write-Output "Pinned image: $($toolchain.ImageId)"
Write-Output "Pinned compiler: $($toolchain.CompilerPath)"
Write-Output "Cg library SHA-256: $($toolchain.CgSha256)"
Write-Output "Run ID: $runId"
foreach ($run in $validatedRuns) {
    Write-Output "$($run.Name): $($run.Summary.Passed)/$($run.Summary.Total) passed; report=$($run.Report); log=$($run.Log)"
}
Write-Output "Output root: $outputRootFull"

```

### scripts/tests/ps3-hlsl-compiler-contract.tests.ps1
```powershell
param(
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    throw 'Pass -OutputRoot as an explicit child of the sibling builds/helengine-ps3 directory.'
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputRootFull = [IO.Path]::GetFullPath($OutputRoot)
$buildRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $repoRoot) 'builds/helengine-ps3'))
$buildRootPrefix = $buildRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if ([string]::Equals($buildRoot, $outputRootFull, [StringComparison]::OrdinalIgnoreCase) -or -not $outputRootFull.StartsWith($buildRootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputRoot must be a strict descendant of the sibling builds/helengine-ps3 directory.'
}

$helperPath = Join-Path $PSScriptRoot 'ps3-hlsl-compiler-acceptance.helpers.ps1'
if (-not (Test-Path -LiteralPath $helperPath -PathType Leaf)) {
    throw "Acceptance helper is missing: $helperPath"
}
. $helperPath

function Assert-Failure {
    param([scriptblock]$Action, [string]$ExpectedText)
    $failed = $false
    try { & $Action } catch {
        $failed = $true
        if ($_.Exception.Message -notlike "*$ExpectedText*") { throw }
    }
    if (-not $failed) { throw "Expected failure containing '$ExpectedText'." }
}

$before = Get-ChildItem Env: | ForEach-Object { "$($_.Name)=$($_.Value)" } | Sort-Object
$fixturesRoot = Join-Path $outputRootFull 'contract-fixtures'
New-Item -ItemType Directory -Path $fixturesRoot -Force | Out-Null

# Missing OutputRoot must fail in a noninteractive child process rather than prompt.
$missingOutput = Start-Process -FilePath 'powershell.exe' -ArgumentList @(
    '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File',
    (Join-Path $PSScriptRoot 'ps3-hlsl-compiler.tests.ps1')
) -Wait -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $fixturesRoot 'missing-output.stdout.log') -RedirectStandardError (Join-Path $fixturesRoot 'missing-output.stderr.log')
if ($missingOutput.ExitCode -eq 0) { throw 'Acceptance unexpectedly succeeded without -OutputRoot.' }

$fixtureRepo = Join-Path $fixturesRoot 'repo'
$fixtureCg = Join-Path $fixtureRepo 'cg/usr/lib64/libCg.so'
New-Item -ItemType Directory -Path (Split-Path -Parent $fixtureCg) -Force | Out-Null
[IO.File]::WriteAllBytes($fixtureCg, [byte[]](1, 2, 3, 4))
$fixtureDigest = (Get-FileHash -LiteralPath $fixtureCg -Algorithm SHA256).Hash.ToLowerInvariant()
$validLock = [ordered]@{
    ResolvedImage = [ordered]@{ ImageId = 'sha256:' + ('a' * 64); CgcompPath = '/opt/ps3dev/bin/cgcomp' }
    ShaderToolkit = [ordered]@{ MountSource = 'cg/usr'; MountTarget = '/opt/cg'; LibraryPath = 'lib64/libCg.so'; LibCgSha256 = $fixtureDigest }
}
$lockPath = Join-Path $fixtureRepo 'toolchain-lock.json'
New-Item -ItemType Directory -Path $fixtureRepo -Force | Out-Null
$validLock | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $lockPath -Encoding UTF8

Assert-Ps3AcceptanceToolchain -RepositoryRoot $fixtureRepo
$invalidLock = $validLock | ConvertTo-Json -Depth 5 | ConvertFrom-Json
$invalidLock.ResolvedImage.ImageId = 'mutable-tag'
$invalidLock | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $lockPath -Encoding UTF8
Assert-Failure { Assert-Ps3AcceptanceToolchain -RepositoryRoot $fixtureRepo } 'Pinned Docker image identity'

$validLock | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $lockPath -Encoding UTF8
[IO.File]::WriteAllBytes($fixtureCg, [byte[]](4, 3, 2, 1))
Assert-Failure { Assert-Ps3AcceptanceToolchain -RepositoryRoot $fixtureRepo } 'SHA-256 mismatch'

$trxPath = Join-Path $fixturesRoot 'passed.trx'
@'
<TestRun xmlns="http://microsoft.com/schemas/VisualStudio/TeamTest/2010">
  <Results><UnitTestResult testId="shader" testName="Ps3ShaderTargetCompatibilityTests.Passes" outcome="Passed" /></Results>
  <TestDefinitions>
    <UnitTest id="shader"><TestMethod className="helengine.ps3.builder.tests.Ps3ShaderTargetCompatibilityTests" /></UnitTest>
    <UnitTest id="unused-reader"><TestMethod className="helengine.ps3.builder.tests.Ps3RsxProgramReaderTests" /></UnitTest>
  </TestDefinitions>
  <ResultSummary outcome="Completed"><Counters total="1" executed="1" passed="1" failed="0" /></ResultSummary>
</TestRun>
'@ | Set-Content -LiteralPath $trxPath -Encoding UTF8
$validSummary = Assert-Ps3AcceptanceTestReport -ExitCode 0 -ReportPath $trxPath -RequiredClasses @('Ps3ShaderTargetCompatibilityTests')
if ($validSummary.Total -ne 1 -or $validSummary.Passed -ne 1) { throw 'Completed TRX reports with all tests passed must be accepted.' }
Assert-Failure { Assert-Ps3AcceptanceTestReport -ExitCode 7 -ReportPath $trxPath -RequiredClasses @('Ps3ShaderTargetCompatibilityTests') } 'exit code 7'
Assert-Failure { Assert-Ps3AcceptanceTestReport -ExitCode 0 -ReportPath (Join-Path $fixturesRoot 'missing.trx') -RequiredClasses @('Ps3ShaderTargetCompatibilityTests') } 'missing'
Assert-Failure { Assert-Ps3AcceptanceTestReport -ExitCode 0 -ReportPath $trxPath -RequiredClasses @('Ps3RsxProgramReaderTests') } 'no executed Passed result'

# A stale passing report at the former static path cannot satisfy a new invocation with no report.
$staleReportPath = Join-Path $fixturesRoot 'results/pure/pure.trx'
New-Item -ItemType Directory -Path (Split-Path -Parent $staleReportPath) -Force | Out-Null
Copy-Item -LiteralPath $trxPath -Destination $staleReportPath -Force
$invocationPaths = Get-Ps3AcceptanceRunPaths -OutputRoot $fixturesRoot -RunId 'new-no-report-run'
if (Test-Path -LiteralPath $staleReportPath -PathType Leaf) {
    Assert-Failure { Assert-Ps3AcceptanceTestReport -ExitCode 0 -ReportPath $invocationPaths.PureReport -RequiredClasses @('Ps3ShaderTargetCompatibilityTests') } 'missing'
}

$emptyTrxPath = Join-Path $fixturesRoot 'empty.trx'
@'
<TestRun xmlns="http://microsoft.com/schemas/VisualStudio/TeamTest/2010">
  <Results /><TestDefinitions /><ResultSummary outcome="Passed"><Counters total="0" executed="0" passed="0" failed="0" /></ResultSummary>
</TestRun>
'@ | Set-Content -LiteralPath $emptyTrxPath -Encoding UTF8
Assert-Failure { Assert-Ps3AcceptanceTestReport -ExitCode 0 -ReportPath $emptyTrxPath -RequiredClasses @('Ps3ShaderTargetCompatibilityTests') } 'zero tests'
Assert-Failure { Assert-Ps3AcceptanceTestReport -ExitCode 0 -ReportPath $trxPath -RequiredClasses @('Ps3ShaderTargetCompatibilityTests', 'Ps3RsxProgramReaderTests') } 'required test class'

$after = Get-ChildItem Env: | ForEach-Object { "$($_.Name)=$($_.Value)" } | Sort-Object
if (Compare-Object $before $after) { throw 'Dot-sourcing acceptance helpers changed the caller process environment.' }
Write-Output 'PS3 HLSL compiler acceptance contract tests passed.'

```

### scripts/tests/ps3-hlsl-compiler-acceptance.helpers.ps1
```powershell
function Assert-Ps3AcceptanceOutputRoot {
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$Candidate,
        [Parameter(Mandatory)][string]$RepositoryRoot
    )

    if ([string]::IsNullOrWhiteSpace($Candidate) -or -not [IO.Path]::IsPathRooted($Candidate)) {
        throw 'OutputRoot must be an explicit absolute path.'
    }
    if ([string]::IsNullOrWhiteSpace($RepositoryRoot) -or -not [IO.Path]::IsPathRooted($RepositoryRoot)) {
        throw 'Repository root must be an absolute path.'
    }

    $fullRepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
    $repositoryParent = Split-Path -Parent $fullRepositoryRoot
    if ([string]::IsNullOrWhiteSpace($repositoryParent)) { throw 'Repository root must have a parent directory.' }
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $repositoryParent 'builds/helengine-ps3'))
    $fullCandidate = [IO.Path]::GetFullPath($Candidate)
    $buildRootPrefix = $buildRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if ([string]::Equals($buildRoot, $fullCandidate, [StringComparison]::OrdinalIgnoreCase) -or
        -not $fullCandidate.StartsWith($buildRootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'OutputRoot must be a strict descendant of the sibling builds/helengine-ps3 directory.'
    }
    return $fullCandidate
}

function Assert-Ps3AcceptanceToolchain {
    param([Parameter(Mandatory)][string]$RepositoryRoot)

    $fullRepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
    $lockPath = Join-Path $fullRepositoryRoot 'toolchain-lock.json'
    if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) { throw "Pinned toolchain lock is missing: $lockPath" }
    try { $lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json }
    catch { throw "Pinned toolchain lock is invalid JSON: $($_.Exception.Message)" }

    $imageId = [string]$lock.ResolvedImage.ImageId
    if ($imageId -notmatch '^sha256:[0-9a-fA-F]{64}$') {
        throw 'Pinned Docker image identity must be sha256 followed by exactly 64 hexadecimal characters.'
    }
    $compilerPath = [string]$lock.ResolvedImage.CgcompPath
    if (-not $compilerPath.StartsWith('/', [StringComparison]::Ordinal) -or $compilerPath.Contains(',')) {
        throw 'Pinned cgcomp path must be an absolute container path.'
    }

    $mountSourceRelative = [string]$lock.ShaderToolkit.MountSource
    if ([string]::IsNullOrWhiteSpace($mountSourceRelative) -or [IO.Path]::IsPathRooted($mountSourceRelative)) {
        throw 'Locked Cg mount source must be a nonempty repository-relative path.'
    }
    $mountSource = [IO.Path]::GetFullPath((Join-Path $fullRepositoryRoot $mountSourceRelative))
    $repositoryPrefix = $fullRepositoryRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $mountSource.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Locked Cg mount source escapes the repository root.'
    }
    if (-not (Test-Path -LiteralPath $mountSource -PathType Container)) { throw "Locked Cg mount source is missing: $mountSource" }

    $libraryRelative = [string]$lock.ShaderToolkit.LibraryPath
    if ([string]::IsNullOrWhiteSpace($libraryRelative) -or [IO.Path]::IsPathRooted($libraryRelative)) {
        throw 'Locked Cg library path must be relative to the mount source.'
    }
    $libraryPath = [IO.Path]::GetFullPath((Join-Path $mountSource ($libraryRelative -replace '/', [IO.Path]::DirectorySeparatorChar)))
    $mountPrefix = $mountSource.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $libraryPath.StartsWith($mountPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Locked Cg library path escapes its mount source.'
    }
    if (-not (Test-Path -LiteralPath $libraryPath -PathType Leaf)) { throw "Locked Cg shared library is missing: $libraryPath" }

    $expectedDigest = [string]$lock.ShaderToolkit.LibCgSha256
    if ($expectedDigest -notmatch '^[0-9a-fA-F]{64}$') { throw 'Pinned Cg library digest must contain exactly 64 hexadecimal characters.' }
    $actualDigest = (Get-FileHash -LiteralPath $libraryPath -Algorithm SHA256).Hash
    if (-not [string]::Equals($expectedDigest, $actualDigest, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Mounted Cg library SHA-256 mismatch: expected $expectedDigest, found $actualDigest."
    }

    return [PSCustomObject]@{
        ImageId = $imageId
        CompilerPath = $compilerPath
        CgLibrary = $libraryPath
        CgSha256 = $actualDigest.ToLowerInvariant()
    }
}

function Get-Ps3AcceptanceRunPaths {
    param(
        [Parameter(Mandatory)][string]$OutputRoot,
        [Parameter(Mandatory)][string]$RunId
    )

    if ([string]::IsNullOrWhiteSpace($RunId) -or $RunId -notmatch '^[A-Za-z0-9-]+$') {
        throw 'Acceptance run ID must contain only letters, digits, and hyphens.'
    }
    $runRoot = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) (Join-Path 'runs' $RunId)
    return [PSCustomObject]@{
        RunRoot = $runRoot
        PureResults = Join-Path $runRoot 'results/pure'
        PureReport = Join-Path $runRoot 'results/pure/pure.trx'
        PureLog = Join-Path $runRoot 'pure-tests.log'
        DockerResults = Join-Path $runRoot 'results/docker'
        DockerReport = Join-Path $runRoot 'results/docker/docker.trx'
        DockerLog = Join-Path $runRoot 'docker-tests.log'
    }
}

function Assert-Ps3AcceptanceTestReport {
    param(
        [Parameter(Mandatory)][int]$ExitCode,
        [Parameter(Mandatory)][string]$ReportPath,
        [Parameter(Mandatory)][string[]]$RequiredClasses
    )

    if ($ExitCode -ne 0) { throw "Test process failed with exit code $ExitCode; see its acceptance log." }
    if (-not (Test-Path -LiteralPath $ReportPath -PathType Leaf)) { throw "Test result report is missing: $ReportPath" }
    try { [xml]$report = Get-Content -LiteralPath $ReportPath -Raw }
    catch { throw "Test result report is invalid XML: $($_.Exception.Message)" }

    $summary = $report.SelectSingleNode("//*[local-name()='ResultSummary']")
    if ($null -eq $summary) { throw "Test result report has no summary: $ReportPath" }
    $counters = $summary.SelectSingleNode("*[local-name()='Counters']")
    if ($null -eq $counters) { throw "Test result report has no summary counters: $ReportPath" }
    $total = [int]$counters.GetAttribute('total')
    $executed = [int]$counters.GetAttribute('executed')
    $passed = [int]$counters.GetAttribute('passed')
    $failed = [int]$counters.GetAttribute('failed')
    if ($total -le 0 -or $executed -le 0) { throw "Test result report contains zero tests: $ReportPath" }
    $summaryOutcome = $summary.GetAttribute('outcome')
    if ($summaryOutcome -notin @('Completed', 'Passed') -or $failed -ne 0 -or $passed -ne $total -or $executed -ne $total) {
        throw "Test result report is not fully passed: total=$total executed=$executed passed=$passed failed=$failed."
    }
    $testResults = @($report.SelectNodes("//*[local-name()='UnitTestResult']"))
    if ($testResults.Count -ne $executed -or @($testResults | Where-Object { $_.GetAttribute('outcome') -ne 'Passed' }).Count -ne 0) {
        throw "Test result report contains missing or non-passing test results: expected=$executed actual=$($testResults.Count)."
    }

    $unitTests = @($report.SelectNodes("//*[local-name()='UnitTest']"))
    foreach ($requiredClass in $RequiredClasses) {
        $classDefinitionFound = $false
        $classHasPassedResult = $false
        foreach ($unitTest in $unitTests) {
            $testMethod = $unitTest.SelectSingleNode("./*[local-name()='TestMethod']")
            if ($null -eq $testMethod) { continue }
            $className = $testMethod.GetAttribute('className')
            if ($className -ne $requiredClass -and -not $className.EndsWith(".$requiredClass", [StringComparison]::Ordinal)) { continue }
            $classDefinitionFound = $true
            $testId = $unitTest.GetAttribute('id')
            $linkedPassedResult = @($testResults | Where-Object {
                $_.GetAttribute('testId') -eq $testId -and $_.GetAttribute('outcome') -eq 'Passed'
            })
            if ($linkedPassedResult.Count -gt 0) { $classHasPassedResult = $true; break }
        }
        if (-not $classDefinitionFound) { throw "Test result report is missing required test class '$requiredClass'." }
        if (-not $classHasPassedResult) { throw "Required test class '$requiredClass' has no executed Passed result linked to its test definition." }
    }
    $classNames = @($unitTests | ForEach-Object {
        $testMethod = $_.SelectSingleNode("./*[local-name()='TestMethod']")
        if ($null -ne $testMethod) { $testMethod.GetAttribute('className') }
    } | Select-Object -Unique)
    return [PSCustomObject]@{ Total = $total; Passed = $passed; Classes = $classNames }
}

```

### builder.tests/Ps3ShaderTargetCompatibilityTests.cs
```csharp
using helengine;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Protects persisted shared target values and verifies the PS3 API define reaches shaderc.</summary>
public sealed class Ps3ShaderTargetCompatibilityTests {
    /// <summary>Confirms the appended PS3 target does not change existing serialized target values.</summary>
    [Fact]
    public void PreservesExistingTargetValuesAndAppendsPs3() {
        Assert.Equal(0, (int)ShaderCompileTarget.DirectX9);
        Assert.Equal(1, (int)ShaderCompileTarget.DirectX11);
        Assert.Equal(2, (int)ShaderCompileTarget.DirectX12);
        Assert.Equal(3, (int)ShaderCompileTarget.Vulkan);
        Assert.Equal(4, (int)ShaderCompileTarget.Metal);
        Assert.Equal(5, (int)ShaderCompileTarget.PsVita);
        Assert.Equal(6, (int)ShaderCompileTarget.WiiU);
        Assert.Equal(7, (int)ShaderCompileTarget.Ps3);
    }

    /// <summary>Confirms PS3 resolves to its persisted name and platform define.</summary>
    [Fact]
    public void ExposesPs3DescriptorNameAndDefine() {
        ShaderTargetDescriptor descriptor = ShaderTargetDescriptors.Get(ShaderCompileTarget.Ps3);

        Assert.Equal("ps3", descriptor.Name);
        Assert.Equal("HEL_API_PS3", descriptor.DefineName);
    }

    /// <summary>Confirms a target API define selects the matching shaderc preprocessor branch.</summary>
    [Fact]
    public void ForwardsPs3TargetDefineToShadercPreprocessor() {
        ShaderTargetDescriptor descriptor = ShaderTargetDescriptors.Get(ShaderCompileTarget.Ps3);
        Ps3ShaderIncludeResolverStub resolver = new() {
            Files = new Dictionary<string, ShaderIncludeResult> {
                ["ps3.hlsl"] = new("ps3.hlsl", "static const float4 PlatformColor = float4(0, 1, 0, 1);"),
                ["other.hlsl"] = new("other.hlsl", "static const float4 PlatformColor = float4(1, 0, 0, 1);")
            }
        };
        string source = $"#if {descriptor.DefineName}\n#include \"ps3.hlsl\"\n#else\n#include \"other.hlsl\"\n#endif\nfloat4 PS() : SV_Target {{ return PlatformColor; }}";
        ShaderCompileRequest request = new(new ShaderSourceInfo("target-define.hlsl", source), "TargetDefine", "PS", ShaderStage.Pixel,
            ShaderCompileTarget.Ps3, new ShaderModel(3, 0), "default", [new ShaderDefine(descriptor.DefineName, "1")],
            new ShaderCompileOptions(ShaderBindingPolicies.Default, false, true, false));

        string output = new Ps3ShaderSourceCompiler().Compile(request, resolver);

        Assert.Contains("void main()", output);
        Assert.Contains(resolver.Requests, include => include.IncludePath == "ps3.hlsl");
        Assert.DoesNotContain(resolver.Requests, include => include.IncludePath == "other.hlsl");
    }
}

```

### docs/Ps3ShaderCompiler.md
```markdown
# PS3 HLSL compiler acceptance

This repository contains native PS3 compiler infrastructure for translating a vertex and pixel HLSL request pair into RSX programs. It is not an enabled authored-shader rendering path: no material selection, cooker staging, native runtime shader selection, or platform feature enablement is included.

## Host and preparation requirements

Run the managed frontend on Windows x64 with the .NET 9 SDK installed. The frontend loads the packaged `runtimes/win-x64/native/spirv-cross.dll`; running the final SDK conversion in Linux Docker does not make this frontend portable to Linux. The managed builder projects also require the sibling `helengine` checkout at `C:\dev\helworks\helengine`, or an explicit `HelengineRoot` MSBuild property / `HELENGINE_ROOT` environment override.

Docker Desktop must be running Linux containers. The pinned PS3 SDK image and the verified NVIDIA Cg 3.1 files must already be prepared. A developer prepares them explicitly from this repository root with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/prepare_toolchain.ps1
```

That preparation command verifies/downloads the locked Cg archive when needed, extracts its `usr` tree under `.ps3\dependencies\cg`, verifies `libCg.so`, builds the pinned image, runs the SDK probe, and records its immutable image identity in `toolchain-lock.json`. Compiler acceptance does not invoke preparation, download a .NET SDK, or pull an SDK image. It validates the existing image lock and Cg library before testing. RPCS3 and PS3 firmware are not compiler-test prerequisites.

## Acceptance

Pass an explicit absolute output directory strictly below the sibling `builds\helengine-ps3` directory. For example:

```powershell
./scripts/tests/ps3-hlsl-compiler.tests.ps1 -OutputRoot C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/acceptance
```

The script resolves the repository from its own location, checks the shared engine checkout, validates the immutable Docker image ID and the Cg library SHA-256, then runs the builder suite in two reported groups: all tests without `Category=Docker`, followed by the Docker integration category. Each invocation gets a unique run ID and writes fresh TRX reports under its own `runs/<run-id>` directory, preserving reports from earlier invocations. Each run must exit successfully and produce a nonempty passing report containing the expected PS3 shader, RSX reader, Docker compiler, and pair-compiler classes. For each required class, the report validator follows `UnitTestResult.testId` to `UnitTest.id` and verifies that linked test result passed; an unused class definition cannot satisfy the requirement. Missing output-root input fails immediately without prompting. A process exit failure, absent/empty report, zero tests, failed test, or missing required executed class fails acceptance. The script prints the pinned compiler identity and report/log locations only after both reports pass.

The output directory contains the .NET artifacts under `dotnet`, invocation-specific TRX files and full process logs under `runs\<run-id>`, and Docker shader-job/test artifacts under `shader-artifacts`. The contract tests use an isolated `contract-fixtures` child. Generated outputs remain within the selected build ID. Acceptance paths must be strict descendants of the sibling build root; the build root itself is not a valid output directory.

## Translation and supported coverage

For each stage, the managed frontend uses shaderc's HLSL compiler and preprocessor, captures SPIR-V reflection, and uses the packaged SPIRV-Cross library to emit GLSL 1.20. Active `#include` directives are resolved through the shared include resolver and shaderc callbacks, including nested relative includes and conditional branches. Inactive includes are not resolved. Request defines are passed to shaderc before preprocessing; the `HEL_API_PS3` descriptor name is tested through a conditional include. The target descriptor exposes `ps3` / `HEL_API_PS3`, while callers supply request defines explicitly.

The accepted native pair path then converts generated vertex/pixel GLSL through `cgcomp` in the pinned Docker SDK. It reads the resulting SDK program binaries and compiler dumps, checks active vertex attributes, uniforms and samplers against SDK records, and reconciles authored inter-stage links by canonical Cg/RSX channel identity. The stages use their own SDK slots, which need not be numerically equal. Invalid or unsupported active routes fail explicitly rather than receiving invented defaults.

The task reports and tests establish vertex and pixel stages. Task 4's real Docker integration fixture uses a modern HLSL source with a `Texture2D`, two sampler identities, a constant-buffer member, `COLOR0` and `TEXCOORD0`; it compiles both stages and links those varyings through distinct Cg channels. Its active attributes, sampler units and fragment constant relocation are validated. This fixture is evidence for that concrete pair, not general HLSL interface support.

The binding mapper recognizes synthetic sampler dimension tokens for 1D, 2D, 3D, cube and rectangle resources, but only `Texture2D`/`Dim2D` has real HLSL-to-SDK integration coverage. The other dimension mappings are unit-level format mappings, not proof that modern HLSL declarations of those dimensions compile in the pinned SDK. Active user varyings are limited to one-column, non-array FP3232 scalar/vector values of width 1â€“4 with matching generated Cg declaration and cross-stage shape/interpolation. Vertex attributes must be non-array and map to a matching SDK attribute record. Active float scalar/vector constants and the documented Cg matrix forms (`float3x3`, `float3x4`, `float4x3`, `float4x4`) are decoded only when source reflection and SDK metadata agree. Optimized-out resources are not synthesized.

Integer/non-FP3232 inter-stage values, varying matrices and arrays, unverified active Cg routes, and untested sampler dimensions remain unsupported. A successful compile of an isolated program does not extend these limits.

## Pipeline boundary

```text
HLSL request + request defines + include resolver
                    |
                    v
        shaderc preprocessing and SPIR-V
                    |
                    v
     packaged SPIRV-Cross -> GLSL 1.20
                    |
                    v
      pinned Docker cgcomp / RSX SDK
                    |
                    v
     vertex/pixel binaries + Cg dumps
                    |
                    v
       validated pair and binding map
```

The generated SDK binaries and headers are consumed as produced and are never rewritten after compilation. This milestone stops at the validated compiler pair and binding map; runtime selection and rendering remain disabled.

```

