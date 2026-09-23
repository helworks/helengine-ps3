using helengine;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Protects persisted shared target values and verifies the PS3 API define reaches shaderc.</summary>
public sealed class Ps3ShaderTargetCompatibilityTests {
    /// <summary>Confirms the appended PS3 target does not change existing serialized target values.</summary>
    [Fact]
    public void PreservesExistingTargetValuesAndAppendsPs3() {
        Assert.Equal(0, (int)ShaderCompileTarget.DirectX9);
        Assert.Equal(1, (int)ShaderCompileTarget.DirectX11);
        Assert.Equal(2, (int)ShaderCompileTarget.DirectX12);
        Assert.Equal(3, (int)ShaderCompileTarget.Vulkan);
        Assert.Equal(4, (int)ShaderCompileTarget.Metal);
        Assert.Equal(5, (int)ShaderCompileTarget.PsVita);
        Assert.Equal(6, (int)ShaderCompileTarget.WiiU);
        Assert.Equal(7, (int)ShaderCompileTarget.Ps3);
    }

    /// <summary>Confirms PS3 resolves to its persisted name and platform define.</summary>
    [Fact]
    public void ExposesPs3DescriptorNameAndDefine() {
        ShaderTargetDescriptor descriptor = ShaderTargetDescriptors.Get(ShaderCompileTarget.Ps3);

        Assert.Equal("ps3", descriptor.Name);
        Assert.Equal("HEL_API_PS3", descriptor.DefineName);
    }

    /// <summary>Confirms a target API define selects the matching shaderc preprocessor branch.</summary>
    [Fact]
    public void ForwardsPs3TargetDefineToShadercPreprocessor() {
        ShaderTargetDescriptor descriptor = ShaderTargetDescriptors.Get(ShaderCompileTarget.Ps3);
        Ps3ShaderIncludeResolverStub resolver = new() {
            Files = new Dictionary<string, ShaderIncludeResult> {
                ["ps3.hlsl"] = new("ps3.hlsl", "static const float4 PlatformColor = float4(0, 1, 0, 1);"),
                ["other.hlsl"] = new("other.hlsl", "static const float4 PlatformColor = float4(1, 0, 0, 1);")
            }
        };
        string source = $"#if {descriptor.DefineName}\n#include \"ps3.hlsl\"\n#else\n#include \"other.hlsl\"\n#endif\nfloat4 PS() : SV_Target {{ return PlatformColor; }}";
        ShaderCompileRequest request = new(new ShaderSourceInfo("target-define.hlsl", source), "TargetDefine", "PS", ShaderStage.Pixel,
            ShaderCompileTarget.Ps3, new ShaderModel(3, 0), "default", [new ShaderDefine(descriptor.DefineName, "1")],
            new ShaderCompileOptions(ShaderBindingPolicies.Default, false, true, false));

        string output = new Ps3ShaderSourceCompiler().Compile(request, resolver);

        Assert.Contains("void main()", output);
        Assert.Contains(resolver.Requests, include => include.IncludePath == "ps3.hlsl");
        Assert.DoesNotContain(resolver.Requests, include => include.IncludePath == "other.hlsl");
    }
}
