<#
.SYNOPSIS
    Measures the PPU ABI facts the PlayStation 3 codegen profile depends on.

.DESCRIPTION
    The generated Core C++ profile must state the target's pointer size and byte order, and
    the v0.2 design commits to enabling exceptions and RTTI. Guessing any of those produces
    a generated tree that miscompiles or misbehaves in ways that look like unrelated bugs,
    so every value is measured inside the locked SDK image rather than assumed.

    Two kinds of measurement are produced:

    - Compile-time facts, read from the cross compiler's own predefined macros. These are
      authoritative for pointer size, long size and byte order, and need nothing to run.
    - A runtime probe executable that throws and catches an exception and performs a
      dynamic_cast, then prints every value. Proving unwinding really works on GameOS
      requires running it, which scripts/launch_in_emulator.ps1 does in a later step.

    Outputs land in .ps3\toolchain\core-abi-<utc>\ and the measured
    values are written to abi-facts.json next to the probe artifacts.

    Command contract:
        powershell -NoProfile -File scripts/probe-core-abi.ps1
#>
[CmdletBinding()]
param(
    [Alias('ExternalRoot')][string]$StateRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $repoRoot 'scripts\ps3-paths.ps1')
$StateRoot = Resolve-Ps3StateRoot -RepoRoot $repoRoot -RequestedRoot $StateRoot
$lockPath = Join-Path $repoRoot 'toolchain-lock.json'
$timestamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')

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
        Runs a native command with stdout and stderr merged and throws on a nonzero exit.
        The error preference is relaxed inside the function because Windows PowerShell 5.1
        turns redirected native stderr into terminating errors; the exit code decides.
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

function Read-KeyValueLines {
    param([string[]]$Lines)
    $values = @{}
    foreach ($line in $Lines) {
        $text = [string]$line
        $separator = $text.IndexOf('=')
        if ($separator -gt 0) {
            $values[$text.Substring(0, $separator).Trim()] = $text.Substring($separator + 1).Trim()
        }
    }
    return $values
}

# --- Locked image ------------------------------------------------------------------
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
    throw "Toolchain lock does not exist: $lockPath"
}
$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($null -eq $lock.ResolvedImage -or [string]::IsNullOrWhiteSpace([string]$lock.ResolvedImage.ImageId)) {
    throw 'toolchain-lock.json has no ResolvedImage.ImageId. Run scripts/prepare_toolchain.ps1 first.'
}
$imageId = [string]$lock.ResolvedImage.ImageId

$probeRoot = Join-Path $StateRoot "toolchain\core-abi-$timestamp"
if (Test-Path -LiteralPath $probeRoot) { throw "Probe directory already exists: $probeRoot" }
New-Item -ItemType Directory -Path $probeRoot | Out-Null

# --- Probe sources -----------------------------------------------------------------
# The runtime probe prints the same facts the macro dump reports, plus the two that can
# only be established by running: that a thrown exception is caught, and that a
# dynamic_cast resolves. Output goes to stdout, which RPCS3 captures as TTY.
Set-Content -LiteralPath (Join-Path $probeRoot 'abi.cpp') -Encoding Ascii -Value @(
    '#include <cstdio>',
    '#include <stdexcept>',
    '#include <string>',
    '',
    '#include <sys/process.h>',
    '',
    'SYS_PROCESS_PARAM(1001, 0x100000)',
    '',
    '/// Polymorphic base used to prove dynamic_cast resolves at run time.',
    'struct ProbeBase { virtual ~ProbeBase() {} };',
    '',
    '/// Derived type the probe casts down to.',
    'struct ProbeDerived : ProbeBase { int marker = 7; };',
    '',
    'int main() {',
    '    std::printf("PS3_ABI_BEGIN\n");',
    '    std::printf("PointerSizeBytes=%d\n", static_cast<int>(sizeof(void*)));',
    '    std::printf("LongSizeBytes=%d\n", static_cast<int>(sizeof(long)));',
    '    std::printf("SizeTBytes=%d\n", static_cast<int>(sizeof(std::size_t)));',
    '    unsigned int orderProbe = 1u;',
    '    std::printf("Endianness=%s\n", (*reinterpret_cast<unsigned char*>(&orderProbe)) == 1u ? "little" : "big");',
    '    int caught = 0;',
    '    std::string message;',
    '    try {',
    '        throw std::runtime_error("ps3-abi-probe");',
    '    } catch (const std::exception& error) {',
    '        caught = 1;',
    '        message = error.what();',
    '    }',
    '    std::printf("ExceptionsWork=%d\n", caught);',
    '    std::printf("ExceptionMessage=%s\n", message.c_str());',
    '    ProbeDerived derived;',
    '    ProbeBase* base = &derived;',
    '    ProbeDerived* roundTrip = dynamic_cast<ProbeDerived*>(base);',
    '    std::printf("RttiWork=%d\n", (roundTrip != nullptr && roundTrip->marker == 7) ? 1 : 0);',
    '    std::printf("StdStringWorks=%d\n", message.size() > 0 ? 1 : 0);',
    '    std::printf("PS3_ABI_END\n");',
    '    return 0;',
    '}'
)

