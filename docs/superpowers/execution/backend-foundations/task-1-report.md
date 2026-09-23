# Task 1: Shared backend cache policy tests

## Files owned by this task

- builder.tests/ShaderBackendCachePolicyTests.cs
- builder.tests/ShaderCacheTestBackend.cs
- builder.tests/ShaderCachePolicyTestBackend.cs
- builder.tests/ShaderCacheTestStore.cs
- docs/superpowers/execution/backend-foundations/task-1-report.md

The pre-existing ShaderCompileService.cs and IShaderBackendCachePolicy.cs in the sibling helengine checkout were preserved unchanged. The implementation uses IShaderBackendCachePolicy.UseSharedCache, as specified by the execution amendment.

## Coverage

The focused tests exercise the real ShaderCompileService and cover cache bypass for a disabled policy, repeated uncached compilation, unchanged key text across include/options/toolchain changes, backend exception propagation, null and unregistered-target errors, and shared cache miss/hit/seed behavior for legacy and explicitly enabled backends on DirectX11 and Vulkan.

## Validation

RED: Not run. The amended brief forbids removing the pre-existing implementation to force a failure. The first focused run instead established the honest baseline and passed (initial GREEN).

Initial baseline command:

    dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations\dotnet --filter FullyQualifiedName~ShaderBackendCachePolicyTests --verbosity quiet

- Result: 17 passed, 0 failed, 0 skipped; exit code 0.
- Log: C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations\task-1-baseline.log
- Warnings: 41 existing warnings from helengine.core (5 CS0414 and 36 IDE0040).

After splitting the null-request and unregistered-target checks into independent tests, the exact same command above was rerun:

- Result: 18 passed, 0 failed, 0 skipped; exit code 0.
- Log: C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations\task-1-green.log
- Warnings: 0 on this incremental run.

For both commands, process TEMP and TMP were temporarily set to C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations\fixture-storage and restored in a finally block. Test artifacts and logs were kept under the specified workspace-owned build root.

No Docker, editor, or broader acceptance suites were run for this task.

## PowerShell command wrappers

The baseline run used this PowerShell wrapper, including its transcript capture:

    $ErrorActionPreference = 'Stop'; $buildRoot = 'C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations'; $fixtureRoot = Join-Path $buildRoot 'fixture-storage'; New-Item -ItemType Directory -Force -Path $fixtureRoot | Out-Null; $oldTemp = $env:TEMP; $oldTmp = $env:TMP; try { $env:TEMP = $fixtureRoot; $env:TMP = $fixtureRoot; dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path "$buildRoot\dotnet" --filter FullyQualifiedName~ShaderBackendCachePolicyTests --verbosity quiet 2>&1 | Tee-Object -FilePath "$buildRoot\task-1-baseline.log"; $testExit = $LASTEXITCODE; Write-Output "TEST_EXIT_CODE=$testExit"; $summary = Select-String -LiteralPath "$buildRoot\task-1-baseline.log" -Pattern 'Passed!|Failed!|Test Run|error |warning ' | Select-Object -Last 12; $summary | ForEach-Object { $_.Line }; exit $testExit } finally { $env:TEMP = $oldTemp; $env:TMP = $oldTmp }

The final run used this wrapper, redirecting the complete command output to its log:

    $ErrorActionPreference = 'Stop'; $buildRoot = 'C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\backend-foundations'; $fixtureRoot = Join-Path $buildRoot 'fixture-storage'; New-Item -ItemType Directory -Force -Path $fixtureRoot | Out-Null; $oldTemp = $env:TEMP; $oldTmp = $env:TMP; try { $env:TEMP = $fixtureRoot; $env:TMP = $fixtureRoot; dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path "$buildRoot\dotnet" --filter FullyQualifiedName~ShaderBackendCachePolicyTests --verbosity quiet *> "$buildRoot\task-1-green.log"; $testExit = $LASTEXITCODE; Write-Output "TEST_EXIT_CODE=$testExit"; Select-String -LiteralPath "$buildRoot\task-1-green.log" -Pattern 'Passed!|Failed!|Test Run|error |warning ' | Select-Object -Last 30 | ForEach-Object { $_.Line }; $warningCount = (Select-String -LiteralPath "$buildRoot\task-1-green.log" -Pattern ': warning ').Count; Write-Output "WARNING_COUNT=$warningCount"; exit $testExit } finally { $env:TEMP = $oldTemp; $env:TMP = $oldTmp }
