function Assert-Ps3AcceptanceOutputRoot {
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$Candidate,
        [Parameter(Mandatory)][string]$RepositoryRoot
    )

    if ([string]::IsNullOrWhiteSpace($Candidate) -or -not [IO.Path]::IsPathRooted($Candidate)) {
        throw 'OutputRoot must be an explicit absolute path.'
    }
    if ([string]::IsNullOrWhiteSpace($RepositoryRoot) -or -not [IO.Path]::IsPathRooted($RepositoryRoot)) {
        throw 'Repository root must be an absolute path.'
    }

    $fullRepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
    $repositoryParent = Split-Path -Parent $fullRepositoryRoot
    if ([string]::IsNullOrWhiteSpace($repositoryParent)) { throw 'Repository root must have a parent directory.' }
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $repositoryParent 'builds/helengine-ps3'))
    $fullCandidate = [IO.Path]::GetFullPath($Candidate)
    $buildRootPrefix = $buildRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if ([string]::Equals($buildRoot, $fullCandidate, [StringComparison]::OrdinalIgnoreCase) -or
        -not $fullCandidate.StartsWith($buildRootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'OutputRoot must be a strict descendant of the sibling builds/helengine-ps3 directory.'
    }
    return $fullCandidate
}

function Assert-Ps3AcceptanceToolchain {
    param([Parameter(Mandatory)][string]$RepositoryRoot)

    $fullRepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
    $lockPath = Join-Path $fullRepositoryRoot 'toolchain-lock.json'
    if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) { throw "Pinned toolchain lock is missing: $lockPath" }
    try { $lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json }
    catch { throw "Pinned toolchain lock is invalid JSON: $($_.Exception.Message)" }

    $imageId = [string]$lock.ResolvedImage.ImageId
    if ($imageId -notmatch '^sha256:[0-9a-fA-F]{64}$') {
        throw 'Pinned Docker image identity must be sha256 followed by exactly 64 hexadecimal characters.'
    }
    $compilerPath = [string]$lock.ResolvedImage.CgcompPath
    if (-not $compilerPath.StartsWith('/', [StringComparison]::Ordinal) -or $compilerPath.Contains(',')) {
        throw 'Pinned cgcomp path must be an absolute container path.'
    }

    $mountSourceRelative = [string]$lock.ShaderToolkit.MountSource
    if ([string]::IsNullOrWhiteSpace($mountSourceRelative) -or [IO.Path]::IsPathRooted($mountSourceRelative)) {
        throw 'Locked Cg mount source must be a nonempty repository-relative path.'
    }
    $mountSource = [IO.Path]::GetFullPath((Join-Path $fullRepositoryRoot $mountSourceRelative))
    $repositoryPrefix = $fullRepositoryRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $mountSource.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Locked Cg mount source escapes the repository root.'
    }
    if (-not (Test-Path -LiteralPath $mountSource -PathType Container)) { throw "Locked Cg mount source is missing: $mountSource" }

    $libraryRelative = [string]$lock.ShaderToolkit.LibraryPath
    if ([string]::IsNullOrWhiteSpace($libraryRelative) -or [IO.Path]::IsPathRooted($libraryRelative)) {
        throw 'Locked Cg library path must be relative to the mount source.'
    }
    $libraryPath = [IO.Path]::GetFullPath((Join-Path $mountSource ($libraryRelative -replace '/', [IO.Path]::DirectorySeparatorChar)))
    $mountPrefix = $mountSource.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $libraryPath.StartsWith($mountPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Locked Cg library path escapes its mount source.'
    }
    if (-not (Test-Path -LiteralPath $libraryPath -PathType Leaf)) { throw "Locked Cg shared library is missing: $libraryPath" }

    $expectedDigest = [string]$lock.ShaderToolkit.LibCgSha256
    if ($expectedDigest -notmatch '^[0-9a-fA-F]{64}$') { throw 'Pinned Cg library digest must contain exactly 64 hexadecimal characters.' }
    $actualDigest = (Get-FileHash -LiteralPath $libraryPath -Algorithm SHA256).Hash
    if (-not [string]::Equals($expectedDigest, $actualDigest, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Mounted Cg library SHA-256 mismatch: expected $expectedDigest, found $actualDigest."
    }

    return [PSCustomObject]@{
        ImageId = $imageId
        CompilerPath = $compilerPath
        CgLibrary = $libraryPath
        CgSha256 = $actualDigest.ToLowerInvariant()
    }
}

