[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$registrationScript = Join-Path $repoRoot 'scripts\register-platform.ps1'
$defaultManifest = Join-Path $repoRoot '.ps3\user-settings\platforms.json'
$resolvedOutputRoot = [IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $resolvedOutputRoot -Force | Out-Null

$testRoot = Join-Path $resolvedOutputRoot ([Guid]::NewGuid().ToString('N'))
$fixtureRoot = Join-Path $testRoot 'authored project fixture'
$assemblyPath = Join-Path $testRoot 'external builder output\Debug build\helengine.ps3.builder.dll'
$projectFile = Join-Path $fixtureRoot 'project.heproj'
$testLog = Join-Path $testRoot 'register-platform.log'
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path -Parent $assemblyPath) -Force | Out-Null
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
[IO.File]::WriteAllText($projectFile, '{"requiredEngineVersion":"test-registration-version"}', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($assemblyPath, 'synthetic builder assembly', [Text.UTF8Encoding]::new($false))

$defaultManifestExisted = Test-Path -LiteralPath $defaultManifest -PathType Leaf
$defaultManifestHash = if ($defaultManifestExisted) {
    (Get-FileHash -LiteralPath $defaultManifest -Algorithm SHA256).Hash
} else {
    ''
}

$failures = [Collections.Generic.List[string]]::new()
function Assert-Registration {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { $script:failures.Add($Message) }
}

function Invoke-Registration {
    param(
        [string]$Label,
        [string]$State,
        [string]$Assembly,
        [string]$Configuration = 'Release'
    )
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script:registrationScript,
        '-ProjectFile', $script:projectFile, '-StateRoot', $State,
        '-Configuration', $Configuration, '-BuilderAssemblyPath', $Assembly)
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $result = & powershell.exe @arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    $resultText = ($result | Out-String)
    Add-Content -LiteralPath $script:testLog -Value "=== $Label (exit $exitCode) ===`n$resultText"
    return [PSCustomObject]@{ ExitCode = $exitCode; Text = $resultText }
}

# Explicit paths with spaces are retained, normalized, and used even for Debug.
$debugState = Join-Path $testRoot 'debug state'
$debugResult = Invoke-Registration -Label 'explicit Debug assembly' -State $debugState -Assembly $assemblyPath -Configuration Debug
Assert-Registration ($debugResult.ExitCode -eq 0) 'Explicit external registration should exit successfully.'
$debugManifestPath = Join-Path $debugState 'user-settings\platforms.json'
if (Test-Path -LiteralPath $debugManifestPath -PathType Leaf) {
    $debugManifest = Get-Content -LiteralPath $debugManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $debugEntry = @($debugManifest.platforms)[0]
    $expectedAssembly = [IO.Path]::GetFullPath($assemblyPath)
    Assert-Registration ($debugEntry.builderAssemblyPath -ceq $expectedAssembly) 'Manifest should preserve the normalized explicit assembly path containing spaces.'
    Assert-Registration ($debugEntry.engineVersion -ceq 'test-registration-version') 'Target project requiredEngineVersion should remain authoritative.'
    Assert-Registration ($debugEntry.playerSourceRootPath -ceq $repoRoot) 'Manifest should retain the repository player source root.'
    Assert-Registration ($debugEntry.generatedCoreCppRootPath -ceq (Join-Path $debugState 'generated-core')) 'Manifest should point to the isolated generated-core root.'
    Assert-Registration ($debugEntry.pluginManifestPath -ceq (Join-Path $repoRoot 'platform-plugin.json')) 'Manifest should retain the repository plugin manifest path.'
} else {
    Assert-Registration $false 'Successful registration should write platforms.json in the requested isolated state root.'
}

# Relative paths, missing files, and directories fail before creating settings or generated roots.
$invalidCases = @(
    [PSCustomObject]@{ Name = 'relative path'; Assembly = 'relative\builder.dll'; Expected = 'fully qualified' },
    [PSCustomObject]@{ Name = 'missing assembly'; Assembly = Join-Path $testRoot 'missing builder.dll'; Expected = 'does not exist' },
    [PSCustomObject]@{ Name = 'directory assembly'; Assembly = $fixtureRoot; Expected = 'does not exist' }
)
foreach ($case in $invalidCases) {
    $invalidState = Join-Path $testRoot ($case.Name -replace '[^A-Za-z0-9]+', '-')
    $result = Invoke-Registration -Label $case.Name -State $invalidState -Assembly $case.Assembly
    Assert-Registration ($result.ExitCode -ne 0) "$($case.Name) should be rejected."
    Assert-Registration ($result.Text -match [regex]::Escape($case.Expected)) "$($case.Name) should report '$($case.Expected)'."
    Assert-Registration (-not (Test-Path -LiteralPath (Join-Path $invalidState 'user-settings'))) "$($case.Name) must not create user-settings."
    Assert-Registration (-not (Test-Path -LiteralPath (Join-Path $invalidState 'generated-core'))) "$($case.Name) must not create generated-core."
}

# A foreign platform in isolated settings is refused without changing the original bytes.
$foreignState = Join-Path $testRoot 'foreign registry state'
$foreignSettings = Join-Path $foreignState 'user-settings'
$foreignManifest = Join-Path $foreignSettings 'platforms.json'
New-Item -ItemType Directory -Path $foreignSettings -Force | Out-Null
$foreignBytes = [Text.Encoding]::UTF8.GetBytes('{"platforms":[{"platformId":"other","marker":"keep bytes"}]}')
[IO.File]::WriteAllBytes($foreignManifest, $foreignBytes)
$foreignHash = (Get-FileHash -LiteralPath $foreignManifest -Algorithm SHA256).Hash
$foreignResult = Invoke-Registration -Label 'foreign registry refusal' -State $foreignState -Assembly $assemblyPath
Assert-Registration ($foreignResult.ExitCode -ne 0) 'Foreign platform settings should be refused.'
Assert-Registration ($foreignResult.Text -match 'Refusing to modify') 'Foreign platform refusal should identify the reason.'
Assert-Registration (((Get-FileHash -LiteralPath $foreignManifest -Algorithm SHA256).Hash) -ceq $foreignHash) 'Foreign platform settings must remain byte-for-byte unchanged.'
Assert-Registration (-not (Test-Path -LiteralPath (Join-Path $foreignState 'generated-core'))) 'Foreign platform refusal must occur before generated-core creation.'

# Backward compatibility: inspect the parameter default without creating a repo-local build output.
$scriptAst = [Management.Automation.Language.Parser]::ParseFile($registrationScript, [ref]$null, [ref]$null)
$assemblyParameter = $scriptAst.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'BuilderAssemblyPath' }
Assert-Registration ($null -ne $assemblyParameter) 'Registration script should expose BuilderAssemblyPath.'
if ($null -ne $assemblyParameter) {
    $defaultText = $assemblyParameter.DefaultValue.Extent.Text
    Assert-Registration ($defaultText -eq "''") 'BuilderAssemblyPath should default to empty so existing callers use the existing configuration-specific path.'
}
$defaultResolution = $scriptAst.FindAll({
    param($node)
    $node -is [Management.Automation.Language.AssignmentStatementAst] -and
        $node.Left -is [Management.Automation.Language.VariableExpressionAst] -and
        $node.Left.VariablePath.UserPath -eq 'builderAssembly' -and
        $node.Right.Extent.Text -match 'Join-Path'
}, $true) | Select-Object -First 1
Assert-Registration ($null -ne $defaultResolution) 'Omitting BuilderAssemblyPath should retain the repository-local configuration-specific assembly resolution.'
if ($null -ne $defaultResolution) {
    Assert-Registration ($defaultResolution.Right.Extent.Text -match 'builder\\bin\\\$Configuration\\net9\.0\\helengine\.ps3\.builder\.dll') 'Default assembly resolution should continue to use the selected configuration directory.'
}

$defaultManifestExistedAfter = Test-Path -LiteralPath $defaultManifest -PathType Leaf
$defaultManifestHashAfter = if ($defaultManifestExistedAfter) {
    (Get-FileHash -LiteralPath $defaultManifest -Algorithm SHA256).Hash
} else {
    ''
}
Assert-Registration ($defaultManifestExistedAfter -eq $defaultManifestExisted) 'Tests must not create or remove the repository default registry.'
Assert-Registration ($defaultManifestHashAfter -ceq $defaultManifestHash) 'Tests must not change the repository default registry.'

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { [Console]::Error.WriteLine("FAIL: $_") }
    throw "register-platform tests failed: $($failures.Count) assertion(s). Log: $testLog"
}

Write-Output "register-platform tests passed. Log: $testLog"
