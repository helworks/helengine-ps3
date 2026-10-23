using System.Diagnostics;
namespace helengine.ps3.builder.tests;

public sealed class Ps3NativeBuildStateRootTests {
    [Theory]
    [InlineData(null)]
    [InlineData("")]
    public void UnsetStateRootPreservesDefault(string value) {
        ProcessStartInfo info = new("pwsh");
        Ps3NativeBuildExecutor.AppendStateRootArgument(info, value);
        Assert.Empty(info.ArgumentList);
    }

    [Fact]
    public void AbsoluteStateRootIsOneArgumentIncludingSpaces() {
        ProcessStartInfo info = new("pwsh");
        string root = Path.Combine(Path.GetPathRoot(Environment.CurrentDirectory)!, "sdk state");
        Ps3NativeBuildExecutor.AppendStateRootArgument(info, root);
        Assert.Equal(new[] { "-StateRoot", root }, info.ArgumentList);
    }

    [Theory]
    [InlineData("relative/state")]
    [InlineData(" ")]
    public void RelativeStateRootFailsBeforeChangingArguments(string value) {
        ProcessStartInfo info = new("pwsh");
        Assert.Throws<InvalidOperationException>(() => Ps3NativeBuildExecutor.AppendStateRootArgument(info, value));
        Assert.Empty(info.ArgumentList);
    }
}
