<#
.SYNOPSIS
    Registers the PlayStation 3 platform in an isolated engine user-settings root.

.DESCRIPTION
    The editor discovers platforms by reading platforms.json from an engine user-settings
    root and matching each entry's engineVersion against the version the caller asks for.
    That version is the target project's requiredEngineVersion, not the engine's own commit,
    so this script reads it from the project file rather than hardcoding a value that would
    silently stop matching.

    The entry is written into an isolated root under the repository-local .ps3 directory
    than into C:\dev\helworks\helengine\user_settings\platforms.json. That file is live,
    untracked and shared with every other platform on this machine, and a mistake there
    would break unrelated targets. Point the editor at this root by exporting
    HELENGINE_ENGINE_USER_SETTINGS_ROOT, which the script prints on success.

    Command contract:
        powershell -NoProfile -File scripts/register-platform.ps1
        powershell -NoProfile -File scripts/register-platform.ps1 -BuilderAssemblyPath 'C:\build output\helengine.ps3.builder.dll'
#>
[CmdletBinding()]
param(
    [string]$ProjectFile = 'C:\dev\helprojs\demodisc\project.heproj',
    [Alias('ExternalRoot')][string]$StateRoot = '',
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$BuilderAssemblyPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $repoRoot 'scripts\ps3-paths.ps1')

if ([string]::IsNullOrWhiteSpace($BuilderAssemblyPath)) {
    $builderAssembly = Join-Path $repoRoot "builder\bin\$Configuration\net9.0\helengine.ps3.builder.dll"
} else {
    # Windows PowerShell 5/.NET Framework may not provide IsPathFullyQualified.
    # Reject drive-relative and root-relative paths instead of resolving them via cwd.
    $isPosixAbsolute = [IO.Path]::DirectorySeparatorChar -eq '/' -and $BuilderAssemblyPath.StartsWith('/')
    if (-not $isPosixAbsolute -and $BuilderAssemblyPath -notmatch '^(?:[A-Za-z]:[\\/]|[\\/]{2}[^\\/]+[\\/][^\\/]+[\\/])') {
        throw 'BuilderAssemblyPath must be fully qualified.'
    }
    $builderAssembly = [IO.Path]::GetFullPath($BuilderAssemblyPath)
}

$StateRoot = Resolve-Ps3StateRoot -RepoRoot $repoRoot -RequestedRoot $StateRoot
$settingsRoot = Join-Path $StateRoot 'user-settings'
$manifestPath = Join-Path $settingsRoot 'platforms.json'
# Pinned explicitly rather than left to the editor's default, so the regenerated tree lands
# somewhere known and can be compiled directly by scripts/build.ps1 -Mode Core without
# hunting through build caches for it.
$generatedCoreRoot = Join-Path $StateRoot 'generated-core'
$pluginManifest = Join-Path $repoRoot 'platform-plugin.json'

# --- Inputs -------------------------------------------------------------------------
if (-not (Test-Path -LiteralPath $ProjectFile -PathType Leaf)) {
    throw "Project file does not exist: $ProjectFile"
}
if (-not (Test-Path -LiteralPath $builderAssembly -PathType Leaf)) {
    throw "Builder assembly does not exist: $builderAssembly`nBuild it first: dotnet build builder/helengine.ps3.builder.csproj -c $Configuration"
}
if (-not (Test-Path -LiteralPath $pluginManifest -PathType Leaf)) {
    throw "Plugin manifest does not exist: $pluginManifest"
}

$project = Get-Content -LiteralPath $ProjectFile -Raw -Encoding UTF8 | ConvertFrom-Json
$requiredEngineVersion = [string]$project.requiredEngineVersion
if ([string]::IsNullOrWhiteSpace($requiredEngineVersion)) {
    throw "Project file does not declare requiredEngineVersion: $ProjectFile"
}

# Discovery compares this string ordinally, so a stale builder would be invisible rather
# than merely out of date. Report the assembly's write time so that is obvious.
$builderWriteTime = (Get-Item -LiteralPath $builderAssembly).LastWriteTimeUtc.ToString('yyyy-MM-dd HH:mm:ss')

# --- Existing manifest ---------------------------------------------------------------
$entries = @()
if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
    $existing = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($null -ne $existing.platforms) {
        foreach ($entry in @($existing.platforms)) {
            if ([string]$entry.platformId -ne 'ps3') {
                throw "Refusing to modify an isolated registry that already holds platform '$($entry.platformId)'. Inspect $manifestPath and remove it deliberately."
            }
        }
    }
}

New-Item -ItemType Directory -Path $settingsRoot -Force | Out-Null
# The generated-core directory must exist before discovery runs. A declared path that is
# absent makes the resolver treat the whole platform as not installed, which surfaces much
# later and far less clearly as "no loaded platform builder is registered for 'ps3'".
New-Item -ItemType Directory -Path $generatedCoreRoot -Force | Out-Null

$manifest = [PSCustomObject]@{
    platforms = @(
        [PSCustomObject]@{
            engineVersion = $requiredEngineVersion
            platformId = 'ps3'
            displayName = 'PlayStation 3'
            builderAssemblyPath = $builderAssembly
            playerSourceRootPath = $repoRoot
            generatedCoreCppRootPath = $generatedCoreRoot
            pluginManifestPath = $pluginManifest
        }
    )
}
[IO.File]::WriteAllText($manifestPath, (($manifest | ConvertTo-Json -Depth 6) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))

Write-Output "Registered ps3 in $manifestPath"
Write-Output "  engineVersion        $requiredEngineVersion  (from $ProjectFile)"
Write-Output "  builderAssemblyPath  $builderAssembly"
Write-Output "  builder built        $builderWriteTime UTC"
Write-Output "  playerSourceRootPath $repoRoot"
Write-Output "  generatedCoreCppRoot $generatedCoreRoot"
Write-Output ''
Write-Output 'Set these before running the platform build:'
Write-Output "  `$env:HELENGINE_ENGINE_USER_SETTINGS_ROOT = '$settingsRoot'"
Write-Output "  `$env:HELENGINE_PS3_REPOSITORY_ROOT = '$repoRoot'"
