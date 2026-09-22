<#
.SYNOPSIS
    Acquires and verifies the proprietary NVIDIA Cg archive used by cgcomp.

.DESCRIPTION
    The archive is kept outside Git and outside the Docker image layer. This
    helper creates the repository-local cache, downloads the locked archive only
    when it is absent, and verifies its SHA-256 before any caller extracts it.
    The optional downloader exists only to make the hash and reuse behavior
    testable without a network connection.
#>

function Get-Ps3CgFileSha256 {
    <#
    .SYNOPSIS
        Returns a lowercase SHA-256 for one local Cg input file.
    #>
    param([Parameter(Mandatory)][string]$Path)

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Ensure-Ps3CgArchive {
    <#
    .SYNOPSIS
        Downloads or reuses the locked NVIDIA Cg archive and verifies its hash.
    #>
    param(
        [Parameter(Mandatory)]$ShaderToolkit,
        [Parameter(Mandatory)][string]$StateRoot,
        [scriptblock]$DownloadFile = $null
    )

    foreach ($property in @('Archive', 'Url', 'ArchiveSha256')) {
        if (-not ($ShaderToolkit.PSObject.Properties.Name -contains $property) -or
            [string]::IsNullOrWhiteSpace([string]$ShaderToolkit.$property)) {
            throw "ShaderToolkit.$property is required."
        }
    }
    if ([IO.Path]::GetFileName([string]$ShaderToolkit.Archive) -ne [string]$ShaderToolkit.Archive) {
        throw "ShaderToolkit.Archive must be a file name, not a path: $($ShaderToolkit.Archive)"
    }

    $cgRoot = Join-Path $StateRoot 'dependencies\cg'
    New-Item -ItemType Directory -Path $cgRoot -Force | Out-Null
    $archivePath = Join-Path $cgRoot ([string]$ShaderToolkit.Archive)
    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        if ($null -eq $DownloadFile) {
            $DownloadFile = {
                param([string]$Url, [string]$Destination)
                Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Destination
            }
        }
        & $DownloadFile ([string]$ShaderToolkit.Url) $archivePath | Out-Null
    }
    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        throw "Cg toolkit download did not produce an archive: $archivePath"
    }

    $archiveHash = Get-Ps3CgFileSha256 -Path $archivePath
    if ($archiveHash -ne ([string]$ShaderToolkit.ArchiveSha256).ToLowerInvariant()) {
        throw "Cg toolkit archive hash mismatch. Expected $($ShaderToolkit.ArchiveSha256), found $archiveHash."
    }
    return $archivePath
}
