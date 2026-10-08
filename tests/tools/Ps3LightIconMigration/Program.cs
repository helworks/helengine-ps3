using System.Security.Cryptography;
using System.Text.Json;
using helengine;
using helengine.editor;
using Serializer = helengine.files.AssetSerializer;

/// <summary>Inspects and prepares narrowly guarded PS3 instruction icon overrides.</summary>
internal static class Program {
    /// <summary>Production wrapper preserves opaque common and other-platform payloads.</summary>
    private static readonly ComponentPlatformOverridePayloadService Wrapper = new();
    /// <summary>Exact PS3 icon reference expected from the existing authored desktop binding.</summary>
    private const string TrianglePath = "textures/instructions/controls/generated/ps3/triangle.png";
    /// <summary>Prepared files remain visible outside the source asset directories.</summary>
    private const string BuildRoot = "C:/dev/helworks/builds/helengine-ps3/2d-batch-20260924/light-icon/";

    /// <summary>Runs read-only inspection; migration commands are explicit.</summary>
    private static int Main(string[] args) {
        try {
            if (args.Length == 2 && args[0] == "verify-cooked") {
                Asset cookedAsset = global::helengine.AssetSerializer.DeserializeFromBytes(File.ReadAllBytes(args[1]));
                // Runtime expansion retains the instance root and nests the blueprint's own root beneath it.
                SceneEntityAsset consolePanel = Walk(Roots(cookedAsset)).Single(e => e.Name == "ConsoleCameraLightInstructionsPanel");
                SceneComponentAssetRecord console = Sprite(consolePanel.Children.Single(e => e.Name == "LightIcon"));
                SceneEntityAsset desktop = Roots(cookedAsset).Single(e => e.Name == "DemoSceneInstructionViewport");
                SceneComponentAssetRecord expected = Sprite(Walk(new[] { desktop }).Single(e => e.Name == "LightIcon"));
                // Both authored icons have identical common color/order and PS3 geometry. Exact runtime payload
                // equality also verifies the cooked texture reference, without interpreting packaged references as editor IDs.
                if (!console.Payload.SequenceEqual(expected.Payload))
                    throw new InvalidDataException("Cooked console LightIcon differs from the existing PS3 Triangle binding.");
                Console.WriteLine(JsonSerializer.Serialize(new { result = "PASS cooked console equals verified PS3 Triangle sprite", spritePayloadHash = Hash(console.Payload) }));
                return 0;
            }
            if (args.Length == 2 && args[0] == "verify") {
                Verify(Read(File.ReadAllBytes(args[1])));
                Console.WriteLine("PASS PS3 Triangle override and PS2 common Circle");
                return 0;
            }
            if (args.Length == 4 && args[0] == "prepare") {
                string output = Path.GetFullPath(args[3]).Replace('\\', '/');
                if (!output.StartsWith(BuildRoot, StringComparison.OrdinalIgnoreCase) || File.Exists(output)) throw new InvalidDataException("Output must be a new task-build file.");
                byte[] original = File.ReadAllBytes(args[1]);
                Asset donor = Read(File.ReadAllBytes(args[2]));
                byte[] candidate = Prepare(Read(original), Donor(donor));
                SelfTests(original, Donor(donor));
                Directory.CreateDirectory(Path.GetDirectoryName(output)!);
                using (FileStream stream = new(output, FileMode.CreateNew)) stream.Write(candidate);
                Console.WriteLine(JsonSerializer.Serialize(new { source = args[1], sourceHash = Hash(original), output, outputHash = Hash(candidate) }));
                return 0;
            }
            if (args.Length != 2 || args[0] != "inspect") throw new ArgumentException("inspect <asset>");
            Asset asset = Read(File.ReadAllBytes(args[1]));
            Console.WriteLine(JsonSerializer.Serialize(Roots(asset).Select(e => new { e.Name, e.Id, children = e.Children.Select(c => c.Name), components = e.Components.Select(c => c.ComponentTypeId) })));
            foreach (SceneEntityAsset entity in Walk(Roots(asset)).Where(e => e.Name == "LightIcon")) {
                SceneComponentAssetRecord record = Sprite(entity);
                Console.WriteLine(JsonSerializer.Serialize(new {
                    entity.Id, entity.Name, record.ComponentTypeId,
                    commonBytes = Wrapper.UnwrapBaseRecord(record).Payload.Length,
                    overrides = Wrapper.ReadOverrideStates(record).Select(o => new {
                        scope = o.Scope.ToString(), properties = o.EnumeratePropertyOverrides(),
                        references = o.EnumerateNamedAssetReferences(), members = o.EnumerateMemberValues(),
                        payloadBytes = o.Payload.Length
                    })
                }));
            }
            return 0;
        } catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }

