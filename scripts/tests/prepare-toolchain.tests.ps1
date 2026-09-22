<#
.SYNOPSIS
    Tests the repo-local NVIDIA Cg archive acquisition contract.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $repoRoot 'scripts\ps3-paths.ps1')
$cgHelperPath = Join-Path $repoRoot 'scripts\ps3-cg.ps1'
if (-not (Test-Path -LiteralPath $cgHelperPath -PathType Leaf)) {
    throw "Cg helper is missing: $cgHelperPath"
}
. $cgHelperPath

function Assert-True {
    param([string]$Name, [bool]$Condition)
    if (-not $Condition) { throw "$Name failed." }
    Write-Output "PASS $Name"
}

function Assert-Throws {
    param([string]$Name, [scriptblock]$Action, [string]$ExpectedMessage)
    try {
        & $Action | Out-Null
    } catch {
        if ([string]$_.Exception.Message -notmatch $ExpectedMessage) {
            throw "$Name failed with the wrong error: $($_.Exception.Message)"
        }
        Write-Output "PASS $Name"
        return
    }
    throw "$Name accepted invalid input."
}

$fixtureId = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmssfff')
$fixtureRoot = Join-Path $repoRoot ".ps3\test-fixtures\prepare-toolchain-$fixtureId"
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
$fixtureSource = Join-Path $fixtureRoot 'fixture-source.tgz'
[IO.File]::WriteAllBytes($fixtureSource, [byte[]](0x43, 0x47, 0x2D, 0x46, 0x49, 0x58, 0x54, 0x55, 0x52, 0x45))
$fixtureHash = (Get-FileHash -LiteralPath $fixtureSource -Algorithm SHA256).Hash.ToLowerInvariant()
$stateRoot = Join-Path $fixtureRoot 'state'
$toolkit = [PSCustomObject]@{
    Archive = 'Cg-test.tgz'
    Url = 'https://example.invalid/Cg-test.tgz'
    ArchiveSha256 = $fixtureHash
}
$downloadState = @{ Count = 0 }
$fakeDownloader = {
    param([string]$Url, [string]$Destination)
    $downloadState.Count++
    Copy-Item -LiteralPath $fixtureSource -Destination $Destination
}.GetNewClosure()

$archivePath = Ensure-Ps3CgArchive -ShaderToolkit $toolkit -StateRoot $stateRoot -DownloadFile $fakeDownloader
Assert-True 'Missing archive is downloaded' ($downloadState.Count -eq 1 -and (Test-Path -LiteralPath $archivePath -PathType Leaf))
Assert-True 'Downloaded archive hash is checked' ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $fixtureHash)

$archivePathAgain = Ensure-Ps3CgArchive -ShaderToolkit $toolkit -StateRoot $stateRoot -DownloadFile $fakeDownloader
Assert-True 'Existing valid archive is reused' ($archivePathAgain -eq $archivePath -and $downloadState.Count -eq 1)

$wrongToolkit = [PSCustomObject]@{
    Archive = $toolkit.Archive
    Url = $toolkit.Url
    ArchiveSha256 = ('0' * 64)
}
Assert-Throws 'Wrong archive hash is rejected' {
    Ensure-Ps3CgArchive -ShaderToolkit $wrongToolkit -StateRoot $stateRoot -DownloadFile $fakeDownloader
} 'hash mismatch'

Write-Output 'Cg archive contract tests passed.'
