<#
.SYNOPSIS
Builds selected original authored scenes into a PlayStation 3 SELF through the editor.

.DESCRIPTION
The native build links a SELF from a generated core and staged content.
This produces those inputs the way a real build does: it runs the
editor's own headless build graph, which cooks the project's assets, compiles
its scripts, regenerates the core and then calls this repository's builder.

Nothing in the authoring project is modified. A copy is staged, and the build
runs against the copy.

Two things make the copy necessary.

The editor selects project code by module, not by script. Every .cs file under
a folder without its own code.module.json belongs to one reserved module, so
referencing any script in that module compiles all of them. This target has no
C++ exceptions, and project code outside the scene in question uses constructs
that the fatal-only runtime rejects, which stops the build on a file the scene
never touches. Pruning the staged copy to the scripts the scene actually
references is the only way to keep those files out, and it is what the
PlayStation 1 port does for the same reason.

The staged copy also carries its own build settings and its own engine
registry, both written here from the engine revision that is actually being
built. The editor compares the registry's engine version to the project's
required version as a plain string and silently ignores entries that differ,
so deriving both from one value is what stops that mismatch from presenting as
the platform not existing.

.EXAMPLE
./scripts/build-authored.ps1 -ProjectRoot C:\dev\helprojs\demodisc `
    -EngineSourceRoot C:\dev\helworks\helengine-camera-fov `
    -EditorAppPath C:\dev\helworks\builds\helengine-ps3\editor\helengine.editor.app.dll `
    -OutputRoot C:\dev\helworks\builds\helengine-ps3\authored-02
#>
[CmdletBinding()]
param(
    # Authoring project that owns the scene. Never modified; a staged copy is built.
    [Parameter(Mandatory = $true)][string] $ProjectRoot,
    # Engine checkout that owns the editor build graph and the Core sources.
    [Parameter(Mandatory = $true)][string] $EngineSourceRoot,
    # helengine.editor.app.dll published from that checkout.
    [Parameter(Mandatory = $true)][string] $EditorAppPath,
    # csharpcodegen checkout whose codegen/codegen.csproj transpiles the core and the
    # project's scripts. Defaults to the engine's own pinned submodule, so a build uses
    # the generator the engine commit names rather than one chosen here.
    [string] $CodegenRoot = '',
    # Fresh output root, conventionally under C:\dev\helworks\builds\helengine-ps3.
    [Parameter(Mandatory = $true)][string] $OutputRoot,
    # Rebuild the caller-owned active output in place rather than retaining a new history.
    [switch] $ReuseOutput,
    # Use the verified codegen already published with the selected central editor.
    [switch] $UsePublishedCodegen,
    [string] $SceneRelativePath = 'scenes/rendering/cube_test.helen',
    [string] $SceneId = 'cube_test',
    # Additional original authored scenes needed by menu actions, in catalog order.
    # These arrays are paired; the startup scene remains first.
    [string[]] $AdditionalSceneRelativePaths = @(),
    [string[]] $AdditionalSceneIds = @(),
    # Optional reviewed preparation of the staged copy, before script dependency pruning.
    [string] $StagedProjectPreparationScript = '',
    # Scripts to keep beyond those the scene names. A retained script that calls a helper
    # in the same module needs the helper kept too, and the scene does not mention it.
    [string[]] $AdditionalRetainedClasses = @(),
    # The dotnet host. Named because it is not always on PATH on this machine.
    [string] $DotNet = 'dotnet'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($AdditionalSceneRelativePaths.Count -ne $AdditionalSceneIds.Count) {
    throw 'AdditionalSceneRelativePaths and AdditionalSceneIds must have matching counts.'
}
$scenePaths = @($SceneRelativePath) + @($AdditionalSceneRelativePaths)
$sceneIds = @($SceneId) + @($AdditionalSceneIds)
if (@($scenePaths | Sort-Object -Unique).Count -ne $scenePaths.Count -or @($sceneIds | Sort-Object -Unique).Count -ne $sceneIds.Count) {
    throw 'Selected scene paths and catalog ids must be unique.'
}
foreach ($relativePath in $scenePaths) {
    if ([IO.Path]::IsPathRooted($relativePath) -or $relativePath.Replace('\', '/').Split('/') -contains '..') {
        throw "Selected scene must be relative to assets: $relativePath"
    }
}
foreach ($catalogId in $sceneIds) {
    if ([string]::IsNullOrWhiteSpace($catalogId)) { throw 'Selected scene catalog ids must not be empty.' }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$projectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$engineRoot = (Resolve-Path -LiteralPath $EngineSourceRoot).Path
$editorApp = (Resolve-Path -LiteralPath $EditorAppPath).Path
$codegenRoot = if ([string]::IsNullOrWhiteSpace($CodegenRoot)) {
    Join-Path $engineRoot 'engine\vendor\csharpcodegen'
} else {
    (Resolve-Path -LiteralPath $CodegenRoot).Path
}
$codegenProject = Join-Path $codegenRoot 'codegen\codegen.csproj'
if (-not (Test-Path -LiteralPath $codegenProject -PathType Leaf)) {
    throw "codegen project is missing: $codegenProject. Run: git -C $engineRoot submodule update --init --recursive"
}
$output = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $output) {
    if (-not $ReuseOutput) { throw "Use a fresh authored build output directory or explicitly select ReuseOutput: $output" }
    if (-not $output.StartsWith('C:\dev\helworks\builds\helengine-ps3\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Active output must stay in the owned platform build root.' }
    $previous = Get-Content (Join-Path $output 'authored-build.json') -Raw | ConvertFrom-Json
    if ([IO.Path]::GetFullPath($previous.project_root) -ne $projectRoot -or [IO.Path]::GetFullPath($previous.engine_source_root) -ne $engineRoot) { throw 'Active output receipt does not match this source project and engine.' }
} elseif ($ReuseOutput) { throw "ReuseOutput requires an existing reviewed active build: $output" }

New-Item -ItemType Directory -Path $output -Force | Out-Null
$logsRoot = Join-Path $output 'logs'
New-Item -ItemType Directory -Path $logsRoot -Force | Out-Null

# 1. Build this repository's platform builder against the same engine checkout the
# editor was built from, so both load one set of base-platform types.
$builderArtifacts = Join-Path $output 'builder'
& rtk proxy $DotNet build (Join-Path $repoRoot 'builder\helengine.ps3.builder.csproj') -c Debug --artifacts-path $builderArtifacts -p:HelengineRoot=$engineRoot -v:q -nologo *> (Join-Path $logsRoot 'builder-build.log')
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $logsRoot 'builder-build.log') -Tail 20; throw 'The PlayStation 3 platform builder failed to build.' }
$builderAssembly = Join-Path $builderArtifacts 'bin\helengine.ps3.builder\debug\helengine.ps3.builder.dll'
if (-not (Test-Path -LiteralPath $builderAssembly -PathType Leaf)) { throw "Builder assembly was not produced: $builderAssembly" }

# 2. Stage the project: assets and settings only. The authoring project keeps its own
# user_settings, caches and build outputs, none of which belong to this build.
$engineVersion = '1.0.0+' + ((& rtk proxy git -c "safe.directory=$engineRoot" -C $engineRoot rev-parse HEAD | Out-String).Trim())
if ($engineVersion -notmatch '^1\.0\.0\+[0-9a-f]{40}$') { throw "Could not resolve the engine revision from $engineRoot" }
$stagedProject = Join-Path $output 'staged-project'
New-Item -ItemType Directory -Path $stagedProject -Force | Out-Null
foreach ($relative in @('assets', 'settings')) {
    $sourcePath = Join-Path $projectRoot $relative
    if (-not (Test-Path -LiteralPath $sourcePath)) { throw "Project is missing ${relative}: $sourcePath" }
    & rtk proxy robocopy $sourcePath (Join-Path $stagedProject $relative) /E /NFL /NDL /NJH /NJS /NP /XD bin obj .generated-build | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Staging $relative failed with robocopy exit code $LASTEXITCODE" }
}
if (-not [string]::IsNullOrWhiteSpace($StagedProjectPreparationScript)) {
    $preparationScript = (Resolve-Path -LiteralPath $StagedProjectPreparationScript).Path
    & $preparationScript -StagedProjectRoot $stagedProject
}
$stagedScene = Join-Path $stagedProject ('assets\' + $SceneRelativePath.Replace('/', '\'))
if (-not (Test-Path -LiteralPath $stagedScene -PathType Leaf)) { throw "Staged project lacks the scene to build: $stagedScene" }
$selectedScenes = @()
for ($sceneIndex = 0; $sceneIndex -lt $scenePaths.Count; $sceneIndex++) {
    $selectedPath = Join-Path $stagedProject ('assets\' + $scenePaths[$sceneIndex].Replace('/', '\'))
    if (-not (Test-Path -LiteralPath $selectedPath -PathType Leaf)) { throw "Staged scene is missing: $selectedPath" }
    $selectedText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($selectedPath))
    $assetIdMatch = [regex]::Match($selectedText, '(?<![0-9a-f])[0-9a-f]{32}(?![0-9a-f])')
    if (-not $assetIdMatch.Success) { throw "No authoring asset id was found in $selectedPath." }
    $selectedScenes += [pscustomobject]@{
        relativePath = $scenePaths[$sceneIndex]
        sceneId = $sceneIds[$sceneIndex]
        assetId = $assetIdMatch.Value
        contentHash = 'sha256:' + (Get-FileHash -LiteralPath $selectedPath -Algorithm SHA256).Hash.ToLowerInvariant()
        text = $selectedText
    }
}
$sceneHash = $selectedScenes[0].contentHash.Substring(7)
$sceneText = ($selectedScenes | ForEach-Object { $_.text }) -join "`n"

