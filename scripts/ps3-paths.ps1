<#
.SYNOPSIS
    Provides side-effect-free path resolution for repository-local PS3 state.

.DESCRIPTION
    The functions in this file are dot-sourced by the PS3 PowerShell scripts.
    They do not create directories or perform I/O so callers can validate paths
    before making any build-related changes.
#>

function Resolve-Ps3StateRoot {
    <#
    .SYNOPSIS
        Resolves the PS3 state root, defaulting to the repository's .ps3 directory.
    #>
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [string]$RequestedRoot = ''
    )

    if ([string]::IsNullOrWhiteSpace($RequestedRoot)) {
        return [IO.Path]::GetFullPath((Join-Path $RepoRoot '.ps3'))
    }
    return [IO.Path]::GetFullPath($RequestedRoot)
}

function Resolve-Ps3OutputRoot {
    <#
    .SYNOPSIS
        Resolves the native-output root, defaulting to .ps3/builds.
    #>
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [string]$RequestedRoot = ''
    )

    $stateRoot = Resolve-Ps3StateRoot -RepoRoot $RepoRoot -RequestedRoot $RequestedRoot
    if ([string]::IsNullOrWhiteSpace($RequestedRoot)) {
        return [IO.Path]::GetFullPath((Join-Path $stateRoot 'builds'))
    }
    return $stateRoot
}

function Assert-Ps3PathInsideRoot {
    <#
    .SYNOPSIS
        Confirms that a path is the root itself or a descendant of that root.
    #>
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Root,
        [string]$Label = 'Path'
    )

    $resolvedPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $resolvedRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $prefix = $resolvedRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedPath.Equals($resolvedRoot, [StringComparison]::OrdinalIgnoreCase) -and
        -not $resolvedPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be inside ${resolvedRoot}: $resolvedPath"
    }
    return $resolvedPath
}
