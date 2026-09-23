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
