namespace helengine.ps3.builder;

/// <summary>Provides the small discovery entry point used when inspecting a builder assembly.</summary>
public static class Program {
    /// <summary>Prints the PlayStation 3 builder identity for discovery diagnostics.</summary>
    /// <param name="args">Optional command-line arguments.</param>
    /// <returns>Zero in all cases.</returns>
    public static int Main(string[] args) {
        if (args != null && args.Length == 1 && string.Equals(args[0], "--describe", StringComparison.OrdinalIgnoreCase)) {
            Ps3PlatformAssetBuilder builder = new();
            Console.WriteLine(builder.Descriptor.BuilderId);
            Console.WriteLine(builder.Descriptor.TargetPlatformId);
            Console.WriteLine(builder.Definition.DisplayName);
            Console.WriteLine(builder.Definition.CodegenProfiles[0].Endianness);
            return 0;
        }

        Console.WriteLine("helengine.ps3.builder --describe");
        return 0;
    }
}
