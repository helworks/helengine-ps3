<#[
.SYNOPSIS
    Contract checks for PS3 scene lighting and camera-authored clearing.
]#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-RepoFile([string]$RelativePath) {
    $path = Join-Path $repoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required PS3 rendering source is missing: $RelativePath"
    }
    return Get-Content -LiteralPath $path -Raw
}
function Assert-SourceMatch([string]$Name, [string]$Source, [string]$Pattern) {
    if ($Source -notmatch $Pattern) { throw "$Name failed: expected source pattern '$Pattern'." }
    Write-Output "PASS $Name"
}

$vertex = Read-RepoFile 'src\platform\ps3\Ps3Vertex.hpp'
$rendererHeader = Read-RepoFile 'src\platform\ps3\Ps3RasterRenderer.hpp'
$renderer = Read-RepoFile 'src\platform\ps3\Ps3RasterRenderer.cpp'
$manager = Read-RepoFile 'src\platform\ps3\Ps3RenderManager3D.cpp'
$runtimeAssets = Read-RepoFile 'src\platform\ps3\Ps3RuntimeAssets.hpp'
$videoHeader = Read-RepoFile 'src\platform\ps3\Ps3Video.hpp'
$video = Read-RepoFile 'src\platform\ps3\Ps3Video.cpp'
$vertexShader = Read-RepoFile 'shaders\unlit.vcg'
$fragmentShader = Read-RepoFile 'shaders\unlit.fcg'

Assert-SourceMatch 'Vertex carries cooked normals' $vertex 'float\s+Normal\s*\[3\]'
Assert-SourceMatch 'Lit shader uses a diffuse normal dot product' $vertexShader 'dot\s*\([^\r\n]*normal'
Assert-SourceMatch 'Lit shader receives a material-lit selector' $vertexShader 'uniform\s+float\s+lit'
Assert-SourceMatch 'Renderer reflects and binds the normal attribute' $renderer 'GCM_VERTEX_ATTRIB_NORMAL'
Assert-SourceMatch 'Lit draws use an inverse-transpose normal transform' $manager 'TryInverseTranspose__ref0_out1\(world, normalTransform\)'
Assert-SourceMatch 'Lighting shader consumes the normal transform' $vertexShader 'uniform\s+float4x4\s+normalTransform'
Assert-SourceMatch 'Manager uses cooked drawable material lighting flag' $manager 'get_Materials\(\)[\s\S]*?->Lit'
Assert-SourceMatch 'Manager submits ambient and directional scene lights' $manager 'get_AmbientLights\(\)[\s\S]*?get_DirectionalLights\(\)'
if ($manager -match 'const\s+::CameraClearSettings\s+clearSettings') {
    throw 'Camera clear value remains mutable for generated non-const accessors failed.'
}
Assert-SourceMatch 'Camera clear settings are copied into a mutable value' $manager '::CameraClearSettings\s+clearSettings\s*='
Assert-SourceMatch 'Runtime model records usable cooked normals' $runtimeAssets 'HasUsableNormals'
Assert-SourceMatch 'Camera settings control color clear' $manager 'get_ClearSettings\(\)[\s\S]*?ClearColorEnabled'
Assert-SourceMatch 'Video can defer its default clear until the camera is known' $videoHeader 'BeginFrame\(bool deferClear'
Assert-SourceMatch 'Video supports independent color/depth clear masks' $video 'ClearColorEnabled[\s\S]*?ClearDepthEnabled'
Assert-SourceMatch 'Fragment shader preserves vertex tint' $fragmentShader 'return\s+tint'

Write-Output 'PS3 rendering contract tests passed.'
