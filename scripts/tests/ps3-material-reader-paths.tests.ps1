<#
.SYNOPSIS
    Exercises the material runner's pure strict-child path validator.

.DESCRIPTION
    This test parses the build runner and evaluates only the Assert-StrictChildPath
    function AST. It does not execute Docker, inspect fixtures, or create files.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$runnerPath = Join-Path $PSScriptRoot 'ps3-material-reader.tests.ps1'
$tokens = $null
$parseErrors = $null
$runnerAst = [System.Management.Automation.Language.Parser]::ParseFile(
    $runnerPath,
    [ref]$tokens,
    [ref]$parseErrors)
if ($parseErrors.Count -ne 0) {
    throw "Could not parse material runner: $($parseErrors[0].Message)"
}

$validatorAsts = @($runnerAst.FindAll({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Assert-StrictChildPath'
}, $true))
if ($validatorAsts.Count -ne 1) {
    throw "Expected exactly one Assert-StrictChildPath function AST; found $($validatorAsts.Count)."
}

$validatorScript = [ScriptBlock]::Create($validatorAsts[0].Extent.Text)
. $validatorScript

$driveRoot = [IO.Path]::GetPathRoot([IO.Directory]::GetCurrentDirectory())
if ([string]::IsNullOrEmpty($driveRoot) -or $driveRoot -notmatch '^[A-Za-z]:\\$') {
    throw "This path contract test requires a Windows drive root; found '$driveRoot'."
}

$parent = Join-Path $driveRoot 'ps3-material-reader-path-test-root\native-material'
$child = Join-Path $parent 'child'
$ancestor = Split-Path -Parent $parent
$siblingChild = "$parent-sibling\child"
$parentSuffix = $parent.Substring($driveRoot.Length).TrimStart('\', '/')
$rootRelativeChild = "\$parentSuffix\child"
$driveRelativeChild = "$($driveRoot.Substring(0, 2))$parentSuffix\child"

function Invoke-PathCase {
    <# Runs one validator behavior and reports whether its acceptance matches the contract. #>
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][scriptblock]$Action,
        [Parameter(Mandatory)][bool]$ShouldReject
    )

    $wasRejected = $false
    try {
        $null = & $Action
    }
    catch {
        $wasRejected = $true
    }

    if ($wasRejected -eq $ShouldReject) {
        Write-Host "PASS $Name"
        return $true
    }

    $expected = if ($ShouldReject) { 'rejection' } else { 'acceptance' }
    Write-Host "FAIL $Name (expected $expected)"
    return $false
}

$passed = 0
$failed = 0
$cases = @(
    @{ Name = 'accepts_drive_absolute_child'; Path = $child; Parent = $parent; Reject = $false },
    @{ Name = 'rejects_parent_equality'; Path = $parent; Parent = $parent; Reject = $true },
    @{ Name = 'rejects_parent_equality_backslash'; Path = "$parent\"; Parent = $parent; Reject = $true },
    @{ Name = 'rejects_parent_equality_forwardslash'; Path = "$parent/"; Parent = $parent; Reject = $true },
    @{ Name = 'rejects_ancestor'; Path = $ancestor; Parent = $parent; Reject = $true },
    @{ Name = 'rejects_sibling_prefix'; Path = $siblingChild; Parent = $parent; Reject = $true },
    @{ Name = 'rejects_relative_path'; Path = 'relative\child'; Parent = $parent; Reject = $true },
    @{ Name = 'rejects_drive_relative_path'; Path = $driveRelativeChild; Parent = $parent; Reject = $true },
    @{ Name = 'rejects_root_relative_path_that_resolves_inside_parent'; Path = $rootRelativeChild; Parent = $parent; Reject = $true },
    @{ Name = 'rejects_relative_parent'; Path = $child; Parent = 'relative-parent'; Reject = $true },
    @{ Name = 'rejects_drive_relative_parent'; Path = $child; Parent = "$($driveRoot.Substring(0, 2))relative-parent"; Reject = $true },
    @{ Name = 'rejects_root_relative_parent'; Path = $child; Parent = "\$parentSuffix"; Reject = $true }
)

foreach ($case in $cases) {
    if (Invoke-PathCase -Name $case.Name -ShouldReject $case.Reject -Action {
        Assert-StrictChildPath -Path $case.Path -Parent $case.Parent -Label $case.Name
    }) {
        $passed++
    }
    else {
        $failed++
    }
}

$uncParent = '\\ps3-material-reader-test-server\ps3-material-reader-test-share\native-material'
$uncChild = "$uncParent\child"
if (Invoke-PathCase -Name 'accepts_complete_unc_child' -ShouldReject $false -Action {
    Assert-StrictChildPath -Path $uncChild -Parent $uncParent -Label 'UNC path'
}) {
    $passed++
}
else {
    $failed++
}

if (Invoke-PathCase -Name 'rejects_incomplete_unc_path' -ShouldReject $true -Action {
    Assert-StrictChildPath -Path '\\ps3-material-reader-test-server' -Parent $uncParent -Label 'UNC path'
}) {
    $passed++
}
else {
    $failed++
}

if (Invoke-PathCase -Name 'rejects_incomplete_unc_parent' -ShouldReject $true -Action {
    Assert-StrictChildPath -Path $child -Parent '\\ps3-material-reader-test-server' -Label 'UNC parent'
}) {
    $passed++
}
else {
    $failed++
}

Write-Output "PS3_MATERIAL_READER_PATH_TESTS passed=$passed failed=$failed"
if ($failed -ne 0) {
    exit 1
}
