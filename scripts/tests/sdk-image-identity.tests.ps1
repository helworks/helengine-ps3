param([Parameter(Mandatory)][string]$FixtureRoot)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../ps3-sdk-identity.ps1')
New-Item -ItemType Directory -Force -Path $FixtureRoot | Out-Null
function Write-JsonBytes($name, $value) {
    $path = Join-Path $FixtureRoot $name
    [IO.File]::WriteAllText($path, ($value | ConvertTo-Json -Depth 10 -Compress), [Text.UTF8Encoding]::new($false))
    return $path
}
function Must-Fail([scriptblock]$Action) {
    $failed = $false
    try { & $Action | Out-Null } catch { $failed = $true }
    if (-not $failed) { throw 'Expected identity rejection.' }
}
$layerA = 'sha256:' + ('a' * 64)
$layerB = 'sha256:' + ('b' * 64)
$configPath = Write-JsonBytes 'config.json' @{ os='linux'; architecture='amd64'; rootfs=@{type='layers'; diff_ids=@($layerA,$layerB)} }
$configId = 'sha256:' + (Get-FileHash $configPath -Algorithm SHA256).Hash.ToLowerInvariant()
$manifestPath = Write-JsonBytes 'manifest.json' @{schemaVersion=2; mediaType='application/vnd.oci.image.manifest.v1+json'; config=@{mediaType='application/vnd.oci.image.config.v1+json';digest=$configId;size=(Get-Item $configPath).Length};layers=@(@{digest=$layerA},@{digest=$layerB})}
$pin = 'sha256:' + (Get-FileHash $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$proof = Read-Ps3SdkImageProof $pin $FixtureRoot
foreach ($actualId in @($pin,$configId)) {
    $actual = [pscustomobject]@{Id=$actualId;Os='linux';Architecture='amd64';RootFS=@{Type='layers';Layers=@($layerA,$layerB)}}
    $result = Resolve-Ps3SdkImageIdentity $pin $proof $actual
    if ($result.LocalImageId -cne $actualId -or $result.CanonicalImageId -cne $pin) { throw 'Identity changed.' }
}
$actual.Id = 'sha256:' + ('c' * 64)
Must-Fail { Resolve-Ps3SdkImageIdentity $pin $proof $actual }
$actual.Id = $configId
$actual.RootFS.Layers = @($layerB,$layerA)
Must-Fail { Resolve-Ps3SdkImageIdentity $pin $proof $actual }
$actual.RootFS.Layers = @($layerA,$layerB)
$actual.Architecture = 'arm64'
Must-Fail { Resolve-Ps3SdkImageIdentity $pin $proof $actual }
Must-Fail { Read-Ps3SdkImageProof ('sha256:' + ('d' * 64)) $FixtureRoot }
[IO.File]::AppendAllText($configPath, ' ')
Must-Fail { Read-Ps3SdkImageProof $pin $FixtureRoot }
[IO.File]::AppendAllText($manifestPath, ' ')
Must-Fail { Read-Ps3SdkImageProof $pin $FixtureRoot }
Write-Output 'PASS: classic/containerd identity; rejected unknown ID, reordered layers, platform, manifest and config corruption.'
