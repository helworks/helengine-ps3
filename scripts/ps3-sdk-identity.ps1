# A Docker classic store addresses an imported OCI image by its config digest.
# Keep the manifest digest as the build/cache identity; prove the local alias.
function Read-Ps3SdkImageProof {
    param([string]$CanonicalImageId, [string]$IdentityRoot)
    if ($CanonicalImageId -cnotmatch '^sha256:[0-9a-f]{64}$') { throw 'Invalid canonical SDK digest.' }
    $manifestPath = Join-Path $IdentityRoot 'manifest.json'
    $configPath = Join-Path $IdentityRoot 'config.json'
    $manifestDigest = 'sha256:' + (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($manifestDigest -cne $CanonicalImageId) { throw 'SDK manifest bytes do not match the locked digest.' }
    $manifest = [IO.File]::ReadAllText($manifestPath) | ConvertFrom-Json
    if ($manifest.schemaVersion -ne 2 -or $manifest.mediaType -cnotin @('application/vnd.oci.image.manifest.v1+json','application/vnd.docker.distribution.manifest.v2+json')) { throw 'Unsupported SDK image manifest.' }
    $configDigest = 'sha256:' + (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($configDigest -cne $manifest.config.digest -or (Get-Item -LiteralPath $configPath).Length -ne $manifest.config.size) { throw 'SDK config bytes do not match the manifest descriptor.' }
    $config = [IO.File]::ReadAllText($configPath) | ConvertFrom-Json
    if ($config.os -cne 'linux' -or $config.architecture -cne 'amd64' -or $config.rootfs.type -cne 'layers') { throw 'SDK config is not a linux/amd64 layered image.' }
    $layers = @($config.rootfs.diff_ids)
    if ($layers.Count -eq 0) { throw 'SDK config contains no filesystem layers.' }
    foreach ($layer in $layers) { if ($layer -cnotmatch '^sha256:[0-9a-f]{64}$') { throw 'Invalid SDK filesystem digest.' } }
    return [pscustomobject]@{ CanonicalImageId=$CanonicalImageId; ConfigImageId=$configDigest; Layers=$layers; ManifestSha256=$manifestDigest.Substring(7); ConfigSha256=$configDigest.Substring(7) }
}

function Resolve-Ps3SdkImageIdentity {
    param([string]$CanonicalImageId, $Proof, $InspectedImage)
    if ($Proof.CanonicalImageId -cne $CanonicalImageId) { throw 'SDK proof belongs to another manifest.' }
    if ($InspectedImage.Id -cnotin @($CanonicalImageId,$Proof.ConfigImageId)) { throw 'Local SDK image is unrelated to the locked manifest.' }
    if ($InspectedImage.Os -cne 'linux' -or $InspectedImage.Architecture -cne 'amd64' -or $InspectedImage.RootFS.Type -cne 'layers') { throw 'Local SDK platform differs from the manifest.' }
    $actualLayers = @($InspectedImage.RootFS.Layers)
    if ($actualLayers.Count -ne $Proof.Layers.Count) { throw 'Local SDK filesystem layer count differs.' }
    for ($i=0; $i -lt $actualLayers.Count; $i++) { if ($actualLayers[$i] -cne $Proof.Layers[$i]) { throw 'Local SDK filesystem layers differ.' } }
    return [pscustomobject]@{ CanonicalImageId=$CanonicalImageId; LocalImageId=[string]$InspectedImage.Id; ManifestSha256=$Proof.ManifestSha256; ConfigSha256=$Proof.ConfigSha256 }
}

function Get-Ps3SdkImageIdentity {
    param([string]$CanonicalImageId, [string]$IdentityRoot = $env:HELENGINE_PS3_SDK_IDENTITY_ROOT)
    if ([string]::IsNullOrWhiteSpace($IdentityRoot)) {
        $actual = ((Invoke-CheckedNative 'docker' @('image','inspect',$CanonicalImageId,'--format','{{.Id}}')) -join '').Trim()
        if ($actual -cne $CanonicalImageId) { throw "Locked SDK image $CanonicalImageId is not available locally (docker reported '$actual')." }
        return [pscustomobject]@{CanonicalImageId=$CanonicalImageId; LocalImageId=$actual; ManifestSha256=$null;ConfigSha256=$null}
    }
    $proof = Read-Ps3SdkImageProof $CanonicalImageId $IdentityRoot
    # Config digest is the local address produced by docker load in a classic store.
    # Missing images can fall back to the canonical address in a containerd store.
    $metadata = $null
    foreach ($candidate in @($proof.ConfigImageId,$CanonicalImageId)) {
        try {
            $metadata = ((Invoke-CheckedNative 'docker' @('image','inspect',$candidate)) -join [Environment]::NewLine) | ConvertFrom-Json
            break
        } catch {
            if ($_.Exception.Message -notmatch '(?i)(No such image|image.*not found)') { throw }
        }
    }
    if ($null -eq $metadata) { throw 'Neither the verified SDK config nor its canonical manifest is installed locally.' }
    $images = @($metadata)
    if ($images.Count -ne 1) { throw 'Expected exactly one SDK image.' }
    return Resolve-Ps3SdkImageIdentity $CanonicalImageId $proof $images[0]
}
