<#
.SYNOPSIS
    Runs the locked cgcomp fragment parser capacity regression.
.DESCRIPTION
    Mounts one authored fragment assembly read-only into the verified SDK image,
    captures bounded compiler logs, validates the generated RSX fragment header,
    and verifies that the input assembly remains byte-for-byte unchanged.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InputAssemblyPath,
    [Parameter(Mandatory)][string]$OutputRoot,
    [string]$ImageId = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-Sha256 {
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Write-BoundedLogTail {
    param([Parameter(Mandatory)][string]$Path, [int]$MaximumCharacters = 4000)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    $content = [IO.File]::ReadAllText($Path)
    if ($content.Length -gt $MaximumCharacters) {
        $content = $content.Substring($content.Length - $MaximumCharacters)
        Write-Output "(showing final $MaximumCharacters characters)"
    }
    if (-not [string]::IsNullOrWhiteSpace($content)) { Write-Output $content }
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$lockPath = Join-Path $repoRoot 'toolchain-lock.json'
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
    throw "Toolchain lock does not exist: $lockPath"
}
$inputPath = [IO.Path]::GetFullPath($InputAssemblyPath)
if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) {
    throw "Input assembly does not exist: $inputPath"
}
if ((Get-Item -LiteralPath $inputPath).Length -eq 0) {
    throw "Input assembly is empty: $inputPath"
}
if ($OutputRoot -notmatch '^(?:[A-Za-z]:[\\/]|[\\/]{2}[^\\/]+[\\/][^\\/]+[\\/])') {
    throw "OutputRoot must be a fully qualified path below C:\dev\helworks\builds\helengine-ps3: $OutputRoot"
}
$resolvedOutputRoot = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\', '/')
$allowedOutputRoot = [IO.Path]::GetFullPath('C:\dev\helworks\builds\helengine-ps3').TrimEnd('\', '/')
$allowedOutputPrefix = $allowedOutputRoot + [IO.Path]::DirectorySeparatorChar
if (-not $resolvedOutputRoot.StartsWith($allowedOutputPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must be a strict descendant of ${allowedOutputRoot}: $resolvedOutputRoot"
}
if (Test-Path -LiteralPath $resolvedOutputRoot -PathType Leaf) {
    throw "Output root is a file: $resolvedOutputRoot"
}
$existingOutputAncestor = $resolvedOutputRoot
while (-not (Test-Path -LiteralPath $existingOutputAncestor)) {
    $nextAncestor = Split-Path -Parent $existingOutputAncestor
    if ([string]::IsNullOrWhiteSpace($nextAncestor) -or $nextAncestor -eq $existingOutputAncestor) {
        throw "No existing directory ancestor for output root: $resolvedOutputRoot"
    }
    $existingOutputAncestor = $nextAncestor
}
if (-not (Test-Path -LiteralPath $existingOutputAncestor -PathType Container)) {
    throw "Output root ancestor is not a directory: $existingOutputAncestor"
}

$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($null -eq $lock.ResolvedImage -or [string]::IsNullOrWhiteSpace([string]$lock.ResolvedImage.ImageId)) {
    throw 'toolchain-lock.json has no verified ResolvedImage.ImageId.'
}
if ([string]::IsNullOrWhiteSpace($ImageId)) { $ImageId = [string]$lock.ResolvedImage.ImageId }
if ($ImageId -notmatch '^sha256:[0-9a-fA-F]{64}$') {
    throw "ImageId must be a full immutable sha256:<64 hex digits> Docker ID: $ImageId"
}
$compilerPath = [string]$lock.ResolvedImage.CgcompPath
if ($compilerPath -notmatch '^/[A-Za-z0-9_./-]+$') {
    throw "ResolvedImage.CgcompPath is missing or invalid: '$compilerPath'"
}
$mountTarget = [string]$lock.ShaderToolkit.MountTarget
$libraryPath = [string]$lock.ShaderToolkit.LibraryPath
if ($mountTarget -ne '/opt/cg' -or $libraryPath -notmatch '^[A-Za-z0-9_./-]+$') {
    throw 'Locked Cg mount target or library path is invalid.'
}
$mountSourceRelative = [string]$lock.ShaderToolkit.MountSource
if ([string]::IsNullOrWhiteSpace($mountSourceRelative) -or [IO.Path]::IsPathRooted($mountSourceRelative)) {
    throw "ShaderToolkit.MountSource must be a repository-relative path: '$mountSourceRelative'"
}
$cgMountSource = [IO.Path]::GetFullPath((Join-Path $repoRoot ($mountSourceRelative -replace '/', '\')))
$repoPrefix = $repoRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not $cgMountSource.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Locked Cg mount source must remain inside the repository: $cgMountSource"
}
if (-not (Test-Path -LiteralPath $cgMountSource -PathType Container)) {
    throw "Locked Cg mount source does not exist: $cgMountSource"
}
$cgLibrary = Join-Path $cgMountSource ($libraryPath -replace '/', '\')
if (-not (Test-Path -LiteralPath $cgLibrary -PathType Leaf)) {
    throw "Locked Cg library does not exist: $cgLibrary"
}
$cgLibraryHash = (Get-FileHash -LiteralPath $cgLibrary -Algorithm SHA256).Hash.ToLowerInvariant()
if ($cgLibraryHash -ne ([string]$lock.ShaderToolkit.LibCgSha256).ToLowerInvariant()) {
    throw "Locked Cg library digest mismatch: expected $($lock.ShaderToolkit.LibCgSha256), found $cgLibraryHash."
}
$cgLibraryDirectory = Split-Path -Parent $libraryPath
$cgLibrarySearchPath = "$mountTarget/$($cgLibraryDirectory -replace '\\', '/')"

$inputHashBefore = Get-Sha256 -Path $inputPath
$dockerCommand = Get-Command docker -ErrorAction SilentlyContinue
if ($null -eq $dockerCommand) { throw 'Docker CLI is not available on PATH.' }
$inspectOutput = @(& $dockerCommand.Source image inspect $ImageId '--format' '{{.Id}}' 2>&1)
$inspectExit = $LASTEXITCODE
if ($inspectExit -ne 0) { throw "Docker image is unavailable ($inspectExit): $ImageId" }
$resolvedImageId = (@($inspectOutput | ForEach-Object { [string]$_ }) -join '').Trim()
if ($resolvedImageId -notmatch '^sha256:[0-9a-fA-F]{64}$') {
    throw "Docker returned an invalid inspected image ID: '$resolvedImageId'"
}
if (-not $resolvedImageId.Equals($ImageId, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Docker image ID mismatch: supplied '$ImageId', inspected '$resolvedImageId'."
}
Write-Output "Resolved Docker image ID: $resolvedImageId"

# All supplied paths and required SDK inputs are validated before the first write.
New-Item -ItemType Directory -Path $resolvedOutputRoot -Force | Out-Null
$runDirectory = Join-Path $resolvedOutputRoot ("run-{0}-{1}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), ([guid]::NewGuid().ToString('N')))
New-Item -ItemType Directory -Path $runDirectory | Out-Null
$stdoutPath = Join-Path $runDirectory 'cgcomp.stdout.log'
$stderrPath = Join-Path $runDirectory 'cgcomp.stderr.log'
$outputPath = Join-Path $runDirectory 'program.bin'

$dockerArgs = @(
    'run', '--rm', '--network', 'none',
    '--ulimit', 'core=0',
    '--env', "LD_LIBRARY_PATH=$cgLibrarySearchPath",
    '--mount', "type=bind,source=$inputPath,target=/input/assembly.dump,readonly",
    '--mount', "type=bind,source=$cgMountSource,target=$mountTarget,readonly",
    '--mount', "type=bind,source=$runDirectory,target=/out",
    '--entrypoint', '/bin/bash',
    $resolvedImageId,
    '-lc', "exec '$compilerPath' -f -a /input/assembly.dump /out/program.bin"
)
Write-Output "Running cgcomp in $runDirectory"
$dockerRunError = $null
try {
    $ErrorActionPreference = 'Continue'
    & $dockerCommand.Source @dockerArgs 1> $stdoutPath 2> $stderrPath
    $compilerExitCode = $LASTEXITCODE
}
catch {
    $dockerRunError = $_.Exception.Message
    $compilerExitCode = -1
}
finally {
    $ErrorActionPreference = 'Stop'
    $inputHashAfter = Get-Sha256 -Path $inputPath
    if ($inputHashAfter -eq $inputHashBefore) {
        Write-Output "PASS input sha256 unchanged=$inputHashAfter"
    }
    else {
        Write-Output "FAIL input sha256 changed: before=$inputHashBefore after=$inputHashAfter"
    }
}
if ($inputHashAfter -ne $inputHashBefore) {
    throw "Input assembly changed during regression: before=$inputHashBefore after=$inputHashAfter"
}
if ($null -ne $dockerRunError) {
    throw "Docker invocation failed: $dockerRunError"
}

if ($compilerExitCode -ne 0) {
    Write-Output "cgcomp/Docker exit code: $compilerExitCode"
    Write-Output 'stdout (bounded):'
    Write-BoundedLogTail -Path $stdoutPath
    Write-Output 'stderr (bounded):'
    Write-BoundedLogTail -Path $stderrPath
    throw "cgcomp failed with exit code $compilerExitCode; see logs in $runDirectory"
}
if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
    throw "cgcomp exited successfully but produced no fragment binary: $outputPath"
}
$bytes = [IO.File]::ReadAllBytes($outputPath)
if ($bytes.Length -lt 36) { throw "Fragment binary is too short ($($bytes.Length) bytes; minimum 36): $outputPath" }
$magic = (([int]$bytes[0] -shl 8) -bor [int]$bytes[1])
if ($magic -ne 0x4650) { throw ('Fragment binary magic mismatch: expected 0x4650, found 0x{0:X4}.' -f $magic) }
$instructionCount = (([int]$bytes[10] -shl 8) -bor [int]$bytes[11])
if ($instructionCount -le 0) { throw "Fragment binary has an invalid instruction count: $instructionCount" }
$ucodeOffset = ([long]$bytes[20] -shl 24) -bor ([long]$bytes[21] -shl 16) -bor ([long]$bytes[22] -shl 8) -bor [long]$bytes[23]
$ucodeLength = [long]$instructionCount * 16
if ($ucodeOffset -lt 36 -or ($ucodeOffset % 16) -ne 0) {
    throw "Microcode offset must be at least 36 and 16-byte aligned; found $ucodeOffset."
}
if (($ucodeOffset + $ucodeLength) -gt $bytes.Length) {
    throw "Microcode region is out of bounds: offset=$ucodeOffset length=$ucodeLength fileLength=$($bytes.Length)."
}
if ($instructionCount -le 1024) {
    throw "Capacity regression was not exercised: expected >1024 instructions, found $instructionCount."
}

Write-Output "PASS cgcomp exit=$compilerExitCode"
Write-Output ('PASS fragment magic=0x{0:X4} instructions={1} ucodeOffset={2} ucodeBytes={3} fileBytes={4}' -f $magic, $instructionCount, $ucodeOffset, $ucodeLength, $bytes.Length)
Write-Output "Output directory: $runDirectory"