    /// <summary>Decodes the complete production stream and rejects unsupported/trailing data.</summary>
    private static Asset Read(byte[] bytes) {
        using MemoryStream stream = new(bytes, false);
        Asset asset = Serializer.Deserialize(stream);
        if (stream.Position != stream.Length || asset is not (SceneAsset or BlueprintAsset)) throw new InvalidDataException("Unexpected asset or trailing data.");
        return asset;
    }

    /// <summary>Provides authored roots without regenerating entities.</summary>
    private static IEnumerable<SceneEntityAsset> Roots(Asset asset) => asset switch {
        SceneAsset scene => scene.RootEntities, BlueprintAsset blueprint => new[] { blueprint.RootEntity },
        _ => throw new InvalidDataException("Unsupported asset.")
    };

    /// <summary>Traverses persisted entities while retaining exact identities and order.</summary>
    private static IEnumerable<SceneEntityAsset> Walk(IEnumerable<SceneEntityAsset> roots) {
        foreach (SceneEntityAsset entity in roots) {
            yield return entity;
            foreach (SceneEntityAsset child in Walk(entity.Children ?? Array.Empty<SceneEntityAsset>())) yield return child;
        }
    }

    /// <summary>Requires precisely one sprite record on the target icon.</summary>
    private static SceneComponentAssetRecord Sprite(SceneEntityAsset entity) => entity.Components.Single(r => r.ComponentTypeId.Contains("SpriteComponent", StringComparison.Ordinal));

    /// <summary>Requires the unique direct LightIcon child of the reusable console panel.</summary>
    private static SceneComponentAssetRecord Target(Asset asset) {
        SceneEntityAsset root = Roots(asset).Single(e => e.Name == "ConsoleCameraLightInstructions");
        SceneEntityAsset panel = root.Children.Single(e => e.Name == "ConsoleCameraLightInstructionsPanel");
        return Sprite(panel.Children.Single(e => e.Name == "LightIcon"));
    }

    /// <summary>Reads the already-correct PS3 binding from the desktop panel, never guessing UVs or asset identity.</summary>
    private static EntityComponentPlatformOverrideState Donor(Asset asset) {
        SceneEntityAsset root = Roots(asset).Single(e => e.Name == "DemoSceneInstructionViewport");
        SceneEntityAsset panel = root.Children.Single(e => e.Name == "DemoSceneInstructionPanel");
        SceneComponentAssetRecord record = Sprite(panel.Children.Single(e => e.Name == "LightIcon"));
        EntityComponentPlatformOverrideState state = Wrapper.ReadOverrideStates(record).Single(IsPs3);
        ValidateOverride(state);
        return state;
    }

    /// <summary>Recognizes PS3 independently of whether the source scope includes group ancestors.</summary>
    private static bool IsPs3(EntityComponentPlatformOverrideState state) => state.Scope.TryGetStepId(SceneOverrideScopeStepKind.Platform, out string id) && id == "ps3";

