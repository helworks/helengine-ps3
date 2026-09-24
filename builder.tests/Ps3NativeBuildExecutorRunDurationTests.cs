using System.Diagnostics;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies optional run-duration argument validation and forwarding.</summary>
public sealed class Ps3NativeBuildExecutorRunDurationTests {
    /// <summary>Leaves existing arguments intact when the option is unset or empty.</summary>
    [Theory]
    [InlineData(null)]
    [InlineData("")]
    public void AppendRunDurationArgument_UnsetOrEmptyValue_LeavesArgumentsUnchanged(string configuredValue) {
        ProcessStartInfo startInfo = CreateStartInfo();

        Ps3NativeBuildExecutor.AppendRunDurationArgument(startInfo, configuredValue);

        Assert.Equal(new[] { "-Sentinel", "existing" }, startInfo.ArgumentList);
    }

    /// <summary>Forwards valid durations as separate canonical argument tokens.</summary>
    /// <param name="configuredValue">Unsigned decimal input to validate.</param>
    /// <param name="expectedSeconds">Canonical seconds token to append.</param>
    [Theory]
    [InlineData("0", "0")]
    [InlineData("1", "1")]
    [InlineData("3600", "3600")]
    [InlineData("00015", "15")]
    public void AppendRunDurationArgument_ValidValue_AppendsTwoTokens(string configuredValue, string expectedSeconds) {
        ProcessStartInfo startInfo = CreateStartInfo();

        Ps3NativeBuildExecutor.AppendRunDurationArgument(startInfo, configuredValue);

        Assert.Equal(new[] { "-Sentinel", "existing", "-RunSeconds", expectedSeconds }, startInfo.ArgumentList);
    }

    /// <summary>Rejects malformed values without changing any existing arguments.</summary>
    /// <param name="configuredValue">Invalid duration value.</param>
    [Theory]
    [InlineData(" ")]
    [InlineData(" 1")]
    [InlineData("1 ")]
    [InlineData("-1")]
    [InlineData("+1")]
    [InlineData("3601")]
    [InlineData("4294967296")]
    [InlineData("1.5")]
    [InlineData("1e2")]
    [InlineData("seconds")]
    [InlineData("15; Write-Host injected")]
    public void AppendRunDurationArgument_InvalidValue_ThrowsBeforeMutatingArguments(string configuredValue) {
        ProcessStartInfo startInfo = CreateStartInfo();

        Assert.Throws<InvalidOperationException>(() => Ps3NativeBuildExecutor.AppendRunDurationArgument(startInfo, configuredValue));

        Assert.Equal(new[] { "-Sentinel", "existing" }, startInfo.ArgumentList);
    }

    /// <summary>Rejects a missing process start description.</summary>
    [Fact]
    public void AppendRunDurationArgument_NullStartInfo_ThrowsArgumentNullException() {
        Assert.Throws<ArgumentNullException>(() => Ps3NativeBuildExecutor.AppendRunDurationArgument(null, "15"));
    }

    /// <summary>Creates a process description with sentinel arguments for mutation checks.</summary>
    /// <returns>A process description containing two existing argument tokens.</returns>
    private static ProcessStartInfo CreateStartInfo() {
        ProcessStartInfo startInfo = new("powershell") { UseShellExecute = false };
        startInfo.ArgumentList.Add("-Sentinel");
        startInfo.ArgumentList.Add("existing");
        return startInfo;
    }
}