# --- In-image measurement ----------------------------------------------------------
# Encoded so no embedded quote survives Windows PowerShell's native argument quoting.
$containerScript = @'
set -e
cd /probe

# Compile-time facts straight from the cross compiler's predefined macros.
ppu-g++ -std=c++17 -mcpu=cell -dM -E -x c++ /dev/null > macros.txt
printf 'MacroPointerSizeBytes=%s\n' "$(grep -E '^#define __SIZEOF_POINTER__ ' macros.txt | awk '{print $3}')"
printf 'MacroLongSizeBytes=%s\n' "$(grep -E '^#define __SIZEOF_LONG__ ' macros.txt | awk '{print $3}')"
printf 'MacroSizeTBytes=%s\n' "$(grep -E '^#define __SIZEOF_SIZE_T__ ' macros.txt | awk '{print $3}')"
# Byte order is resolved by the preprocessor rather than by string-matching the macro
# dump: GCC defines __BYTE_ORDER__ as the token __ORDER_BIG_ENDIAN__, not as a number,
# so comparing the dumped values textually reports the wrong answer.
cat > endian.cpp <<'ENDIAN_PROBE'
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
MacroEndianness=big
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
MacroEndianness=little
#else
MacroEndianness=unknown
#endif
ENDIAN_PROBE
ppu-g++ -std=c++17 -mcpu=cell -E -P -x c++ endian.cpp | grep -E '^MacroEndianness='

# Build the runtime probe with exceptions and RTTI explicitly enabled.
ppu-g++ -std=c++17 -Wall -Wextra -mcpu=cell -mhard-float -fmodulo-sched \
  -fexceptions -frtti -isystem "$PSL1GHT/ppu/include" \
  abi.cpp -L"$PSL1GHT/ppu/lib" -lrt -llv2 -lm -o abi.elf
printf 'ExceptionsCompile=1\n'
printf 'RttiCompile=1\n'

# Confirm the unwinding machinery is actually linked in rather than merely accepted.
if ppu-nm abi.elf | grep -qE '__cxa_throw|_Unwind_RaiseException'; then
  printf 'UnwindSymbolsLinked=1\n'
else
  printf 'UnwindSymbolsLinked=0\n'
fi

ppu-readelf -h abi.elf > abi-elf-header.txt
grep -E 'Class|Data|Machine' abi-elf-header.txt | sed 's/^ *//'

# Cross-check the compiler's answer against the linked ELF header, which is the
# authority for what actually runs on the console.
if grep -q 'big endian' abi-elf-header.txt; then
  printf 'HeaderEndianness=big\n'
else
  printf 'HeaderEndianness=little\n'
fi

# Package a SELF through the same SDK rules the normal build uses.
mkdir -p self-stage
ppu-strip abi.elf -o self-stage/abi.elf
sprxlinker self-stage/abi.elf
make_self self-stage/abi.elf abi.self > /dev/null
printf 'SelfBytes=%s\n' "$(stat -c %s abi.self)"
'@
$encodedScript = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($containerScript))

$dockerArgs = @(
    'run', '--rm', '--network', 'none',
    '--mount', "type=bind,source=$probeRoot,target=/probe",
    '--workdir', '/probe',
    '--entrypoint', '/bin/bash',
    $imageId,
    '-c', "echo $encodedScript | base64 -d | bash -e"
)
Write-Output "docker $($dockerArgs -join ' ')"
$output = Invoke-CheckedNative 'docker' $dockerArgs
$output | Set-Content -LiteralPath (Join-Path $probeRoot 'probe.log') -Encoding UTF8
$output | ForEach-Object { Write-Output "  $_" }

