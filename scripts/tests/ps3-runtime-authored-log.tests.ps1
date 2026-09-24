<#
.SYNOPSIS
    Exercises authored TTY acceptance with synthetic logs in a task-owned output directory.
#>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$taskRoot = 'C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\authored-log-check'
if ($OutputRoot -notmatch '^[A-Za-z]:[\\/]') { throw 'OutputRoot must be fully qualified.' }
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (-not $OutputRoot.StartsWith($taskRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputRoot must be beneath the owned authored-log-check directory.'
}
$ancestor = $OutputRoot
while ($ancestor) {
    if (Test-Path -LiteralPath $ancestor) {
        if ((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'OutputRoot must not traverse a reparse point.'
        }
    }
    $ancestor = [IO.Path]::GetDirectoryName($ancestor)
}

$checker = Join-Path $PSScriptRoot 'ps3-runtime-visible-content.tests.ps1'
$cases = New-Object 'System.Collections.Generic.List[object]'

function Add-Case {
    <# .SYNOPSIS Registers a synthetic log and the exact behavior its child checker must report. #>
    param([string]$Name, [string]$Log, [string]$Arguments, [bool]$Pass, [string]$Diagnostic)
    $cases.Add([pscustomobject]@{ Name=$Name; Log=$Log; Arguments=$Arguments; Pass=$Pass; Diagnostic=$Diagnostic })
}

$matrix = '1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1'
$valid = @"
PS3_AUTHORED_PROGRAM_READY asset=ForwardStandardShader vertex=ForwardStandardShader.vs pixel=ForwardStandardShader.ps vertexBlobBytes=100 pixelBlobBytes=100 fragmentUcodeBytes=100 ownedGpuBytes=100
PS3_AUTHORED_MATERIAL path=materials/cube.ps3mat asset=CubeMaterial shader=ForwardStandardShader vertex=ForwardStandardShader.vs pixel=ForwardStandardShader.ps variant=ForwardStandard sourceHash=abc rgba=FFFFFFFF flags=0 diffuse=
PS3_CORE_SCENE_LOADED sceneId=cube_test freeBytes=1000
PS3_CORE_AUTHORED_DRAW Frame=0 material=CubeMaterial indices=36 Lit=0 HasDir=1 Double=0
PS3_CORE_AUTHORED_VALUES Frame=0 field=World values=$matrix
PS3_CORE_AUTHORED_VALUES Frame=0 field=WVP values=$matrix
PS3_CORE_AUTHORED_VALUES Frame=0 field=Normal values=$matrix
PS3_CORE_AUTHORED_VALUES Frame=0 field=Camera values=0,0,3,1
PS3_CORE_AUTHORED_VALUES Frame=0 field=Base values=1,1,1,1
PS3_CORE_AUTHORED_VALUES Frame=0 field=Ambient values=0.1,0.1,0.1,1
PS3_CORE_AUTHORED_VALUES Frame=0 field=DirColor values=1,1,1,1
PS3_CORE_AUTHORED_VALUES Frame=0 field=DirDirection values=0,-1,0,0
PS3_FRAME_PROBE label=first-frame pose=0.000 readable=1 center=0xFFFFFF left=0x6495ED right=0x6495ED top=0x6495ED bottom=0x6495ED corner=0x6495ED
PS3_EXIT reason=run-duration-reached failed=0 frames=10 draws=10
PS3_CORE_STAGE stage=CoreDisposed freeBytes=1000
PS3_DRAIN_COMPLETE label=1 frames=10
PS3_FRAME_PROBE label=final-frame pose=0.100 readable=1 center=0xFFFFFF left=0x6495ED right=0x6495ED top=0x6495ED bottom=0x6495ED corner=0x6495ED
PS3_AUTHORED_CACHE_SHUTDOWN freedGpuBytes=100 ownedGpuBytes=0 programsCreated=1 texturesCreated=4
PS3_SHUTDOWN exitCode=0
"@
$argsWhite = '-AuthoredShaders -ExpectedCenterRgb FFFFFF -ExpectedLit 0'
Add-Case 'unlit-white' $valid $argsWhite $true 'PASS authored'
$lit = $valid.Replace('Lit=0', 'Lit=1').Replace('flags=0', 'flags=1').Replace('center=0xFFFFFF', 'center=0x804020')
Add-Case 'lit-explicit-oracle' $lit '-AuthoredShaders -ExpectedCenterRgb 804020 -ExpectedLit 1' $true 'PASS authored'
Add-Case 'channel-tolerance' ($lit.Replace('center=0x804020', 'center=0x81411F')) '-AuthoredShaders -ExpectedCenterRgb 804020 -ExpectedLit 1 -ChannelTolerance 1' $true 'PASS authored'
Add-Case 'legacy-smoke' "PS3_CORE_SCENE_LOADED sceneId=cube_test`nPS3_FRAME_PROBE label=first-frame center=0x112233 corner=0x6495ED" '' $true 'PASS cube_test'
Add-Case 'boot-diagnostic' ("PS3_DIAGNOSTIC_SHADER_READY`n" + $valid) $argsWhite $true 'PASS authored'
Add-Case 'missing-scene' ($valid.Replace('sceneId=cube_test', 'sceneId=other')) $argsWhite $false 'cube_test scene'
Add-Case 'missing-ready' ($valid -replace '(?m)^PS3_AUTHORED_PROGRAM_READY[^\r\n]*\r?\n', '') $argsWhite $false 'program READY'
Add-Case 'wrong-shader' ($valid.Replace('asset=ForwardStandardShader ', 'asset=WrongShader ')) $argsWhite $false 'program READY'
Add-Case 'wrong-vertex' ($valid.Replace('vertex=ForwardStandardShader.vs', 'vertex=wrong.vs')) $argsWhite $false 'program READY'
Add-Case 'wrong-pixel' ($valid.Replace('pixel=ForwardStandardShader.ps', 'pixel=wrong.ps')) $argsWhite $false 'program READY'
Add-Case 'wrong-variant' ($valid.Replace('variant=ForwardStandard', 'variant=Wrong')) $argsWhite $false 'material'
Add-Case 'unmatched-material' ($valid.Replace('material=CubeMaterial', 'material=Other')) $argsWhite $false 'material'
Add-Case 'wrong-lit' ($valid.Replace('Lit=0', 'Lit=1')) $argsWhite $false 'ExpectedLit'
Add-Case 'zero-indices' ($valid.Replace('indices=36', 'indices=0')) $argsWhite $false 'first authored DRAW'
Add-Case 'wrong-frame' ($valid.Replace('DRAW Frame=0', 'DRAW Frame=1')) $argsWhite $false 'first authored DRAW'
foreach ($field in @('World','WVP','Normal','Camera','Base','Ambient','DirColor','DirDirection')) {
    Add-Case "missing-$field" ($valid -replace "(?m)^PS3_CORE_AUTHORED_VALUES Frame=0 field=$field values=[^\r\n]*\r?\n", '') $argsWhite $false "values field $field"
}
Add-Case 'duplicate-values' ($valid + "`nPS3_CORE_AUTHORED_VALUES Frame=0 field=Base values=1,1,1,1") $argsWhite $false 'values field Base'
Add-Case 'wrong-value-count' ($valid.Replace('field=Camera values=0,0,3,1', 'field=Camera values=0,0,3')) $argsWhite $false 'values field Camera'
foreach ($number in @('NaN', 'Infinity', '-Infinity', '1e999', 'bad', '')) {
    Add-Case ('nonfinite-' + $cases.Count) ($valid.Replace('field=Base values=1,1,1,1', "field=Base values=$number,1,1,1")) $argsWhite $false 'finite numeric'
}
Add-Case 'unreadable-probe' ($valid.Replace('readable=1', 'readable=0')) $argsWhite $false 'readable'
Add-Case 'clear-only' ($valid.Replace('center=0xFFFFFF', 'center=0x6495ED')) $argsWhite $false 'clear-only'
Add-Case 'wrong-center' ($valid.Replace('center=0xFFFFFF', 'center=0xFFFFFE')) $argsWhite $false 'ExpectedCenterRgb'
Add-Case 'wrong-corner' ($valid.Replace('corner=0x6495ED', 'corner=0x000000')) $argsWhite $false 'corner'
$firstProbe = ($valid -split '\r?\n' | Where-Object { $_ -like 'PS3_FRAME_PROBE label=first-frame *' })
Add-Case 'duplicate-probe' ($valid + "`n$firstProbe") $argsWhite $false 'probe'
Add-Case 'error-marker' ($valid + "`nPS3_CORE_ERROR stage=DrawAuthored") $argsWhite $false 'error marker'
Add-Case 'missing-content' ($valid + "`nPS3_CONTENT_MISSING path=mesh") $argsWhite $false 'error marker'
Add-Case 'failed-exit' ($valid.Replace('failed=0', 'failed=1')) $argsWhite $false 'PS3_EXIT'
Add-Case 'wrong-exit-reason' ($valid.Replace('reason=run-duration-reached', 'reason=user-requested')) $argsWhite $false 'PS3_EXIT'
Add-Case 'zero-draws' ($valid.Replace('frames=10 draws=10', 'frames=10 draws=0')) $argsWhite $false 'PS3_EXIT'
Add-Case 'duplicate-exit' ($valid + "`nPS3_EXIT reason=run-duration-reached failed=0 frames=10 draws=10") $argsWhite $false 'PS3_EXIT'
Add-Case 'missing-drain' ($valid.Replace('PS3_DRAIN_COMPLETE', 'REMOVED_DRAIN')) $argsWhite $false 'DRAIN_COMPLETE'
Add-Case 'gpu-ownership' ($valid.Replace('ownedGpuBytes=0 programsCreated', 'ownedGpuBytes=1 programsCreated')) $argsWhite $false 'cache SHUTDOWN'
Add-Case 'no-programs-created' ($valid.Replace('programsCreated=1 texturesCreated', 'programsCreated=0 texturesCreated')) $argsWhite $false 'programsCreated'
Add-Case 'missing-shutdown' ($valid.Replace('PS3_SHUTDOWN', 'REMOVED_SHUTDOWN')) $argsWhite $false 'PS3_SHUTDOWN'
Add-Case 'wrong-shutdown' ($valid.Replace('exitCode=0', 'exitCode=1')) $argsWhite $false 'PS3_SHUTDOWN'
Add-Case 'duplicate-shutdown' ($valid + "`nPS3_SHUTDOWN exitCode=0") $argsWhite $false 'PS3_SHUTDOWN'
Add-Case 'wrong-order' ($valid.Replace('PS3_CORE_STAGE stage=CoreDisposed freeBytes=1000', 'PS3_DRAIN_COMPLETE label=1 frames=10').Replace("PS3_DRAIN_COMPLETE label=1 frames=10`nPS3_FRAME_PROBE label=final", "PS3_CORE_STAGE stage=CoreDisposed freeBytes=1000`nPS3_FRAME_PROBE label=final")) $argsWhite $false 'lifecycle order'
Add-Case 'missing-expected-rgb' $valid '-AuthoredShaders -ExpectedLit 0' $false 'ExpectedCenterRgb is required'
Add-Case 'missing-expected-lit' $valid '-AuthoredShaders -ExpectedCenterRgb FFFFFF' $false 'ExpectedLit is required'
Add-Case 'bad-rgb' $valid '-AuthoredShaders -ExpectedCenterRgb FFFFF -ExpectedLit 0' $false 'ExpectedCenterRgb'
Add-Case 'bad-lit' $valid '-AuthoredShaders -ExpectedCenterRgb FFFFFF -ExpectedLit 2' $false 'ExpectedLit'
Add-Case 'bad-tolerance' $valid ($argsWhite + ' -ChannelTolerance 9') $false 'ChannelTolerance'

$firstDraw = 'PS3_CORE_AUTHORED_DRAW Frame=0 material=CubeMaterial indices=36 Lit=0 HasDir=1 Double=0'
Add-Case 'duplicate-draw-identical' ($valid + "`n$firstDraw") $argsWhite $false 'exactly one authored DRAW'
Add-Case 'duplicate-draw-conflicting' ($valid + "`n" + $firstDraw.Replace('Lit=0', 'Lit=1')) $argsWhite $false 'exactly one authored DRAW'
Add-Case 'duplicate-draw-malformed' ($valid + "`nPS3_CORE_AUTHORED_DRAW malformed") $argsWhite $false 'exactly one authored DRAW'
Add-Case 'material-lit-mismatch' ($lit.Replace('flags=1', 'flags=0')) '-AuthoredShaders -ExpectedCenterRgb 804020 -ExpectedLit 1' $false 'material flags disagree with DRAW Lit'
Add-Case 'material-double-mismatch' ($valid.Replace('Double=0', 'Double=1')) $argsWhite $false 'material flags disagree with DRAW Double'
Add-Case 'material-double-consistent' ($valid.Replace('Double=0', 'Double=1').Replace('flags=0', 'flags=2')) $argsWhite $true 'PASS authored'
foreach ($base in @('0.5,1,1,1', '1,0.5,1,1', '1,1,0.5,1', '1,1,1,0.5')) {
    Add-Case ('material-base-mismatch-' + $cases.Count) ($valid.Replace('field=Base values=1,1,1,1', "field=Base values=$base")) $argsWhite $false 'material RGBA disagrees with Base'
}
Add-Case 'material-base-float32-rounding' ($valid.Replace('rgba=FFFFFFFF', 'rgba=804020FF').Replace('field=Base values=1,1,1,1', 'field=Base values=0.501960814,0.250980407,0.125490203,1')) $argsWhite $true 'PASS authored'
Add-Case 'material-base-within-tolerance' ($valid.Replace('field=Base values=1,1,1,1', 'field=Base values=0.9999991,1,1,1')) $argsWhite $true 'PASS authored'
Add-Case 'material-base-outside-tolerance' ($valid.Replace('field=Base values=1,1,1,1', 'field=Base values=0.9999989,1,1,1')) $argsWhite $false 'material RGBA disagrees with Base'
Add-Case 'duplicate-disposed-malformed' ($valid + "`nPS3_CORE_STAGE stage=CoreDisposed freeBytes=bad") $argsWhite $false 'exactly one CoreDisposed'
Add-Case 'duplicate-disposed-extra-field' ($valid + "`nPS3_CORE_STAGE stage=CoreDisposed freeBytes=1000 failed=1") $argsWhite $false 'exactly one CoreDisposed'

$oldTemp = $env:TEMP
$oldTmp = $env:TMP
$failed = 0
try {
    $tempPath = Join-Path $OutputRoot 'temp'
    New-Item -ItemType Directory -Path $tempPath -Force | Out-Null
    $env:TEMP = $tempPath
    $env:TMP = $tempPath
    foreach ($case in $cases) {
        $fixture = Join-Path $OutputRoot ($case.Name + '.tty.log')
        [IO.File]::WriteAllText($fixture, $case.Log)
        $start = New-Object Diagnostics.ProcessStartInfo
        $start.FileName = 'powershell.exe'
        $start.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $checker + '" -TtyLogPath "' + $fixture + '" ' + $case.Arguments
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $process = [Diagnostics.Process]::Start($start)
        try {
            $output = $process.StandardOutput.ReadToEnd() + $process.StandardError.ReadToEnd()
            $process.WaitForExit()
            $code = $process.ExitCode
        } finally { $process.Dispose() }
        [IO.File]::WriteAllText((Join-Path $OutputRoot ($case.Name + '.result.log')), $output)
        if (($case.Pass -ne ($code -eq 0)) -or ($output -notmatch [regex]::Escape($case.Diagnostic))) {
            $failed++
            Write-Output "FAIL $($case.Name): exit=$code expectedPass=$($case.Pass) expectedDiagnostic=$($case.Diagnostic)"
        } else { Write-Output "PASS $($case.Name)" }
    }
    Write-Output "RESULT passed=$($cases.Count - $failed) failed=$failed total=$($cases.Count)"
} finally {
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
}
if ($failed -gt 0) { exit 1 }