function Get-Ps3AcceptanceRunPaths {
    param(
        [Parameter(Mandatory)][string]$OutputRoot,
        [Parameter(Mandatory)][string]$RunId
    )

    if ([string]::IsNullOrWhiteSpace($RunId) -or $RunId -notmatch '^[A-Za-z0-9-]+$') {
        throw 'Acceptance run ID must contain only letters, digits, and hyphens.'
    }
    $runRoot = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) (Join-Path 'runs' $RunId)
    return [PSCustomObject]@{
        RunRoot = $runRoot
        PureResults = Join-Path $runRoot 'results/pure'
        PureReport = Join-Path $runRoot 'results/pure/pure.trx'
        PureLog = Join-Path $runRoot 'pure-tests.log'
        DockerResults = Join-Path $runRoot 'results/docker'
        DockerReport = Join-Path $runRoot 'results/docker/docker.trx'
        DockerLog = Join-Path $runRoot 'docker-tests.log'
    }
}

function Assert-Ps3AcceptanceTestReport {
    param(
        [Parameter(Mandatory)][int]$ExitCode,
        [Parameter(Mandatory)][string]$ReportPath,
        [Parameter(Mandatory)][string[]]$RequiredClasses
    )

    if ($ExitCode -ne 0) { throw "Test process failed with exit code $ExitCode; see its acceptance log." }
    if (-not (Test-Path -LiteralPath $ReportPath -PathType Leaf)) { throw "Test result report is missing: $ReportPath" }
    try { [xml]$report = Get-Content -LiteralPath $ReportPath -Raw }
    catch { throw "Test result report is invalid XML: $($_.Exception.Message)" }

    $summary = $report.SelectSingleNode("//*[local-name()='ResultSummary']")
    if ($null -eq $summary) { throw "Test result report has no summary: $ReportPath" }
    $counters = $summary.SelectSingleNode("*[local-name()='Counters']")
    if ($null -eq $counters) { throw "Test result report has no summary counters: $ReportPath" }
    $total = [int]$counters.GetAttribute('total')
    $executed = [int]$counters.GetAttribute('executed')
    $passed = [int]$counters.GetAttribute('passed')
    $failed = [int]$counters.GetAttribute('failed')
    if ($total -le 0 -or $executed -le 0) { throw "Test result report contains zero tests: $ReportPath" }
    $summaryOutcome = $summary.GetAttribute('outcome')
    if ($summaryOutcome -notin @('Completed', 'Passed') -or $failed -ne 0 -or $passed -ne $total -or $executed -ne $total) {
        throw "Test result report is not fully passed: total=$total executed=$executed passed=$passed failed=$failed."
    }
    $testResults = @($report.SelectNodes("//*[local-name()='UnitTestResult']"))
    if ($testResults.Count -ne $executed -or @($testResults | Where-Object { $_.GetAttribute('outcome') -ne 'Passed' }).Count -ne 0) {
        throw "Test result report contains missing or non-passing test results: expected=$executed actual=$($testResults.Count)."
    }

    $unitTests = @($report.SelectNodes("//*[local-name()='UnitTest']"))
    foreach ($requiredClass in $RequiredClasses) {
        $classDefinitionFound = $false
        $classHasPassedResult = $false
        foreach ($unitTest in $unitTests) {
            $testMethod = $unitTest.SelectSingleNode("./*[local-name()='TestMethod']")
            if ($null -eq $testMethod) { continue }
            $className = $testMethod.GetAttribute('className')
            if ($className -ne $requiredClass -and -not $className.EndsWith(".$requiredClass", [StringComparison]::Ordinal)) { continue }
            $classDefinitionFound = $true
            $testId = $unitTest.GetAttribute('id')
            $linkedPassedResult = @($testResults | Where-Object {
                $_.GetAttribute('testId') -eq $testId -and $_.GetAttribute('outcome') -eq 'Passed'
            })
            if ($linkedPassedResult.Count -gt 0) { $classHasPassedResult = $true; break }
        }
        if (-not $classDefinitionFound) { throw "Test result report is missing required test class '$requiredClass'." }
        if (-not $classHasPassedResult) { throw "Required test class '$requiredClass' has no executed Passed result linked to its test definition." }
    }
    $classNames = @($unitTests | ForEach-Object {
        $testMethod = $_.SelectSingleNode("./*[local-name()='TestMethod']")
        if ($null -ne $testMethod) { $testMethod.GetAttribute('className') }
    } | Select-Object -Unique)
    return [PSCustomObject]@{ Total = $total; Passed = $passed; Classes = $classNames }
}