$facts = Read-KeyValueLines $output
foreach ($required in @('MacroPointerSizeBytes', 'MacroEndianness', 'HeaderEndianness', 'ExceptionsCompile', 'UnwindSymbolsLinked')) {
    if (-not $facts.ContainsKey($required) -or [string]::IsNullOrWhiteSpace([string]$facts[$required])) {
        throw "ABI probe did not report $required. See $(Join-Path $probeRoot 'probe.log')"
    }
}
if ($facts['MacroEndianness'] -ne $facts['HeaderEndianness']) {
    throw "The compiler reports '$($facts['MacroEndianness'])' byte order but the linked ELF header reports '$($facts['HeaderEndianness'])'. Resolve the disagreement before setting the codegen profile."
}
if ($facts['HeaderEndianness'] -ne 'big') {
    throw "Expected a big-endian target; the ELF header reports '$($facts['HeaderEndianness'])'."
}
if ($facts['UnwindSymbolsLinked'] -ne '1') {
    throw 'The probe linked without unwinding symbols; exceptions would not survive at run time.'
}

foreach ($artifact in @('abi.elf', 'abi.self', 'abi-elf-header.txt')) {
    $artifactPath = Join-Path $probeRoot $artifact
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf) -or (Get-Item -LiteralPath $artifactPath).Length -eq 0) {
        throw "ABI probe did not produce a nonempty $artifact"
    }
}

# --- Record ------------------------------------------------------------------------
# These are the compile-time and link-time facts only. Whether exceptions actually unwind
# on the console is established by running the emitted SELF; that result is recorded in
# docs/CoreAbiProbe.md, which is the authority for the runtime half.
$record = [PSCustomObject]@{
    SchemaVersion = 1
    MeasuredAtUtc = $timestamp
    ImageId = $imageId
    ProbeDirectory = $probeRoot
    PointerSizeBytes = [int]$facts['MacroPointerSizeBytes']
    LongSizeBytes = [int]$facts['MacroLongSizeBytes']
    SizeTBytes = [int]$facts['MacroSizeTBytes']
    Endianness = [string]$facts['MacroEndianness']
    ExceptionsCompile = $facts['ExceptionsCompile'] -eq '1'
    RttiCompile = $facts['RttiCompile'] -eq '1'
    UnwindSymbolsLinked = $facts['UnwindSymbolsLinked'] -eq '1'
    LaunchArtifact = Join-Path $probeRoot 'abi.self'
}
$recordPath = Join-Path $probeRoot 'abi-facts.json'
[IO.File]::WriteAllText($recordPath, (($record | ConvertTo-Json -Depth 4) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))

# Emit a manifest in the launcher's schema so the probe SELF is launched through the same
# containment, identity and hash verification as a normal build artifact instead of being
# started directly. The probe is not a product build, so its mode is named accordingly.
$probeSelfPath = Join-Path $probeRoot 'abi.self'
$probeElfPath = Join-Path $probeRoot 'abi.elf'
$manifest = [PSCustomObject]@{
    SchemaVersion = 1
    BuildId = Split-Path -Leaf $probeRoot
    SourceCommit = ((Invoke-CheckedNative 'git' @('-C', $repoRoot, 'rev-parse', 'HEAD')) -join '').Trim()
    SourceDirty = @(Invoke-CheckedNative 'git' @('-C', $repoRoot, 'status', '--porcelain', '--untracked-files=all') | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }).Count -gt 0
    Mode = 'CoreAbiProbe'
    FixedPose = $false
    RunSeconds = 0
    FailurePoint = 'None'
    ToolchainLockSha256 = (Get-FileHash -LiteralPath $lockPath -Algorithm SHA256).Hash.ToLowerInvariant()
    ImageId = $imageId
    LaunchArtifact = 'abi.self'
    Artifacts = @(
        [PSCustomObject]@{ Path = 'abi.self'; Sha256 = (Get-FileHash -LiteralPath $probeSelfPath -Algorithm SHA256).Hash.ToLowerInvariant(); Bytes = (Get-Item -LiteralPath $probeSelfPath).Length }
        [PSCustomObject]@{ Path = 'abi.elf'; Sha256 = (Get-FileHash -LiteralPath $probeElfPath -Algorithm SHA256).Hash.ToLowerInvariant(); Bytes = (Get-Item -LiteralPath $probeElfPath).Length }
    )
}
$manifestTemporaryPath = Join-Path $probeRoot 'build-manifest.json.tmp'
[IO.File]::WriteAllText($manifestTemporaryPath, (($manifest | ConvertTo-Json -Depth 6) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $manifestTemporaryPath -Destination (Join-Path $probeRoot 'build-manifest.json')

Write-Output ''
Write-Output "Pointer size: $($record.PointerSizeBytes) bytes. Endianness: $($record.Endianness). Unwind symbols linked: $($record.UnwindSymbolsLinked)."
Write-Output "Facts written to $recordPath"
Write-Output "Run the probe to verify unwinding at run time:"
Write-Output "  powershell -NoProfile -File scripts/launch_in_emulator.ps1 -ArtifactPath $(Join-Path $probeRoot 'abi.self') -Visible"
