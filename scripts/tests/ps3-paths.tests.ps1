<#
.SYNOPSIS
    Tests the repository-local PS3 state path contract.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$helperPath = Join-Path $repoRoot 'scripts\ps3-paths.ps1'
if (-not (Test-Path -LiteralPath $helperPath -PathType Leaf)) {
    throw "Path helper is missing: $helperPath"
}
. $helperPath

function Assert-Equal {
    param([string]$Name, $Actual, $Expected)
    if ($Actual -ne $Expected) {
        throw "$Name failed. Expected '$Expected', found '$Actual'."
    }
    Write-Output "PASS $Name"
}

function Assert-True {
    param([string]$Name, [bool]$Condition)
    if (-not $Condition) { throw "$Name failed." }
    Write-Output "PASS $Name"
}

$stateRoot = Resolve-Ps3StateRoot -RepoRoot $repoRoot
$expectedStateRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '.ps3'))
Assert-Equal 'Default state root is repo-local' $stateRoot $expectedStateRoot

$outputRoot = Resolve-Ps3OutputRoot -RepoRoot $repoRoot
Assert-Equal 'Default output root is repo-local' $outputRoot ([IO.Path]::GetFullPath((Join-Path $expectedStateRoot 'builds')))

$containedPath = Assert-Ps3PathInsideRoot -Path (Join-Path $outputRoot 'one\artifact.self') -Root $outputRoot -Label 'Artifact'
Assert-True 'Contained artifact path is accepted' $containedPath.EndsWith('one\artifact.self', [StringComparison]::OrdinalIgnoreCase)

$explicitRoot = Join-Path $repoRoot '.ps3\custom-output'
Assert-Equal 'Explicit output root is preserved' (Resolve-Ps3OutputRoot -RepoRoot $repoRoot -RequestedRoot $explicitRoot) ([IO.Path]::GetFullPath($explicitRoot))

Write-Output 'PS3 path contract tests passed.'
