<#
.SYNOPSIS
    Builds and verifies the custom Helengine PS3 SDK Docker image from toolchain-lock.json.

.DESCRIPTION
    Reads the pinned Inputs section of toolchain-lock.json, records the Docker context
    hashes into it, computes the canonical inputs hash that names the local image tag
    (helengine-ps3-sdk:<inputs-hash>), builds docker/Dockerfile with every pin passed as a
    build argument, verifies the compiler and SDK tools inside the resulting image,
    verifies the read-only NVIDIA Cg toolkit mount, compiles a probe vertex/fragment pair
    through the pinned ppu_rules, and writes the verified image identity into the
    ResolvedImage section. The build log lands under the external PS3 build root.

    Command contract:
        powershell -NoProfile -File scripts/prepare_toolchain.ps1
#>
[CmdletBinding()]
param(
    [string]$ExternalRoot = 'C:\dev\helworks\builds\helengine-ps3',
    [ValidateRange(1, 64)][int]$BuildJobs = 8
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ExternalRoot = [IO.Path]::GetFullPath($ExternalRoot)
$lockPath = Join-Path $repoRoot 'toolchain-lock.json'
$dockerDirectory = Join-Path $repoRoot 'docker'
$dockerfilePath = Join-Path $dockerDirectory 'Dockerfile'
$dockerIgnorePath = Join-Path $dockerDirectory '.dockerignore'
$toolchainRoot = Join-Path $ExternalRoot 'toolchain'
$timestamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')

function Get-FileSha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-TextSha256 {
    param([string]$Text)
    $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $hash = $sha.ComputeHash($bytes)
    }
    finally {
        $sha.Dispose()
    }
    return ([BitConverter]::ToString($hash)).Replace('-', '').ToLowerInvariant()
}