    /// <summary>Checks exact intended properties, texture identity and display geometry.</summary>
    private static void ValidateOverride(EntityComponentPlatformOverrideState state) {
        if (!state.EnumeratePropertyOverrides().Order().SequenceEqual(new[] { "Size", "SourceRect" }) || state.EnumerateMemberValues().Any()) throw new InvalidDataException("Unexpected PS3 override members.");
        var reference = state.EnumerateNamedAssetReferences().Single();
        if (reference.Key != "Texture" || reference.Value.RelativePath != TrianglePath || reference.Value.AssetId != "1c88f2482c1345669d0e05654c9b2575") throw new InvalidDataException("Unexpected PS3 texture reference.");
        using var textureField = Field(state.Payload, "Texture");
        SceneAssetReference texture = SceneComponentBinaryFieldEncoding.ReadOptionalReference(textureField);
        if (texture?.RelativePath != TrianglePath || texture.AssetId != reference.Value.AssetId) throw new InvalidDataException("PS3 payload and texture metadata disagree.");
        using var size = Field(state.Payload, "Size");
        if (size.ReadInt32() != 46 || size.ReadInt32() != 46) throw new InvalidDataException("Unexpected fitted Triangle size.");
        using var uv = Field(state.Payload, "SourceRect");
        if (uv.ReadSingle() != 0.2109375f || uv.ReadSingle() != 0.2109375f || uv.ReadSingle() != 0.58203125f || uv.ReadSingle() != 0.58203125f) throw new InvalidDataException("Unexpected Triangle source rect.");
    }

    /// <summary>Opens one required field with the production tagged-field decoder.</summary>
    private static EngineBinaryReader Field(byte[] payload, string name) {
        if (!new EditorTaggedSceneComponentFieldReader(payload).TryGetFieldReader(name, out EngineBinaryReader reader)) throw new InvalidDataException($"Missing sprite field {name}.");
        return reader;
    }

    /// <summary>Requires both the added PS3 override and unchanged PS2 common texture.</summary>
    private static void Verify(Asset asset) {
        SceneComponentAssetRecord record = Target(asset);
        ValidateOverride(Wrapper.ReadOverrideStates(record).Single(IsPs3));
        using var texture = Field(Wrapper.UnwrapBaseRecord(record).Payload, "Texture");
        if (SceneComponentBinaryFieldEncoding.ReadOptionalReference(texture)?.RelativePath != "textures/instructions/controls/generated/ps2/circle.png") throw new InvalidDataException("Common PS2 Circle missing.");
    }

    /// <summary>Adds only the PS3 binding, then proves canonical restoration leaves every other authored byte unchanged.</summary>
    private static byte[] Prepare(Asset asset, EntityComponentPlatformOverrideState donor) {
        if (asset is not BlueprintAsset blueprint) throw new InvalidDataException("Only the referenced console blueprint needs migration; the scene must remain unchanged.");
        byte[] before = Serializer.SerializeToBytes(asset);
        SceneComponentAssetRecord target = Target(asset);
        byte[] originalPayload = target.Payload;
        SceneAssetReference[] originalReferences = blueprint.AssetReferences;
        SceneComponentAssetRecord common = Wrapper.UnwrapBaseRecord(target);
        var states = Wrapper.ReadOverrideStates(target);
        if (states.Any(IsPs3)) { Verify(asset); return before; }
        // Unoverridden fields in the copied payload must match the target, not the donor panel's layout.
        using (var a = Field(common.Payload, "Color")) using (var b = Field(donor.Payload, "Color"))
            if (a.ReadUInt32() != b.ReadUInt32()) throw new InvalidDataException("Donor color differs from target.");
        using (var a = Field(common.Payload, "RenderOrder2D")) using (var b = Field(donor.Payload, "RenderOrder2D"))
            if (a.ReadByte() != b.ReadByte()) throw new InvalidDataException("Donor render order differs from target.");
        EntityComponentSaveState save = new();
        foreach (var state in states) save.SetScopedPlatformOverride(state.Scope, state);
        EntityComponentPlatformOverrideState addition = new() { Scope = new EditorOverrideScope("ps3"), Payload = donor.Payload.ToArray() };
        foreach (string property in donor.EnumeratePropertyOverrides()) addition.SetPropertyOverride(property);
        foreach (var reference in donor.EnumerateNamedAssetReferences()) addition.SetAssetReference(reference.Key, reference.Value);
        save.SetScopedPlatformOverride(addition.Scope, addition);
        target.Payload = Wrapper.Wrap(common, save).Payload;
        SceneAssetReference texture = donor.EnumerateAssetReferences().Single();
        if (!blueprint.AssetReferences.Any(r => r.RelativePath == TrianglePath)) blueprint.AssetReferences = blueprint.AssetReferences.Append(texture).ToArray();
        string expectedReferences = ReferenceFingerprint(blueprint.AssetReferences);
        byte[] result = Serializer.SerializeToBytes(asset);
        Asset decoded = Read(result);
        Verify(decoded);
        // Restore only our added override using production decoding. This detects incidental reordering or data loss.
        SceneComponentAssetRecord decodedTarget = Target(decoded);
        EntityComponentSaveState restored = new();
        foreach (var state in Wrapper.ReadOverrideStates(decodedTarget).Where(s => !IsPs3(s))) restored.SetScopedPlatformOverride(state.Scope, state);
        decodedTarget.Payload = Wrapper.Wrap(Wrapper.UnwrapBaseRecord(decodedTarget), restored).Payload;
        if (!decodedTarget.Payload.SequenceEqual(originalPayload)) throw new InvalidDataException("Other platform/common payload changed.");
        if (ReferenceFingerprint(((BlueprintAsset)decoded).AssetReferences) != expectedReferences) throw new InvalidDataException("Reference table changed unexpectedly.");
        ((BlueprintAsset)decoded).AssetReferences = originalReferences;
        Guard(before, decoded);
        return result;
    }

