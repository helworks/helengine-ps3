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
