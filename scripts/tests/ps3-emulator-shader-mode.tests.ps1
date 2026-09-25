[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$launcherPath = Join-Path $repoRoot 'scripts\launch_in_emulator.ps1'
$tokens = $null
$parseErrors = $null
$launcherAst = [System.Management.Automation.Language.Parser]::ParseFile($launcherPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) {
    throw "Launcher must parse under Windows PowerShell 5: $($parseErrors[0])"
}

$configFunction = $launcherAst.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Get-Rpcs3RunConfigLines'
}, $true)
if ($null -eq $configFunction) {
    throw 'Launcher is missing Get-Rpcs3RunConfigLines.'
}
. ([scriptblock]::Create($configFunction.Extent.Text))

$defaultLines = @(Get-Rpcs3RunConfigLines)
$expectedDefault = @(
    'Video:',
    '  Write Color Buffers: true',
    'Miscellaneous:',
    '  Exit RPCS3 when process finishes: true',
    '  Start games in fullscreen mode: false',
    '  Show shader compilation hint: false',
    '  Show PPU compilation hint: false'
)
if ([string]::Join("`n", $defaultLines) -cne [string]::Join("`n", $expectedDefault)) {
    throw 'Default per-run RPCS3 configuration changed unexpectedly.'
}

$synchronousLines = @(Get-Rpcs3RunConfigLines -SynchronousShaders)
$expectedSynchronous = @(
    'Video:',
    '  Write Color Buffers: true',
    '  Shader Mode: Legacy Recompiler (single-threaded)',
    'Miscellaneous:',
    '  Exit RPCS3 when process finishes: true',
    '  Start games in fullscreen mode: false',
    '  Show shader compilation hint: false',
    '  Show PPU compilation hint: false'
)
if ([string]::Join("`n", $synchronousLines) -cne [string]::Join("`n", $expectedSynchronous)) {
    throw 'Synchronous mode must add only the exact per-run shader-mode override.'
}
if (@($synchronousLines | Where-Object { $_ -match '^\s*Shader Mode:' }).Count -ne 1) {
    throw 'Synchronous mode must emit exactly one shader-mode key.'
}

$launcherText = Get-Content -LiteralPath $launcherPath -Raw
if ($launcherText -notmatch '(?m)^\s*\[switch\]\$SynchronousShaders\s*,?$' -or
    $launcherText -notmatch 'SynchronousShaders=\$\(\[bool\]\$SynchronousShaders\)') {
    throw 'Launcher must expose and record the requested synchronous-mode switch.'
}

Write-Output 'PASS launcher-default-rpcs3-config-unchanged'
Write-Output 'PASS synchronous-mode-is-exact-per-run-yaml'
Write-Output 'PASS launcher-records-requested-shader-mode'
Write-Output 'PASS windows-powershell-5-parse'