# 3. Prune project code to the scripts the scene names, plus whatever those need.
# The scene records each script component as "Namespace.Type, assembly", so the scene
# itself is the list of what to keep; reading it is better than maintaining a list here
# that would go stale the moment the scene changes.
$retainedScriptClasses = @()
foreach ($match in [regex]::Matches($sceneText, '([A-Za-z_][A-Za-z0-9_\.]*)\s*,\s*[A-Za-z_][A-Za-z0-9_\.]*')) {
    $typeName = $match.Groups[1].Value
    if ($typeName.StartsWith('helengine.')) { continue }
    if (-not $typeName.Contains('.')) { continue }
    $retainedScriptClasses += $typeName.Substring($typeName.LastIndexOf('.') + 1)
}
$sceneNamedClasses = @($retainedScriptClasses + $AdditionalRetainedClasses | Sort-Object -Unique)
if ($sceneNamedClasses.Count -eq 0) { throw "No project script components were found in $stagedScene." }

$codebaseRoot = Join-Path $stagedProject 'assets\codebase'
if (-not (Test-Path -LiteralPath $codebaseRoot)) { throw "Staged project has no codebase: $codebaseRoot" }
$removedModules = @()
foreach ($moduleFile in Get-ChildItem -LiteralPath $codebaseRoot -Recurse -Filter 'code.module.json') {
    $module = Get-Content -LiteralPath $moduleFile.FullName -Raw | ConvertFrom-Json
    if ([string]$module.moduleKind -ne 'runtime') {
        $removedModules += [string]$module.moduleId
        if (-not $moduleFile.DirectoryName.StartsWith($codebaseRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Module removal must stay beneath the staged codebase: $($moduleFile.DirectoryName)"
        }
        Remove-Item -LiteralPath $moduleFile.DirectoryName -Recurse -Force
    }
}

# A component the scene names is rarely self-contained: it calls helpers, and the scene
# says nothing about those. Keeping only the named components fails to compile on the
# first helper. So the retained set is closed over what the retained files actually
# mention, one file name at a time, until it stops growing. A file is pulled in only
# because a file already being kept names its type, which is the same reason the
# compiler would need it.
$classFiles = @{}
foreach ($script in Get-ChildItem -LiteralPath $codebaseRoot -Recurse -Filter '*.cs') {
    $className = [IO.Path]::GetFileNameWithoutExtension($script.Name)
    if (-not $classFiles.ContainsKey($className)) { $classFiles[$className] = $script.FullName }
}
$retainedScriptClasses = [Collections.Generic.HashSet[string]]::new([string[]]$sceneNamedClasses)
$pending = [Collections.Generic.Queue[string]]::new([string[]]$sceneNamedClasses)
while ($pending.Count -gt 0) {
    $className = $pending.Dequeue()
    if (-not $classFiles.ContainsKey($className)) { continue }
    $body = [IO.File]::ReadAllText($classFiles[$className])
    foreach ($candidate in $classFiles.Keys) {
        if ($retainedScriptClasses.Contains($candidate)) { continue }
        if ($body -match ('(?<![A-Za-z0-9_])' + [regex]::Escape($candidate) + '(?![A-Za-z0-9_])')) {
            [void]$retainedScriptClasses.Add($candidate)
            $pending.Enqueue($candidate)
        }
    }
}
$retainedScriptClasses = @($retainedScriptClasses | Sort-Object)

$retainedScripts = @()
$removedScripts = @()
foreach ($script in Get-ChildItem -LiteralPath $codebaseRoot -Recurse -Filter '*.cs') {
    $className = [IO.Path]::GetFileNameWithoutExtension($script.Name)
    if ($retainedScriptClasses -contains $className) {
        $retainedScripts += $script.FullName.Substring($stagedProject.Length + 1).Replace('\', '/')
        continue
    }
    $removedScripts += $script.FullName.Substring($stagedProject.Length + 1).Replace('\', '/')
    Remove-Item -LiteralPath $script.FullName -Force
    $sidecar = $script.FullName + '.hmeta'
    if (Test-Path -LiteralPath $sidecar) { Remove-Item -LiteralPath $sidecar -Force }
}
# A component the scene names but whose script is gone would fail at load with nothing
# to point at, so it fails here instead, naming the file.
foreach ($className in $sceneNamedClasses) {
    if (-not ($retainedScripts | Where-Object { $_.EndsWith('/' + $className + '.cs') })) {
        throw "The scene names component '$className' but the project codebase has no $className.cs."
    }
}
[ordered]@{
    sceneNamedClasses = $sceneNamedClasses
    retainedScriptClasses = $retainedScriptClasses
    retainedScripts = $retainedScripts
    removedScripts = $removedScripts
    removedModules = $removedModules
} | ConvertTo-Json -Depth 4 | Out-File -LiteralPath (Join-Path $output 'code-prune-report.json') -Encoding utf8

# 4. Point the staged project at this engine revision and declare the platform.
$projectDocument = Get-Content -LiteralPath (Join-Path $projectRoot 'project.heproj') -Raw | ConvertFrom-Json
$projectDocument.requiredEngineVersion = $engineVersion
$projectDocument.supportedPlatforms = @($projectDocument.supportedPlatforms | Where-Object { $_ -ne 'ps3' }) + @('ps3')
$projectDocument | ConvertTo-Json -Depth 8 | Out-File -LiteralPath (Join-Path $stagedProject 'project.heproj') -Encoding utf8
$platformsSettingsPath = Join-Path $stagedProject 'settings\platforms.json'
$platformsSettings = if (Test-Path -LiteralPath $platformsSettingsPath) {
    Get-Content -LiteralPath $platformsSettingsPath -Raw | ConvertFrom-Json
} else {
    [pscustomobject]@{ supportedPlatforms = @() }
}
$platformsSettings.supportedPlatforms = @($platformsSettings.supportedPlatforms | Where-Object { $_ -ne 'ps3' }) + @('ps3')
$platformsSettings | ConvertTo-Json -Depth 4 | Out-File -LiteralPath $platformsSettingsPath -Encoding utf8

# 5. The headless build reads its scene selection and profile ids from the project's own
# build configuration and refuses a platform that has no entry, so write one.
$editorOutput = Join-Path $output 'editor-output'
$buildConfig = [ordered]@{
    platforms = @(
        [ordered]@{
            platformId = 'ps3'
            overridesProjectScenes = $true
            selectedSceneReferences = @($selectedScenes | ForEach-Object {
                [ordered]@{ sourceKind = 1; relativePath = $_.relativePath; providerId = ''; assetId = $_.assetId; contentHash = $_.contentHash }
            })
            selectedSceneIds = @($sceneIds)
            sceneOrders = @(for ($sceneIndex = 0; $sceneIndex -lt $sceneIds.Count; $sceneIndex++) {
                [ordered]@{ sceneId = $sceneIds[$sceneIndex]; orderNumber = $sceneIndex + 1 }
            })
            outputDirectoryPath = $editorOutput
            debugBuild = $false
            selectedEnvironmentId = 'release'
            selectedBuildProfileId = 'release'
            editorPrebuildCommandIdsByBuildProfileId = [ordered]@{}
            selectedGraphicsProfileId = 'ps3-standard'
            selectedBuildOptionValues = [ordered]@{}
            selectedGraphicsOptionValues = [ordered]@{}
            selectedCodegenProfileId = 'default'
            selectedStorageProfileId = 'loose-files'
            selectedMediaProfileId = 'ps3-self'
            selectedCodegenOptionValues = [ordered]@{}
        }
    )
    queueItems = @()
}
New-Item -ItemType Directory -Path (Join-Path $stagedProject 'user_settings') -Force | Out-Null
$buildConfig | ConvertTo-Json -Depth 8 | Out-File -LiteralPath (Join-Path $stagedProject 'user_settings\build_config.json') -Encoding utf8

# 6. A build-local engine registry, so this build neither reads nor disturbs the
# machine-local one another session may be editing.
$generatedCoreRoot = Join-Path $output 'generated-core'
New-Item -ItemType Directory -Path $generatedCoreRoot -Force | Out-Null
$engineUserSettings = Join-Path $output 'engine-user-settings'
New-Item -ItemType Directory -Path $engineUserSettings -Force | Out-Null
$publishedCodegen = Join-Path (Split-Path -Parent $editorApp) 'codegen'
[ordered]@{
    platforms = @(
        [ordered]@{
            engineVersion = $engineVersion
            platformId = 'ps3'
            displayName = 'PlayStation 3'
            builderAssemblyPath = $builderAssembly
            playerSourceRootPath = $repoRoot
            # Required by engine revisions before the field was retired; later ones
            # ignore it with a warning, so carrying it works with both.
            codegenToolPath = (Join-Path $publishedCodegen 'codegen.exe')
            generatedCoreCppRootPath = $generatedCoreRoot
            pluginManifestPath = ''
        }
    )
} | ConvertTo-Json -Depth 8 | Out-File -LiteralPath (Join-Path $engineUserSettings 'platforms.json') -Encoding utf8

# 7. Publish codegen beside the editor, which is the first place the editor looks.
# It is cleared first: dotnet publish leaves files it considers current, so publishing a
# different csharpcodegen over an earlier one leaves one commit's tool beside another
# commit's bundled runtime headers, which compiles a core the headers do not describe.
$publishedCodegen = [IO.Path]::GetFullPath($publishedCodegen)
if (-not $UsePublishedCodegen) {
    if (-not $publishedCodegen.StartsWith('C:\dev\helworks\builds\helengine-ps3\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Codegen output must stay in this platform build root.' }
    if (Test-Path -LiteralPath $publishedCodegen) { Remove-Item -LiteralPath $publishedCodegen -Recurse -Force }
    $codegenPublishLog = Join-Path $logsRoot 'codegen-publish.log'
    & rtk proxy $DotNet publish $codegenProject -c Release -o $publishedCodegen --artifacts-path (Join-Path $output 'codegen-artifacts') -v:q --nologo *> $codegenPublishLog
    if ($LASTEXITCODE -ne 0) { Get-Content $codegenPublishLog -Tail 20; throw "Publishing codegen beside the editor failed with exit code $LASTEXITCODE." }
}
if (-not (Test-Path -LiteralPath (Join-Path $publishedCodegen 'codegen.exe') -PathType Leaf)) {
    throw "codegen publish did not produce the tool: $publishedCodegen"
}
$codegenCommit = (& rtk proxy git -c "safe.directory=$codegenRoot" -C $codegenRoot rev-parse HEAD 2>$null | Out-String).Trim()

# 8. Run the editor's headless build graph against the staged project.
$workspaceRoot = Join-Path $output 'workspace'
New-Item -ItemType Directory -Path $workspaceRoot -Force | Out-Null
$env:HELENGINE_SOURCE_ROOT = $engineRoot
$env:HELENGINE_ENGINE_USER_SETTINGS_ROOT = $engineUserSettings
$env:HELENGINE_BUILD_WORKSPACE_ROOT = $workspaceRoot
$env:HELENGINE_PS3_REPOSITORY_ROOT = $repoRoot
$editorLog = Join-Path $logsRoot 'editor-build.log'
& rtk proxy $DotNet $editorApp --project (Join-Path $stagedProject 'project.heproj') --build ps3 --build-profile release --output $editorOutput *> $editorLog
$editorExit = $LASTEXITCODE

[ordered]@{
    schema = 1
    created_utc = [DateTime]::UtcNow.ToString('o')
    engine_version = $engineVersion
    engine_source_root = $engineRoot
    editor_app = $editorApp
    codegen_root = $codegenRoot
    codegen_commit = $codegenCommit
    builder_assembly = $builderAssembly
    project_root = $projectRoot
    scene_relative_path = $SceneRelativePath
    scene_id = $SceneId
    staged_project_preparation_script = $StagedProjectPreparationScript
    scene_sha256 = $sceneHash
    selected_scenes = @($selectedScenes | ForEach-Object {
        [ordered]@{ scene_relative_path = $_.relativePath; scene_id = $_.sceneId; asset_id = $_.assetId; content_hash = $_.contentHash }
    })
    retained_script_classes = $retainedScriptClasses
    staged_project = $stagedProject
    generated_core_root = $generatedCoreRoot
    workspace_root = $workspaceRoot
    editor_output = $editorOutput
    editor_exit_code = $editorExit
    reused_output = [bool]$ReuseOutput
    reused_published_codegen = [bool]$UsePublishedCodegen
} | ConvertTo-Json -Depth 4 | Out-File -LiteralPath (Join-Path $output 'authored-build.json') -Encoding utf8

if ($editorExit -ne 0) {
    Get-Content -LiteralPath $editorLog -Tail 40
    throw "The editor's headless build for ps3 failed with exit code $editorExit (see $editorLog)."
}
Write-Output (Join-Path $output 'authored-build.json')
