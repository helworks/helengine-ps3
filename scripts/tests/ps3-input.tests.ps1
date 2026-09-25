<# Builds the pad regression executable with the pinned SDK and the actual generated InputSystem. #>
[CmdletBinding()]
param(
    [string]$StandardRoot = 'C:/dev/helworks/builds/helengine-ps3/2d-batch-20260924/standard/output/core-4d09240df135406585e6468beab56b18',
    [string]$GeneratedRoot = 'C:/dev/helworks/builds/helengine-ps3/2d-batch-20260924/standard/cache/v2/d87d222cd166d9049967e97c50c8e301/b/ps3/release/release/generated-core',
    [switch]$Run,
    [string]$RpcS3Path = 'C:/dev/helworks/emus/rpcs3-v0.0.42-20013-2d103407/rpcs3.exe'
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$image = 'sha256:63a28f67dacb0a6d80c165645af126c7d6760e6a3b02e03b6e08aa5aeb96c40f'
$buildId = 'input-' + [guid]::NewGuid().ToString('N')
$buildRoot = "C:/dev/helworks/builds/helengine-ps3/2d-batch-20260924/native-pad-input/$buildId"
New-Item -ItemType Directory -Path "$buildRoot/temp" -Force | Out-Null
$coreObject = "$StandardRoot/obj/generated/helengine_core_unity.o"
$coreHash = (Get-FileHash $coreObject).Hash
$standardManifest = Get-Content "$StandardRoot/build-manifest.json" -Raw | ConvertFrom-Json
function Get-GeneratedFingerprint {
    <# Matches the standard builder's sorted relative-path/hash fingerprint. #>
    $root = [IO.Path]::GetFullPath($GeneratedRoot).TrimEnd('\', '/')
    $records = @(Get-ChildItem $root -Recurse -File | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($root.Length).TrimStart('\', '/').Replace('\', '/')
        $relative + '=' + (Get-FileHash $_.FullName).Hash.ToLowerInvariant()
    })
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes([string]::Join("`n", $records)))).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}
$generatedHash = Get-GeneratedFingerprint
if ($generatedHash -ne $standardManifest.GeneratedCoreSha256 -or $standardManifest.ImageId -ne $image) { throw 'Generated headers or SDK do not match the supplied standard build.' }
$before = @{}
foreach ($path in @('src/platform/ps3/Ps3InputBackend.cpp','src/platform/ps3/Ps3InputBackend.hpp','src/platform/ps3/Ps3PadApi.hpp','tests/ps3_input_backend_tests.cpp','tests/Makefile.input')) {
    $before[$path] = (Get-FileHash "$repo/$path").Hash
}
$oldTemp = @{}
foreach ($name in @('TEMP','TMP','TMPDIR')) {
    $oldTemp[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    [Environment]::SetEnvironmentVariable($name, "$buildRoot/temp", 'Process')
}
try {
    $ErrorActionPreference = 'Continue'
    & docker run --rm --pull=never --network none --mount "type=bind,source=$repo,target=/src,readonly" --mount "type=bind,source=$buildRoot,target=/out" --mount "type=bind,source=$StandardRoot,target=/standard,readonly" --mount "type=bind,source=$GeneratedRoot,target=/generated,readonly" --env TMPDIR=/out/temp $image make -f /src/tests/Makefile.input all *> "$buildRoot/build.log"
    $buildExit = $LASTEXITCODE
} finally {
    $ErrorActionPreference = 'Stop'
    foreach ($name in $oldTemp.Keys) { [Environment]::SetEnvironmentVariable($name, $oldTemp[$name], 'Process') }
}
$log = Get-Content "$buildRoot/build.log" -Raw
$log.Substring([Math]::Max(0, $log.Length - 4000))
"BuildRoot=$buildRoot"
if ($buildExit -ne 0) { throw "Input fixture build failed: $buildExit" }
foreach ($path in $before.Keys) {
    if ((Get-FileHash "$repo/$path").Hash -ne $before[$path]) { throw "Source changed during compilation: $path" }
}
if ((Get-FileHash $coreObject).Hash -ne $coreHash -or (Get-GeneratedFingerprint) -ne $generatedHash) { throw 'Generated inputs changed during compilation.' }
$artifact = "$buildRoot/ps3_input_backend_tests.self"
$manifest = [ordered]@{
    SchemaVersion = 1; BuildId = $buildId; TestKind = 'Ps3InputBackend'
    ToolchainLockSha256 = (Get-FileHash "$repo/toolchain-lock.json").Hash.ToLowerInvariant()
    ToolchainImageId = $image; SourceHashes = $before; StandardCoreObjectSha256 = $coreHash; GeneratedCoreSha256 = $generatedHash
    LaunchArtifact = 'ps3_input_backend_tests.self'
    Artifacts = @(@{ Path = 'ps3_input_backend_tests.self'; Sha256 = (Get-FileHash $artifact).Hash.ToLowerInvariant() })
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content "$buildRoot/build-manifest.json" -Encoding UTF8
if ($Run) {
    if (Get-Process rpcs3 -ErrorAction SilentlyContinue) { throw 'An existing RPCS3 instance is reserved; refusing to replace it.' }
    & "$repo/scripts/launch_in_emulator.ps1" -ArtifactPath $artifact -OutputRoot C:/dev/helworks/builds/helengine-ps3 -RpcS3Path $RpcS3Path
    $tty = Get-ChildItem "$buildRoot/emulator-runs" -Filter TTY.log -Recurse | Sort-Object LastWriteTime | Select-Object -Last 1
    if (!$tty) { throw 'Missing input fixture TTY evidence.' }
    $result = Get-Content $tty.FullName -Raw
    $result.Substring([Math]::Max(0, $result.Length - 4000))
    if ($result -notmatch 'PS3_INPUT_BACKEND_TESTS passed=\d+ failed=0' -or $result -match '(?m)^FAIL ') { throw 'Input regression test failed.' }
}
