<#
.SYNOPSIS
    Compiles and runs the SDK-independent cube geometry/transform tests with the host g++.

.DESCRIPTION
    Builds tests/native/Ps3CubeTests.cpp together with the SDK-free sources under
    src/platform/ps3 into a fresh repository-local .ps3/builds/<BuildId> directory,
    runs the resulting executable and
    propagates its exit code. Nothing is written to the worktree or Temp.

    Command contract:
        powershell -NoProfile -File scripts/test-native.ps1 -BuildId <id>
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')]
    [string]$BuildId,
    [string]$OutputRoot = '',
    [string]$HostCompiler = 'g++'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $repoRoot 'scripts\ps3-paths.ps1')
$OutputRoot = Resolve-Ps3OutputRoot -RepoRoot $repoRoot -RequestedRoot $OutputRoot
$buildRoot = Join-Path $OutputRoot $BuildId
if (Test-Path -LiteralPath $buildRoot) {
    throw "Refusing to reuse existing build directory: $buildRoot"
}
$compiler = Get-Command $HostCompiler -ErrorAction SilentlyContinue
if ($null -eq $compiler) {
    throw "Host compiler '$HostCompiler' is not on PATH."
}
New-Item -ItemType Directory -Path $buildRoot | Out-Null
$executable = Join-Path $buildRoot 'cube-tests.exe'
$logPath = Join-Path $buildRoot 'cube-tests.log'

$sources = @(
    'tests/native/Ps3CubeTests.cpp',
    'src/platform/ps3/Ps3CubeMesh.cpp',
    'src/platform/ps3/Ps3CubeTransform.cpp'
) | ForEach-Object { Join-Path $repoRoot ($_ -replace '/', '\') }
$compileArgs = @('-std=c++17', '-Wall', '-Wextra', '-Werror', '-I' + (Join-Path $repoRoot 'src')) + $sources + @('-o', $executable)
function ConvertTo-LineText {
    <#
        Converts one merged-stream item to text. Windows PowerShell 5.1 wraps native stderr
        lines in ErrorRecords whose string form is the exception type, so the message is used.
    #>
    param($Item)
    if ($Item -is [System.Management.Automation.ErrorRecord]) { return [string]$Item.Exception.Message }
    return [string]$Item
}

function Invoke-NativeMerged {
    <#
        Runs a native command with stdout and stderr merged and returns the lines; the
        caller reads $LASTEXITCODE. Windows PowerShell 5.1 turns redirected stderr lines
        into terminating errors under ErrorActionPreference=Stop, so it is relaxed here.
    #>
    param([string]$FilePath, [string[]]$ArgumentList)
    $ErrorActionPreference = 'Continue'
    return @(& $FilePath @ArgumentList 2>&1 | ForEach-Object { ConvertTo-LineText $_ })
}

$compilerVersion = ((Invoke-NativeMerged $compiler.Source @('--version') | Select-Object -First 1) -join '')
Set-Content -LiteralPath $logPath -Encoding UTF8 -Value @("# $($compiler.Source) $($compileArgs -join ' ')", "# compiler: $compilerVersion")
$compileOutput = Invoke-NativeMerged $compiler.Source $compileArgs
$compileExit = $LASTEXITCODE
$compileOutput | Add-Content -LiteralPath $logPath -Encoding UTF8
if ($compileExit -ne 0) {
    throw "Host compilation failed ($compileExit):`n$($compileOutput -join [Environment]::NewLine)"
}
if (-not (Test-Path -LiteralPath $executable -PathType Leaf) -or (Get-Item -LiteralPath $executable).Length -eq 0) {
    throw "Host compilation did not produce $executable"
}
$runOutput = Invoke-NativeMerged $executable @()
$runExit = $LASTEXITCODE
$runOutput | Add-Content -LiteralPath $logPath -Encoding UTF8
$runOutput | Write-Output
Add-Content -LiteralPath $logPath -Encoding UTF8 -Value "# exit code $runExit"
if ($runExit -ne 0) {
    throw "Cube tests failed with exit code $runExit. See $logPath"
}
Write-Output "Cube tests passed. Log: $logPath"
