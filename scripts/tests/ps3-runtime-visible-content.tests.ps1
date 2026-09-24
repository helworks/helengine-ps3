<#
.SYNOPSIS
    Confirms the centered cube_test draw changes a center pixel from the clear-only corners.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$TtyLogPath,
    [switch]$AuthoredShaders,
    [string]$ExpectedCenterRgb,
    [ValidateSet(0, 1)][int]$ExpectedLit,
    [ValidateRange(0, 8)][int]$ChannelTolerance = 0
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not (Test-Path -LiteralPath $TtyLogPath -PathType Leaf)) {
    throw "RPCS3 TTY log does not exist: $TtyLogPath"
}

$log = Get-Content -LiteralPath $TtyLogPath -Raw
if ($AuthoredShaders) {
    if (-not $PSBoundParameters.ContainsKey('ExpectedCenterRgb')) { throw 'ExpectedCenterRgb is required in authored mode.' }
    if (-not $PSBoundParameters.ContainsKey('ExpectedLit')) { throw 'ExpectedLit is required in authored mode.' }
    if ($ExpectedCenterRgb -cnotmatch '^[0-9A-Fa-f]{6}$') { throw 'ExpectedCenterRgb must contain exactly six hex digits.' }

    function Get-TtyRecords {
        <# .SYNOPSIS Selects complete real marker lines and retains their positions for lifecycle checks. #>
        param([string]$Marker)
        return [regex]::Matches($log, ('(?m)^' + [regex]::Escape($Marker) + '(?=[ \t\r\n]|$)[^\r\n]*'))
    }

    function Read-TtyRecord {
        <# .SYNOPSIS Validates the entire native record, rejecting malformed or contradictory fields. #>
        param([object]$Record, [string]$Pattern, [string]$Diagnostic)
        $parsed = [regex]::Match($Record.Value, $Pattern)
        if (-not $parsed.Success) { throw $Diagnostic }
        return $parsed
    }

    function Assert-FiniteNumber {
        <# .SYNOPSIS Parses one native decimal value using invariant culture and rejects nonfinite values. #>
        param([string]$Text, [string]$Field)
        $number = 0.0
        if ($Text -cnotmatch '^[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?$' -or
            -not [double]::TryParse($Text, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$number) -or
            [double]::IsNaN($number) -or [double]::IsInfinity($number)) {
            throw "Expected finite numeric value for $Field; got '$Text'."
        }
    }

    if ($log -cmatch '\bPS3_(?:(?:[A-Z0-9_]+_)?ERROR|CONTENT_MISSING)\b') { throw "TTY contains error marker $($Matches[0])." }
    $scenes = @(Get-TtyRecords 'PS3_CORE_SCENE_LOADED' | Where-Object { $_.Value -cmatch '^PS3_CORE_SCENE_LOADED sceneId=cube_test freeBytes=[0-9]+$' })
    if ($scenes.Count -eq 0) { throw 'Authored run did not load the cube_test scene.' }

    $programs = @(Get-TtyRecords 'PS3_AUTHORED_PROGRAM_READY')
    if ($programs.Count -eq 0) { throw 'Missing authored program READY.' }
    foreach ($program in $programs) {
        $null = Read-TtyRecord $program '^PS3_AUTHORED_PROGRAM_READY asset=ForwardStandardShader vertex=ForwardStandardShader\.vs pixel=ForwardStandardShader\.ps vertexBlobBytes=[0-9]+ pixelBlobBytes=[0-9]+ fragmentUcodeBytes=[0-9]+ ownedGpuBytes=[0-9]+$' 'Invalid authored program READY identity or record.'
    }
    $materials = @(Get-TtyRecords 'PS3_AUTHORED_MATERIAL')
    if ($materials.Count -eq 0) { throw 'Missing authored material.' }
    $materialIds = @{}
    foreach ($material in $materials) {
        $parsed = Read-TtyRecord $material '^PS3_AUTHORED_MATERIAL path=.+ asset=(\S+) shader=ForwardStandardShader vertex=ForwardStandardShader\.vs pixel=ForwardStandardShader\.ps variant=ForwardStandard sourceHash=\S+ rgba=([0-9A-Fa-f]{8}) flags=([0-9]+) diffuse=.*$' 'Invalid authored material shader or variant.'
        $flags = [uint32]0
        if (-not [uint32]::TryParse($parsed.Groups[3].Value, [Globalization.NumberStyles]::None, [Globalization.CultureInfo]::InvariantCulture, [ref]$flags)) { throw 'Invalid authored material flags.' }
        $materialIds[$parsed.Groups[1].Value] = [pscustomobject]@{ Index=$material.Index; Rgba=$parsed.Groups[2].Value; Flags=$flags }
    }
    $draws = @(Get-TtyRecords 'PS3_CORE_AUTHORED_DRAW')
    if ($draws.Count -ne 1) { throw 'Expected exactly one authored DRAW record.' }
    $draw = Read-TtyRecord $draws[0] '^PS3_CORE_AUTHORED_DRAW Frame=0 material=(\S+) indices=0*[1-9][0-9]* Lit=([01]) HasDir=[01] Double=([01])$' 'Invalid first authored DRAW: Frame=0 and positive indices are required.'
    if ([int]$draw.Groups[2].Value -ne $ExpectedLit) { throw 'First authored DRAW does not match ExpectedLit.' }
    $materialId = $draw.Groups[1].Value
    if (-not $materialIds.ContainsKey($materialId) -or $materialIds[$materialId].Index -ge $draws[0].Index -or $programs[0].Index -ge $draws[0].Index) {
        throw 'First authored DRAW has no preceding matching authored material and program.'
    }
    $selectedMaterial = $materialIds[$materialId]
    if (($selectedMaterial.Flags -band 1) -ne [int]$draw.Groups[2].Value) { throw 'Selected material flags disagree with DRAW Lit.' }
    if ((($selectedMaterial.Flags -band 2) -shr 1) -ne [int]$draw.Groups[3].Value) { throw 'Selected material flags disagree with DRAW Double.' }

    $fieldCounts = [ordered]@{ World=16; WVP=16; Normal=16; Camera=4; Base=4; Ambient=4; DirColor=4; DirDirection=4 }
    $values = @{}
    $baseValues = @()
    foreach ($record in @(Get-TtyRecords 'PS3_CORE_AUTHORED_VALUES')) {
        $parsed = Read-TtyRecord $record '^PS3_CORE_AUTHORED_VALUES Frame=0 field=(World|WVP|Normal|Camera|Base|Ambient|DirColor|DirDirection) values=(.*)$' 'Malformed first-frame values record.'
        $field = $parsed.Groups[1].Value
        if ($values.ContainsKey($field)) { throw "Duplicate values field $field." }
        $items = $parsed.Groups[2].Value.Split(',')
        if ($items.Count -ne $fieldCounts[$field]) { throw "Wrong count for values field $field." }
        foreach ($item in $items) { Assert-FiniteNumber $item $field }
        if ($field -ceq 'Base') { $baseValues = $items }
        $values[$field] = $record.Index
    }
    foreach ($field in $fieldCounts.Keys) {
        if (-not $values.ContainsKey($field)) { throw "Missing values field $field." }
    }
    # Allow float32 %.9g rounding while rejecting materially different submitted color channels.
    for ($channel = 0; $channel -lt 4; $channel++) {
        $materialChannel = [Convert]::ToInt32($selectedMaterial.Rgba.Substring($channel * 2, 2), 16) / 255.0
        $baseChannel = [double]::Parse($baseValues[$channel], [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture)
        if ([Math]::Abs($baseChannel - $materialChannel) -gt 1e-6) { throw 'Selected material RGBA disagrees with Base values (absolute tolerance 1e-6).' }
    }

    $probes = @{}
    foreach ($record in @(Get-TtyRecords 'PS3_FRAME_PROBE')) {
        $parsed = Read-TtyRecord $record '^PS3_FRAME_PROBE label=(first-frame|final-frame) pose=(\S+) readable=([01]) center=0x([0-9A-Fa-f]{6}) left=0x[0-9A-Fa-f]{6} right=0x[0-9A-Fa-f]{6} top=0x[0-9A-Fa-f]{6} bottom=0x[0-9A-Fa-f]{6} corner=0x([0-9A-Fa-f]{6})$' 'Malformed frame probe record.'
        $label = $parsed.Groups[1].Value
        if ($probes.ContainsKey($label)) { throw "Duplicate frame probe $label." }
        Assert-FiniteNumber $parsed.Groups[2].Value 'probe pose'
        $probes[$label] = [pscustomobject]@{ Index=$record.Index; Readable=$parsed.Groups[3].Value; Center=$parsed.Groups[4].Value; Corner=$parsed.Groups[5].Value }
    }
    if (-not $probes.ContainsKey('first-frame')) { throw 'Missing first-frame probe.' }
    $probe = $probes['first-frame']
    if ($probe.Readable -ne '1') { throw 'First-frame probe must be readable=1.' }
    if ($probe.Corner -ine '6495ED') { throw 'First-frame probe corner must be 0x6495ED.' }
    if ($probe.Center -ieq $probe.Corner) { throw 'First-frame probe is clear-only.' }
    for ($channel = 0; $channel -lt 6; $channel += 2) {
        $actual = [Convert]::ToInt32($probe.Center.Substring($channel, 2), 16)
        $expected = [Convert]::ToInt32($ExpectedCenterRgb.Substring($channel, 2), 16)
        if ([Math]::Abs($actual - $expected) -gt $ChannelTolerance) { throw 'First-frame center does not match ExpectedCenterRgb within ChannelTolerance.' }
    }

    $exits = @(Get-TtyRecords 'PS3_EXIT')
    if ($exits.Count -ne 1) { throw 'Expected exactly one PS3_EXIT record.' }
    $null = Read-TtyRecord $exits[0] '^PS3_EXIT reason=run-duration-reached failed=0 frames=0*[1-9][0-9]* draws=0*[1-9][0-9]*$' 'Invalid PS3_EXIT: require run-duration-reached, failed=0, positive frames and draws.'
    $shutdowns = @(Get-TtyRecords 'PS3_SHUTDOWN')
    if ($shutdowns.Count -ne 1) { throw 'Expected exactly one PS3_SHUTDOWN record.' }
    $null = Read-TtyRecord $shutdowns[0] '^PS3_SHUTDOWN exitCode=0$' 'Invalid PS3_SHUTDOWN: require exitCode=0.'
    $disposed = @(Get-TtyRecords 'PS3_CORE_STAGE' | Where-Object { $_.Value -cmatch '(?:^|[ \t])stage=CoreDisposed(?=[ \t]|$)' })
    if ($disposed.Count -ne 1) { throw 'Expected exactly one CoreDisposed stage.' }
    $null = Read-TtyRecord $disposed[0] '^PS3_CORE_STAGE stage=CoreDisposed freeBytes=[0-9]+$' 'Invalid CoreDisposed stage record.'
    $drains = @(Get-TtyRecords 'PS3_DRAIN_COMPLETE')
    if ($drains.Count -ne 1) { throw 'Expected exactly one DRAIN_COMPLETE record.' }
    $null = Read-TtyRecord $drains[0] '^PS3_DRAIN_COMPLETE label=[0-9]+ frames=[0-9]+$' 'Invalid DRAIN_COMPLETE record.'
    if ($draws[0].Index -ge $probe.Index -or $probe.Index -ge $exits[0].Index -or $exits[0].Index -ge $disposed[0].Index -or $disposed[0].Index -ge $drains[0].Index) {
        throw 'Invalid lifecycle order: draw, first probe, exit, CoreDisposed, drain are required.'
    }
    foreach ($field in $fieldCounts.Keys) {
        if ($values[$field] -le $draws[0].Index -or $values[$field] -ge $probe.Index) { throw "Invalid lifecycle order for values field $field." }
    }
    $caches = @(Get-TtyRecords 'PS3_AUTHORED_CACHE_SHUTDOWN')
    if ($caches.Count -eq 0) { throw 'Missing authored cache SHUTDOWN.' }
    $hasProgram = $false
    foreach ($cache in $caches) {
        $parsed = Read-TtyRecord $cache '^PS3_AUTHORED_CACHE_SHUTDOWN freedGpuBytes=[0-9]+ ownedGpuBytes=0 programsCreated=([0-9]+) texturesCreated=[0-9]+$' 'Invalid authored cache SHUTDOWN: ownedGpuBytes must be zero.'
        if ($parsed.Groups[1].Value -match '[1-9]') { $hasProgram = $true }
        if ($cache.Index -le $drains[0].Index -or $cache.Index -ge $shutdowns[0].Index) { throw 'Invalid lifecycle order: drain before cache retirement before final shutdown is required.' }
    }
    if (-not $hasProgram) { throw 'Authored cache SHUTDOWN must report positive programsCreated in at least one cache.' }
    if ($probes.ContainsKey('final-frame') -and ($probes['final-frame'].Index -le $drains[0].Index -or $probes['final-frame'].Index -ge $caches[0].Index)) { throw 'Invalid lifecycle order for final-frame probe.' }
    Write-Output "PASS authored cube_test ExpectedCenterRgb=$ExpectedCenterRgb ExpectedLit=$ExpectedLit ChannelTolerance=$ChannelTolerance; TTY validation only, not provenance or a lighting oracle."
    return
}
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