function ConvertTo-JsonStringLiteral {
    param([string]$Value)
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    foreach ($character in $Value.ToCharArray()) {
        switch ($character) {
            '"' { [void]$builder.Append('\"') }
            '\' { [void]$builder.Append('\\') }
            "`n" { [void]$builder.Append('\n') }
            "`r" { [void]$builder.Append('\r') }
            "`t" { [void]$builder.Append('\t') }
            default {
                if ([int]$character -lt 32) {
                    [void]$builder.Append(('\u{0:x4}' -f [int]$character))
                } else {
                    [void]$builder.Append($character)
                }
            }
        }
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function ConvertTo-CanonicalJson {
    <#
        Deterministic compact JSON: object keys sorted ordinally, no whitespace,
        invariant-culture numbers. Used so the image tag hash depends only on the
        declared inputs, never on formatting or property order.
    #>
    param($Value)
    if ($null -eq $Value) { return 'null' }
    if ($Value -is [string]) { return (ConvertTo-JsonStringLiteral $Value) }
    if ($Value -is [bool]) { if ($Value) { return 'true' } else { return 'false' } }
    if ($Value -is [int] -or $Value -is [long] -or $Value -is [double] -or $Value -is [decimal]) {
        return $Value.ToString([Globalization.CultureInfo]::InvariantCulture)
    }
    if ($Value -is [System.Collections.IDictionary]) {
        $parts = @()
        [string[]]$keys = @($Value.Keys | ForEach-Object { [string]$_ })
        [Array]::Sort($keys, [StringComparer]::Ordinal)
        foreach ($key in $keys) {
            $parts += ((ConvertTo-JsonStringLiteral $key) + ':' + (ConvertTo-CanonicalJson $Value[$key]))
        }
        return ('{' + ($parts -join ',') + '}')
    }
    if ($Value -is [System.Collections.IEnumerable]) {
        $parts = @()
        foreach ($item in $Value) { $parts += (ConvertTo-CanonicalJson $item) }
        return ('[' + ($parts -join ',') + ']')
    }
    if ($Value.GetType().Name -eq 'PSCustomObject') {
        $parts = @()
        [string[]]$names = @($Value.PSObject.Properties | ForEach-Object { [string]$_.Name })
        [Array]::Sort($names, [StringComparer]::Ordinal)
        foreach ($name in $names) {
            $parts += ((ConvertTo-JsonStringLiteral $name) + ':' + (ConvertTo-CanonicalJson $Value.$name))
        }
        return ('{' + ($parts -join ',') + '}')
    }
    throw "Unsupported value type in canonical JSON: $($Value.GetType().FullName)"
}

function ConvertTo-LineText {
    <#
        Converts one merged-stream item to text. Windows PowerShell 5.1 wraps native stderr
        lines in ErrorRecords whose string form is the exception type, so the message is used.
    #>
    param($Item)
    if ($Item -is [System.Management.Automation.ErrorRecord]) { return [string]$Item.Exception.Message }
    return [string]$Item
}

function Invoke-CheckedNative {
    <#
        Runs a native command with stdout and stderr merged. Windows PowerShell 5.1 turns
        redirected stderr lines into terminating errors under ErrorActionPreference=Stop,
        so the preference is relaxed only inside this function; the exit code decides.
    #>
    param([string]$FilePath, [string[]]$ArgumentList)
    $ErrorActionPreference = 'Continue'
    $output = @(& $FilePath @ArgumentList 2>&1 | ForEach-Object { ConvertTo-LineText $_ })
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Command failed ($exitCode): $FilePath $($ArgumentList -join ' ')`n$($output -join [Environment]::NewLine)"
    }
    return $output
}

function Invoke-NativeToLog {
    <#
        Runs a native command, streaming merged stdout/stderr to the console and appending
        it to a log file, and returns the exit code. See Invoke-CheckedNative for why the
        error preference is relaxed here.
    #>
    param([string]$FilePath, [string[]]$ArgumentList, [string]$LogPath)
    $ErrorActionPreference = 'Continue'
    $writer = [IO.StreamWriter]::new($LogPath, $true, [Text.UTF8Encoding]::new($false))
    try {
        & $FilePath @ArgumentList 2>&1 | ForEach-Object {
            $line = ConvertTo-LineText $_
            $writer.WriteLine($line)
            Write-Host $line
        }
        $exitCode = $LASTEXITCODE
    }
    finally {
        $writer.Dispose()
    }
    return $exitCode
}

function Read-KeyValueLines {
    param([string[]]$Lines)
    $values = @{}
    foreach ($line in $Lines) {
        $text = [string]$line
        $separator = $text.IndexOf('=')
        if ($separator -gt 0) {
            $values[$text.Substring(0, $separator)] = $text.Substring($separator + 1)
        }
    }
    return $values
}

# --- Host prerequisites -----------------------------------------------------------
foreach ($required in @($lockPath, $dockerfilePath, $dockerIgnorePath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file does not exist: $required"
    }
}
$dockerOsType = ((Invoke-CheckedNative 'docker' @('info', '--format', '{{.OSType}}')) -join '').Trim()
if ($dockerOsType -ne 'linux') {
    throw "Docker must run Linux containers; reported OSType '$dockerOsType'."
}
New-Item -ItemType Directory -Path $toolchainRoot -Force | Out-Null
$prepareLogPath = Join-Path $toolchainRoot "prepare-$timestamp.log"

# --- Resolve lock inputs ----------------------------------------------------------
$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($lock.SchemaVersion -ne 1) { throw "Unsupported toolchain-lock.json SchemaVersion: $($lock.SchemaVersion)" }
$inputs = $lock.Inputs
$inputs.DockerfileSha256 = Get-FileSha256 $dockerfilePath
$inputs.DockerIgnoreSha256 = Get-FileSha256 $dockerIgnorePath
# Repository-owned SDK patches are build inputs too: record each one by name and hash.
$patchDirectory = Join-Path $dockerDirectory 'patches'
$patchRecords = @()
if (Test-Path -LiteralPath $patchDirectory -PathType Container) {
    foreach ($patchFile in (Get-ChildItem -LiteralPath $patchDirectory -Filter '*.patch' -File | Sort-Object Name)) {
        $patchRecords += [PSCustomObject]@{ Name = $patchFile.Name; Sha256 = Get-FileSha256 $patchFile.FullName }
    }
}
if ($inputs.PSObject.Properties.Name -contains 'Psl1ghtPatches') {
    $inputs.Psl1ghtPatches = $patchRecords
} else {
    $inputs | Add-Member -MemberType NoteProperty -Name 'Psl1ghtPatches' -Value $patchRecords
}
$canonicalInputs = ConvertTo-CanonicalJson $inputs
$inputsHash = Get-TextSha256 $canonicalInputs
$imageTag = "helengine-ps3-sdk:$inputsHash"
Write-Output "Inputs hash $inputsHash -> $imageTag"

# --- Build the image --------------------------------------------------------------
$buildArgs = @(
    'build',
    '--file', $dockerfilePath,
    '--tag', $imageTag,
    '--progress', 'plain',
    '--provenance', 'false',
    '--sbom', 'false',
    '--build-arg', "BASE_IMAGE=$($inputs.BaseImage)",
    '--build-arg', "APT_SNAPSHOT=$($inputs.AptSnapshot)",
    '--build-arg', "PS3TOOLCHAIN_REPOSITORY=$($inputs.Ps3ToolchainRepository)",
    '--build-arg', "PS3TOOLCHAIN_COMMIT=$($inputs.Ps3ToolchainCommit)",
    '--build-arg', "PSL1GHT_REPOSITORY=$($inputs.Psl1ghtRepository)",
    '--build-arg', "PSL1GHT_COMMIT=$($inputs.Psl1ghtCommit)",
    '--build-arg', "BUILD_JOBS=$BuildJobs",
    $dockerDirectory
)
Write-Output "docker $($buildArgs -join ' ')"
Set-Content -LiteralPath $prepareLogPath -Value @("# docker $($buildArgs -join ' ')", "# started $timestamp UTC") -Encoding UTF8
$buildExitCode = Invoke-NativeToLog 'docker' $buildArgs $prepareLogPath
if ($buildExitCode -ne 0) {
    throw "docker build failed with exit code $buildExitCode. See $prepareLogPath"
}

# --- Verify the image -------------------------------------------------------------
$imageId = ((Invoke-CheckedNative 'docker' @('image', 'inspect', $imageTag, '--format', '{{.Id}}')) -join '').Trim()
if ($imageId -notmatch '^sha256:[0-9a-f]{64}$') { throw "Docker returned an invalid image ID for ${imageTag}: $imageId" }
$labelToolchain = ((Invoke-CheckedNative 'docker' @('image', 'inspect', $imageTag, '--format', '{{index .Config.Labels \"org.helengine.ps3.ps3toolchain-commit\"}}')) -join '').Trim()
$labelPsl1ght = ((Invoke-CheckedNative 'docker' @('image', 'inspect', $imageTag, '--format', '{{index .Config.Labels \"org.helengine.ps3.psl1ght-commit\"}}')) -join '').Trim()
if ($labelToolchain -ne $inputs.Ps3ToolchainCommit) { throw "Image ps3toolchain label mismatch: $labelToolchain" }
if ($labelPsl1ght -ne $inputs.Psl1ghtCommit) { throw "Image PSL1GHT label mismatch: $labelPsl1ght" }

$verifyScript = @'
set -e
test -x "$PS3DEV/ppu/bin/ppu-gcc"
test -x "$PS3DEV/ppu/bin/ppu-g++"
test -x "$PS3DEV/ppu/bin/ppu-readelf"
test -x "$PS3DEV/bin/cgcomp"
test -x "$PS3DEV/bin/sprxlinker"
test -x "$PS3DEV/bin/make_self"
test -x "$PS3DEV/bin/fself"
test -x "$PS3DEV/bin/bin2s"
test -f "$PSL1GHT/ppu_rules"
test -f "$PSL1GHT/base_rules"
test -f "$PSL1GHT/ppu/lib/librsx.a"
test -f "$PSL1GHT/ppu/lib/libgcm_sys.a"
test -f "$PSL1GHT/ppu/lib/libsysutil.a"
test -f "$PSL1GHT/ppu/include/rsx/rsx.h"
cat "$PS3DEV/helengine/toolchain-record.txt"
printf 'ReadelfPath=%s\n' "$(command -v ppu-readelf)"
printf 'CgcompPath=%s\n' "$(command -v cgcomp)"
printf 'MakePath=%s\n' "$(command -v make)"
printf 'MakeVersion=%s\n' "$(make --version | head -n1)"
printf 'RuntimeAptPackagesSha256=%s\n' "$(sha256sum $PS3DEV/helengine/runtime-apt-packages.txt | cut -d' ' -f1)"
printf 'BuilderAptPackagesSha256=%s\n' "$(sha256sum $PS3DEV/helengine/builder-apt-packages.txt | cut -d' ' -f1)"
'@
# The script is base64-encoded so no embedded quote survives Windows PowerShell's native
# argument quoting; bash decodes and runs it with -e inside the container.
$encodedVerifyScript = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($verifyScript))
$metadataLines = Invoke-CheckedNative 'docker' @('run', '--rm', '--network', 'none', '--entrypoint', '/bin/bash', $imageTag, '-c', "echo $encodedVerifyScript | base64 -d | bash -e")
$metadataLines | ForEach-Object { Write-Output "  $_" }
$metadata = Read-KeyValueLines $metadataLines
if ($metadata['CompilerTarget'] -ne 'powerpc64-ps3-elf') { throw "Unexpected compiler target: $($metadata['CompilerTarget'])" }
if ($metadata['Ps3ToolchainCommit'] -ne $inputs.Ps3ToolchainCommit) { throw "Recorded ps3toolchain commit mismatch: $($metadata['Ps3ToolchainCommit'])" }
if ($metadata['Psl1ghtCommit'] -ne $inputs.Psl1ghtCommit) { throw "Recorded PSL1GHT commit mismatch: $($metadata['Psl1ghtCommit'])" }

# --- Verify the proprietary Cg toolkit mount (kept outside the image) -------------
$shaderToolkit = $lock.ShaderToolkit
$cgRoot = [IO.Path]::GetFullPath($shaderToolkit.ExternalRoot)
$cgArchivePath = Join-Path $cgRoot $shaderToolkit.Archive
$cgMountSource = [IO.Path]::GetFullPath($shaderToolkit.MountSource)
$cgLibraryPath = Join-Path $cgMountSource ($shaderToolkit.LibraryPath -replace '/', '\')
if (-not (Test-Path -LiteralPath $cgArchivePath -PathType Leaf)) {
    throw "NVIDIA Cg toolkit archive is required at $cgArchivePath. Download $($shaderToolkit.Url) (licence: $($shaderToolkit.License)) and place it there; it is never embedded in the image."
}
$cgArchiveHash = Get-FileSha256 $cgArchivePath
if ($cgArchiveHash -ne $shaderToolkit.ArchiveSha256) {
    throw "Cg toolkit archive hash mismatch. Expected $($shaderToolkit.ArchiveSha256), found $cgArchiveHash."
}
if (-not (Test-Path -LiteralPath $cgLibraryPath -PathType Leaf)) {
    Write-Output "Extracting Cg toolkit usr/ tree to $cgMountSource"
    Invoke-CheckedNative 'tar' @('-xzf', $cgArchivePath, '-C', $cgRoot, './usr') | Out-Null
}
if (-not (Test-Path -LiteralPath $cgLibraryPath -PathType Leaf)) {
    throw "Cg toolkit extraction did not produce $cgLibraryPath"
}
$libCgHash = Get-FileSha256 $cgLibraryPath
if (-not [string]::IsNullOrWhiteSpace($shaderToolkit.LibCgSha256) -and $shaderToolkit.LibCgSha256 -ne $libCgHash) {
    throw "libCg.so hash changed. Expected $($shaderToolkit.LibCgSha256), found $libCgHash."
}
$shaderToolkit.LibCgSha256 = $libCgHash

# --- Probe: compile a vertex/fragment pair through the pinned ppu_rules and a PPU ELF
$probeDirectory = Join-Path $toolchainRoot "probe-$timestamp"
New-Item -ItemType Directory -Path $probeDirectory | Out-Null
Set-Content -LiteralPath (Join-Path $probeDirectory 'probe.vcg') -Encoding Ascii -Value @(
    'void main(float3 position : POSITION, float4 color : COLOR0,',
    '          uniform float4x4 mvp,',
    '          out float4 clip : POSITION, out float4 tint : COLOR0) {',
    '    clip = mul(mvp, float4(position, 1.0));',
    '    tint = color;',
    '}'
)
Set-Content -LiteralPath (Join-Path $probeDirectory 'probe.fcg') -Encoding Ascii -Value @(
    'float4 main(float4 tint : COLOR0) : COLOR {',
    '    return tint;',
    '}'
)
Set-Content -LiteralPath (Join-Path $probeDirectory 'probe.cpp') -Encoding Ascii -Value @(
    '#include <cstdio>',
    '#include <sys/process.h>',
    'SYS_PROCESS_PARAM(1001, 0x100000)',
    'int main() { std::printf("PS3_PROBE\n"); return 0; }'
)
Set-Content -LiteralPath (Join-Path $probeDirectory 'Makefile') -Encoding Ascii -Value @(
    'include $(PSL1GHT)/ppu_rules',
    'all: probe.vpo probe.fpo probe.elf',
    'probe.elf: probe.cpp',
    "`t`$(CXX) -std=c++17 -Wall -Wextra -mcpu=cell `$(MACHDEP) `$(LIBPSL1GHT_INC) `$< `$(LIBPSL1GHT_LIB) -lrt -llv2 -o `$@",
    "`t`$(PREFIX)readelf -h `$@ > probe-elf-header.txt"
)
$probeScript = 'set -e; make VERBOSE=1 all && ls -l probe.vpo probe.fpo probe.elf'
$probeArgs = @(
    'run', '--rm', '--network', 'none',
    '--mount', "type=bind,source=$probeDirectory,target=/probe",
    '--mount', "type=bind,source=$cgMountSource,target=$($shaderToolkit.MountTarget),readonly",
    '--workdir', '/probe',
    '--entrypoint', '/bin/bash',
    $imageTag, '-c', $probeScript
)
$probeOutput = Invoke-CheckedNative 'docker' $probeArgs
$probeOutput | Set-Content -LiteralPath (Join-Path $probeDirectory 'probe.log') -Encoding UTF8
foreach ($probeFile in @('probe.vpo', 'probe.fpo', 'probe.elf', 'probe-elf-header.txt')) {
    $probePath = Join-Path $probeDirectory $probeFile
    if (-not (Test-Path -LiteralPath $probePath -PathType Leaf) -or (Get-Item -LiteralPath $probePath).Length -eq 0) {
        throw "Toolchain probe did not produce a nonempty $probeFile. See $(Join-Path $probeDirectory 'probe.log')"
    }
}
$probeHeader = Get-Content -LiteralPath (Join-Path $probeDirectory 'probe-elf-header.txt') -Raw
if ($probeHeader -notmatch 'PowerPC64' -or $probeHeader -notmatch 'big endian') {
    throw "Probe ELF header is not PowerPC64 big endian:`n$probeHeader"
}

# --- Write the resolved identity ---------------------------------------------------
$lock.ResolvedImage = [PSCustomObject]@{
    Tag = $imageTag
    ImageId = $imageId
    InputsHash = $inputsHash
    PreparedAtUtc = $timestamp
    PrepareLog = $prepareLogPath
    CompilerC = $metadata['CompilerC']
    CompilerCxx = $metadata['CompilerCxx']
    CompilerTarget = $metadata['CompilerTarget']
    Binutils = $metadata['Binutils']
    GccPrerequisites = $metadata['GccPrerequisites']
    GccPrerequisiteVerification = $metadata['GccPrerequisiteVerification']
    NewlibPatchSha256 = $metadata['NewlibPatchSha256']
    GccPatchSha256 = $metadata['GccPatchSha256']
    BinutilsPatchSha256 = $metadata['BinutilsPatchSha256']
    Psl1ghtLocalPatches = $metadata['Psl1ghtLocalPatches']
    MachDep = $metadata['MachDep']
    CpuFlag = $metadata['CpuFlag']
    ReadelfPath = $metadata['ReadelfPath']
    CgcompPath = $metadata['CgcompPath']
    MakeVersion = $metadata['MakeVersion']
    SpuToolchain = $metadata['SpuToolchain']
    RuntimeAptPackagesSha256 = $metadata['RuntimeAptPackagesSha256']
    BuilderAptPackagesSha256 = $metadata['BuilderAptPackagesSha256']
    Endian = 'powerpc64-ps3-elf big endian'
    Architecture = 'PowerPC64 (Cell PPU)'
    ProbeDirectory = $probeDirectory
    ProbeVpoSha256 = Get-FileSha256 (Join-Path $probeDirectory 'probe.vpo')
    ProbeFpoSha256 = Get-FileSha256 (Join-Path $probeDirectory 'probe.fpo')
    ProbeElfSha256 = Get-FileSha256 (Join-Path $probeDirectory 'probe.elf')
}
$lockJson = $lock | ConvertTo-Json -Depth 12
[IO.File]::WriteAllText($lockPath, ($lockJson + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
Write-Output "Prepared $imageTag ($imageId). Lock written to $lockPath; log at $prepareLogPath"
