<#
.SYNOPSIS
    Confirms the centered cube_test draw changes a center pixel from the clear-only corners.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$TtyLogPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not (Test-Path -LiteralPath $TtyLogPath -PathType Leaf)) {
    throw "RPCS3 TTY log does not exist: $TtyLogPath"
}

$log = Get-Content -LiteralPath $TtyLogPath -Raw
if ($log -notmatch 'PS3_CORE_SCENE_LOADED sceneId=cube_test') {
    throw 'RPCS3 run did not load the expected cube_test scene.'
}
if ($log -notmatch 'PS3_FRAME_PROBE label=first-frame[^\r\n]*center=0x([0-9A-Fa-f]{6})[^\r\n]*corner=0x([0-9A-Fa-f]{6})') {
    throw 'RPCS3 first-frame probe did not report center and corner pixels.'
}

$center = $Matches[1].ToUpperInvariant()
$corner = $Matches[2].ToUpperInvariant()
if ($center -eq $corner) {
    throw "cube_test center pixel still matches the clear-only corner (0x$center); no visible centered geometry was confirmed."
}

Write-Output "PASS cube_test center pixel 0x$center differs from clear corner 0x$corner."
