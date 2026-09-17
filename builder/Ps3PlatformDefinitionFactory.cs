using helengine.baseplatform.Definitions;
using helengine.baseplatform.Profiles;

namespace helengine.ps3.builder;

/// <summary>Creates the editor-facing PlayStation 3 platform contract for the generated-Core boot gate.</summary>
public static class Ps3PlatformDefinitionFactory {
    /// <summary>
    /// Features the v0.2 boot gate cannot serve yet. The list is deliberately shorter than the
    /// Dreamcast one: PlayStation 3 has real storage and a programmable GPU, so only what the
    /// gate genuinely cannot serve is disabled, and each entry is removed as its milestone lands.
    /// </summary>
    const string ForcedDisabledFeatures = "debug_overlay;shaders;physics3d.diagnostics";

    /// <summary>
    /// Pointer size in bytes, measured inside the SDK image by scripts/probe-core-abi.ps1 and
    /// recorded in docs/CoreAbiProbe.md. PlayStation 3 GameOS code is 64-bit, unlike the
    /// 32-bit consoles this project also targets, so this must not be copied from them.
    /// </summary>
    const string PointerSizeBytes = "8";

    /// <summary>Identifier of the single graphics profile the boot gate publishes.</summary>
    public const string StandardGraphicsProfileId = "ps3-standard";

    /// <summary>Creates the complete PlayStation 3 build, graphics, codegen and runtime definition.</summary>
    /// <returns>Validated platform definition.</returns>
    public static PlatformDefinition Create() {
        PlatformSettingDefinition[] buildSettings = [
            new("game-name", "Game Name", PlatformSettingKind.Text, "helengine", true, []),
            new("enable-native-runtime-diagnostics", "Enable Native Runtime Diagnostics", PlatformSettingKind.Boolean, "true", true, [])
        ];

        PlatformCodegenProfileDefinition codegen = new(
            "default",
            "Default",
            "PlayStation 3 C# to C++ generated runtime profile",
            PlatformCodegenLanguage.Cpp,
            PlatformSerializationEndianness.BigEndian,
            [
                new(PlatformCodegenSettingIds.ForcedDisabledFeatures, "Forced Disabled Features", PlatformSettingKind.Text, ForcedDisabledFeatures, true, []),
                new("generated-math-convention", "Generated Math Convention", PlatformSettingKind.Text, "native-column-vector", true, []),
                new("pointer-size-bytes", "Pointer Size (Bytes)", PlatformSettingKind.Text, PointerSizeBytes, true, []),
                new("codegen-use-exceptions", "Use Exceptions", PlatformSettingKind.Boolean, "true", true, []),
                new("codegen-use-rtti", "Use RTTI", PlatformSettingKind.Boolean, "true", true, []),
                new("native-file-stream-descriptor-apis", "Native File Stream Descriptor APIs", PlatformSettingKind.Boolean, "false", true, []),
                new("type-remaps", "Type Remaps", PlatformSettingKind.Text, "System.Numerics.Vector2=helengine.float2;System.Numerics.Vector3=helengine.float3;System.Numerics.Vector4=helengine.float4;System.Numerics.Quaternion=helengine.float4", true, []),
                new("native-file-system-header", "Native File System Header", PlatformSettingKind.Text, "\"platform/ps3/Ps3ContentStreamSource.hpp\"", true, []),
                new("native-file-system-type", "Native File System Type", PlatformSettingKind.Text, "helengine::ps3::Ps3ContentStreamSource", true, [])
            ]);

        PlatformBuildProfileDefinition[] profiles = [
            new("debug", "Debug", "PlayStation 3 development build", StandardGraphicsProfileId, "default", buildSettings),
            new("release", "Release", "PlayStation 3 release build", StandardGraphicsProfileId, "default", buildSettings)
        ];

        return new PlatformDefinition(
            "ps3",
            "PlayStation 3",
            profiles,
            [new PlatformGraphicsProfileDefinition(StandardGraphicsProfileId, "PlayStation 3 Standard", "RSX opaque renderer", [])],
            [new PlatformAssetRequirementDefinition("scene", "Scene", true, ["helen"])],
            [],
            [
                new PlatformComponentSupportRule("helengine.meshcomponent", PlatformComponentSupportKind.Unsupported, "The v0.2 boot gate renders no authored geometry.", "Authored meshes arrive with the scene milestone."),
                new PlatformComponentSupportRule("helengine.cameracomponent", PlatformComponentSupportKind.Unsupported, "The v0.2 boot gate renders no authored camera.", "Authored cameras arrive with the scene milestone.")
            ],
            [codegen],
            [new PlatformStorageProfileDefinition("loose-files", "Loose Files", PlatformStorageProfileKind.LooseFiles, "ps3-loose-files", false)],
            [new PlatformMediaProfileDefinition("ps3-self", "PlayStation 3 SELF", PlatformMediaLayoutKind.InstallTree, false, true)],
            new RuntimeGenerationContract(RuntimeMaterialResolutionMode.CookedPlatformOwned, true, PackagedPathPolicy.ContentRelativeOnly));
    }
}