    /// <summary>Rejects every unrelated serialized mutation after exact restoration of the allowed delta.</summary>
    private static void Guard(byte[] before, Asset restored) {
        if (!before.SequenceEqual(Serializer.SerializeToBytes(restored))) throw new InvalidDataException("Unrelated authored data changed.");
    }

    /// <summary>Exercises no-op repetition and explicit rejection of unrelated changes and ambiguous targets.</summary>
    private static void SelfTests(byte[] bytes, EntityComponentPlatformOverrideState donor) {
        byte[] once = Prepare(Read(bytes), donor);
        if (!once.SequenceEqual(Prepare(Read(once), donor))) throw new InvalidDataException("Idempotence failed.");
        Asset changed = Read(bytes);
        Roots(changed).Single().Name = "unexpected";
        bool rejected = false;
        try { Guard(Serializer.SerializeToBytes(Read(bytes)), changed); } catch (InvalidDataException) { rejected = true; }
        if (!rejected) throw new InvalidDataException("Unrelated-change guard failed.");
        BlueprintAsset duplicate = (BlueprintAsset)Read(bytes);
        var panel = duplicate.RootEntity.Children.Single(e => e.Name == "ConsoleCameraLightInstructionsPanel");
        panel.Children = panel.Children.Append(panel.Children.Single(e => e.Name == "LightIcon")).ToArray();
        rejected = false;
        try { _ = Prepare(duplicate, donor); } catch (InvalidOperationException) { rejected = true; }
        if (!rejected) throw new InvalidDataException("Duplicate-target guard failed.");
        rejected = false;
        try { _ = Prepare(changed, donor); } catch (InvalidOperationException) { rejected = true; }
        if (!rejected) throw new InvalidDataException("Missing-target guard failed.");
        Console.WriteLine("PASS migration idempotence, unrelated mutation, duplicate target, missing target");
    }

    /// <summary>Hashes original and prepared bytes for guarded installation.</summary>
    private static string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes));

    /// <summary>Compares every immutable reference field and multiplicity while allowing the serializer's canonical table ordering.</summary>
    private static string ReferenceFingerprint(IEnumerable<SceneAssetReference> references) =>
        string.Join("\n", references.Select(r => JsonSerializer.Serialize(r)).Order(StringComparer.Ordinal));
}
