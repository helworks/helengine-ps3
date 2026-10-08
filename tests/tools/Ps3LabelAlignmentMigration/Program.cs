using System.Security.Cryptography;
using System.Text.Json;
using helengine;
using FileAssetSerializer = helengine.files.AssetSerializer;

/// <summary>Inspects and prepares guarded label-position changes in serialized scenes and blueprints.</summary>
internal static class Program {
    /// <summary>Restricts prepared copies to the approved task build root.</summary>
    private const string BuildRoot = "C:/dev/helworks/builds/helengine-ps3/2d-batch-20260924/label-alignment";
    /// <summary>Persisted nested group scope used by Micro SD consumers.</summary>
    private static readonly SceneOverrideScopeStepAsset[] MicroSdScope = {
        new() { Kind = SceneOverrideScopeStepKind.Group, Id = "sd" },
        new() { Kind = SceneOverrideScopeStepKind.Group, Id = "msd" }
    };

    /// <summary>Runs self-tests, reads one asset, or writes a prepared copy without touching the source.</summary>
    /// <param name="args">Command and optional absolute asset paths.</param>
    /// <returns>Zero on success, one when an input or guard fails.</returns>
    private static int Main(string[] args) {
        try {
            if (args.Length == 1 && args[0] == "self-test") return RunSelfTests();
            if (args.Length == 2 && args[0] == "inspect") { WriteJson(Inspect(ReadInput(args[1]))); return 0; }
            if (args.Length == 3 && args[0] == "prepare") {
                string sourcePath = RequireFile(args[1]);
                string outputPath = RequireNewBuildPath(args[2], sourcePath);
                byte[] sourceBytes = File.ReadAllBytes(sourcePath);
                byte[] proposed = PrepareAsset(Deserialize(sourceBytes));
                Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
                using (FileStream output = new(outputPath, FileMode.CreateNew, FileAccess.Write, FileShare.None)) output.Write(proposed);
                WriteJson(new { inputPath = sourcePath, inputSha256 = Hash(sourceBytes), outputPath, outputSha256 = Hash(proposed), labels = DescribeLabels(Deserialize(proposed)) });
                return 0;
            }
            throw new ArgumentException("Usage: self-test | inspect <absolute-file> | prepare <absolute-source> <absolute-new-file>");
        } catch (Exception exception) { Console.Error.WriteLine($"FAIL {exception.GetType().Name}: {exception.Message}"); return 1; }
    }

    /// <summary>Loads an absolute existing asset file without changing its bytes.</summary>
    /// <param name="path">Absolute path to a scene or blueprint.</param><returns>Path and original file bytes.</returns>
    private static (string Path, byte[] Bytes) ReadInput(string path) { string full = RequireFile(path); return (full, File.ReadAllBytes(full)); }

    /// <summary>Parses a production scene or blueprint and requires full stream consumption.</summary>
    /// <param name="bytes">Serialized source bytes.</param><returns>Decoded supported asset.</returns>
    private static Asset Deserialize(byte[] bytes) {
        using MemoryStream stream = new(bytes, false);
        Asset asset = FileAssetSerializer.Deserialize(stream);
        if (stream.Position != stream.Length) throw new InvalidDataException("Asset contains trailing or unread bytes.");
        if (asset is not SceneAsset && asset is not BlueprintAsset) throw new InvalidDataException($"Unsupported asset type {asset.GetType().FullName}.");
        return asset;
    }

    /// <summary>Reports target labels and their immediate parent, common position, level order, and transform scopes.</summary>
    /// <param name="input">Absolute source path and original bytes.</param><returns>JSON-ready inspection data.</returns>
    private static object Inspect((string Path, byte[] Bytes) input) {
        Asset asset = Deserialize(input.Bytes);
        return new { inputPath = input.Path, inputSha256 = Hash(input.Bytes), type = asset.GetType().FullName, labels = DescribeLabels(asset) };
    }

    /// <summary>Builds the concise label and scope inventory included by inspect and prepare.</summary>
    /// <param name="asset">Decoded source or candidate asset.</param><returns>Label records for direct panel children.</returns>
    private static object[] DescribeLabels(Asset asset) => FindTargets(asset).Select(target => (object)new {
            entity = target.Entity.Name, id = target.Entity.Id, parent = target.Panel.Name,
            localPosition = Position(target.Entity.LocalPosition), levelOrder = target.Entity.OverrideLevelOrder,
            transformScopes = (target.Entity.PlatformTransformOverrides ?? Array.Empty<SceneEntityPlatformTransformOverrideAsset>()).Select(value => new {
                scope = Scope(value.Scope), value.HasLocalPositionOverride, position = Position(value.LocalPosition),
                value.HasLocalScaleOverride, value.LocalScale, value.HasLocalOrientationOverride, value.LocalOrientation
            })
        }).ToArray();

    /// <summary>Creates updated bytes and proves all changes are limited to target common positions and transform arrays.</summary>
    /// <param name="asset">Decoded production asset to update in memory.</param><returns>Validated serialized candidate bytes.</returns>
    private static byte[] PrepareAsset(Asset asset) {
        byte[] original = FileAssetSerializer.SerializeToBytes(asset);
        Asset snapshot = Deserialize(original);
        List<Target> targets = FindTargets(asset);
        Dictionary<string, Target> originals = FindTargets(snapshot).ToDictionary(Key);
        foreach (Target target in targets) {
            ValidateLevelOrder(target.Entity);
            int oldY = OldY(target.Entity.Name!);
            int newY = oldY - 14;
            SceneEntityPlatformTransformOverrideAsset? scoped = FindMicroSdOverride(target.Entity, false);
            if (target.Entity.LocalPosition.Y == newY) {
                if (scoped?.HasLocalPositionOverride != true || !SamePosition(scoped.LocalPosition, target.Entity.LocalPosition, oldY))
                    throw new InvalidDataException($"Already-adjusted {target.Entity.Name} lacks its expected Micro SD position override.");
                continue;
            }
            if (target.Entity.LocalPosition.Y != oldY) throw new InvalidDataException($"Unexpected common Y={target.Entity.LocalPosition.Y} on {target.Entity.Name}; expected {oldY} or {newY}.");
            if (scoped?.HasLocalPositionOverride == true) throw new InvalidDataException($"Unexpected existing Micro SD position override on {target.Entity.Name}.");
            scoped ??= AddMicroSdOverride(target.Entity);
            scoped.HasLocalPositionOverride = true;
            scoped.LocalPosition = target.Entity.LocalPosition;
            float3 common = target.Entity.LocalPosition;
            target.Entity.LocalPosition = new float3(common.X, newY, common.Z);
        }

        byte[] candidate = FileAssetSerializer.SerializeToBytes(asset);
        Asset decodedCandidate = Deserialize(candidate);
        Dictionary<string, Target> candidateTargets = FindTargets(decodedCandidate).ToDictionary(Key);
        foreach (Target originalTarget in targets) {
            Target changed = candidateTargets[Key(originalTarget)];
            int oldY = OldY(changed.Entity.Name!);
            int newY = oldY - 14;
            SceneEntityPlatformTransformOverrideAsset? scoped = FindMicroSdOverride(changed.Entity, false);
            if (changed.Entity.LocalPosition.Y != newY || scoped?.HasLocalPositionOverride != true || !SamePosition(scoped.LocalPosition, changed.Entity.LocalPosition, oldY))
                throw new InvalidDataException($"Prepared {changed.Entity.Name} failed semantic verification.");
            if (!TransformArraysEqualExceptPosition(originalTarget.Entity.PlatformTransformOverrides, changed.Entity.PlatformTransformOverrides))
                throw new InvalidDataException($"Prepared {changed.Entity.Name} changed non-position transform override state.");
            Target before = originals[Key(originalTarget)];
            changed.Entity.LocalPosition = before.Entity.LocalPosition;
            changed.Entity.PlatformTransformOverrides = before.Entity.PlatformTransformOverrides;
        }
        if (!FileAssetSerializer.SerializeToBytes(decodedCandidate).SequenceEqual(original))
            throw new InvalidDataException("Canonical restoration detected a change outside target positions and transform overrides.");
        return candidate;
    }

    /// <summary>Finds the unique CameraText and LightIconText pair directly under each supported panel.</summary>
    /// <param name="asset">Decoded scene or blueprint.</param><returns>Paired label entities and their panel.</returns>
    private static List<Target> FindTargets(Asset asset) {
        IEnumerable<SceneEntityAsset> roots = asset switch {
            SceneAsset scene => scene.RootEntities ?? Array.Empty<SceneEntityAsset>(),
            BlueprintAsset blueprint => new[] { blueprint.RootEntity },
            _ => throw new InvalidDataException("Only scene and blueprint assets are supported.")
        };
        List<Target> targets = new();
        foreach (SceneEntityAsset panel in Walk(roots)) {
            if (panel.Name is not ("DemoSceneInstructionPanel" or "ConsoleCameraLightInstructionsPanel")) continue;
            SceneEntityAsset[] children = panel.Children ?? Array.Empty<SceneEntityAsset>();
            SceneEntityAsset[] cameras = children.Where(child => child?.Name == "CameraText").ToArray();
            SceneEntityAsset[] lights = children.Where(child => child?.Name == "LightIconText").ToArray();
            if (cameras.Length + lights.Length == 0) continue;
            if (cameras.Length != 1 || lights.Length != 1) throw new InvalidDataException($"Panel {panel.Name} must contain exactly one CameraText and one LightIconText.");
            targets.Add(new Target(panel, cameras[0])); targets.Add(new Target(panel, lights[0]));
        }
        if (targets.Count == 0 || targets.Count % 2 != 0) throw new InvalidDataException("No paired instruction labels were found.");
        foreach (Target target in targets) ValidateLevelOrder(target.Entity);
        return targets;
    }

    /// <summary>Requires the established group, platform, and build-config resolution order.</summary>
    /// <param name="entity">Instruction label entity.</param>
    private static void ValidateLevelOrder(SceneEntityAsset entity) {
        SceneOverrideScopeStepKind[] expected = { SceneOverrideScopeStepKind.Group, SceneOverrideScopeStepKind.Platform, SceneOverrideScopeStepKind.BuildConfig };
        if (entity.OverrideLevelOrder == null || !entity.OverrideLevelOrder.SequenceEqual(expected))
            throw new InvalidDataException($"Unexpected override level order on {entity.Name}.");
    }

    /// <summary>Returns one existing SD/Micro-SD override and rejects duplicate matching entries.</summary>
    /// <param name="entity">Label whose transform overrides are searched.</param><param name="addIfMissing">Whether to add a new scope entry.</param>
    /// <returns>Matching entry, or null when absent and addition was not requested.</returns>
    private static SceneEntityPlatformTransformOverrideAsset? FindMicroSdOverride(SceneEntityAsset entity, bool addIfMissing) {
        SceneEntityPlatformTransformOverrideAsset[] matches = (entity.PlatformTransformOverrides ?? Array.Empty<SceneEntityPlatformTransformOverrideAsset>())
            .Where(value => ScopeMatches(value.Scope)).ToArray();
        if (matches.Length > 1) throw new InvalidDataException($"Duplicate SD/Micro-SD transform scopes on {entity.Name}.");
        if (matches.Length == 1) return matches[0];
        return addIfMissing ? AddMicroSdOverride(entity) : null;
    }

    /// <summary>Adds an SD/Micro-SD transform record while retaining every existing scoped record.</summary>
    /// <param name="entity">Label receiving the new scope.</param><returns>The newly appended transform override.</returns>
    private static SceneEntityPlatformTransformOverrideAsset AddMicroSdOverride(SceneEntityAsset entity) {
        SceneEntityPlatformTransformOverrideAsset value = new() { Scope = MicroSdScope.Select(step => new SceneOverrideScopeStepAsset { Kind = step.Kind, Id = step.Id }).ToArray() };
        entity.PlatformTransformOverrides = (entity.PlatformTransformOverrides ?? Array.Empty<SceneEntityPlatformTransformOverrideAsset>()).Append(value).ToArray();
        return value;
    }

    /// <summary>Runs five synthetic migration checks using production scene serialization.</summary>
    /// <returns>Zero if all checks pass.</returns>
    private static int RunSelfTests() {
        int failures = 0;
        failures += Check("old positions migrate with Micro SD preservation", TestOldToNew);
        failures += Check("already-correct asset is a no-op", TestAlreadyCorrect);
        failures += Check("unexpected Y is rejected", TestUnexpectedY);
        failures += Check("existing scale and orientation survive", TestOverrideFields);
        failures += Check("unrelated entity stays unchanged", TestUnrelatedEntity);
        Console.WriteLine(failures == 0 ? "PASS all 5 self-tests" : $"FAIL {failures} of 5 self-tests");
        return failures == 0 ? 0 : 1;
    }

    /// <summary>Runs one synthetic check and prints a bounded result.</summary>
    /// <param name="name">Short check label.</param><param name="test">Assertion callback.</param><returns>One on failure.</returns>
    private static int Check(string name, Action test) { try { test(); Console.WriteLine($"PASS {name}"); return 0; } catch (Exception e) { Console.WriteLine($"FAIL {name}: {e.GetType().Name}: {e.Message}"); return 1; } }

    /// <summary>Verifies both panels migrate their common positions and preserve old Micro SD coordinates.</summary>
    private static void TestOldToNew() {
        Asset source = SyntheticScene(); byte[] result = PrepareAsset(source); List<Target> targets = FindTargets(Deserialize(result));
        if (targets.Count != 4) throw new InvalidOperationException("Both desktop and console pairs were not migrated.");
        foreach (Target target in targets) {
            int oldY = OldY(target.Entity.Name!);
            AssertPosition(target.Entity.LocalPosition, oldY - 14);
            SceneEntityPlatformTransformOverrideAsset scoped = FindMicroSdOverride(target.Entity, false)!;
            if (!SamePosition(scoped.LocalPosition, target.Entity.LocalPosition, oldY)) throw new InvalidOperationException("Micro SD position was not preserved.");
        }
    }

    /// <summary>Verifies corrected common values and matching preserved position overrides do not change.</summary>
    private static void TestAlreadyCorrect() {
        byte[] original = FileAssetSerializer.SerializeToBytes(SyntheticScene(12f, 80f, true));
        if (!PrepareAsset(Deserialize(original)).SequenceEqual(original)) throw new InvalidOperationException("Already-correct asset was not a no-op.");
    }

    /// <summary>Verifies an unexpected common Y fails visibly.</summary>
    private static void TestUnexpectedY() {
        PrepareAsset(SyntheticScene());
        Asset invalid = SyntheticScene(27f, 94f);
        FindTargets(invalid);
        try {
            PrepareAsset(invalid);
        } catch (InvalidDataException exception) when (exception.Message.Contains("Unexpected common Y=27", StringComparison.Ordinal)) {
            return;
        }
        throw new InvalidOperationException("Unexpected Y was accepted.");
    }

    /// <summary>Verifies adding the position value leaves an existing scope's scale and orientation intact.</summary>
    private static void TestOverrideFields() {
        SceneEntityAsset camera = FindTargets(SyntheticScene()).First(t => t.Entity.Name == "CameraText").Entity;
        SceneEntityPlatformTransformOverrideAsset before = camera.PlatformTransformOverrides![0];
        float3 scale = before.LocalScale; float4 orientation = before.LocalOrientation;
        byte[] result = PrepareAsset(SyntheticScene());
        SceneEntityPlatformTransformOverrideAsset after = FindMicroSdOverride(FindTargets(Deserialize(result)).First(t => t.Entity.Name == "CameraText").Entity, false)!;
        if (!after.HasLocalScaleOverride || !after.HasLocalOrientationOverride || !after.LocalScale.Equals(scale) || !after.LocalOrientation.Equals(orientation))
            throw new InvalidOperationException("Scale or orientation override changed.");
    }

    /// <summary>Verifies an unrelated serialized root entity retains its identity and position.</summary>
    private static void TestUnrelatedEntity() {
        SceneAsset source = SyntheticScene(); byte[] result = PrepareAsset(source); SceneAsset output = (SceneAsset)Deserialize(result);
        SceneEntityAsset before = source.RootEntities.Single(e => e.Name == "Unrelated"); SceneEntityAsset after = output.RootEntities.Single(e => e.Name == "Unrelated");
        if (before.Id != after.Id || !before.LocalPosition.Equals(after.LocalPosition)) throw new InvalidOperationException("Unrelated root entity changed.");
    }

    /// <summary>Creates paired synthetic desktop and console panels with representative payload and scoped transform data.</summary>
    /// <param name="cameraY">Common camera Y.</param><param name="lightY">Common light Y.</param><param name="corrected">Whether to author the expected Micro SD position.</param>
    /// <returns>Scene fixture for migration checks.</returns>
    private static SceneAsset SyntheticScene(float cameraY = 26f, float lightY = 94f, bool corrected = false) {
        SceneEntityAsset Panel(string name, uint id) {
            SceneEntityAsset Label(string label, uint labelId, float y) {
                float microSdY = corrected ? y + 14f : y;
                SceneEntityAsset entity = new() { Id = labelId, Name = label, LocalPosition = new float3(130f, y, 0.1f),
                    HasOverrideLevelOrder = true,
                    OverrideLevelOrder = new[] { SceneOverrideScopeStepKind.Group, SceneOverrideScopeStepKind.Platform, SceneOverrideScopeStepKind.BuildConfig },
                    Components = new[] { new SceneComponentAssetRecord { ComponentKey = "text", ComponentTypeId = "TextComponent", ComponentIndex = 0, Payload = new byte[] { 1, 2, 3 } } } };
                if (label == "CameraText" || corrected) entity.PlatformTransformOverrides = new[] { new SceneEntityPlatformTransformOverrideAsset {
                    Scope = MicroSdScope.Select(s => new SceneOverrideScopeStepAsset { Kind = s.Kind, Id = s.Id }).ToArray(),
                    HasLocalPositionOverride = corrected, LocalPosition = new float3(130f, microSdY, 0.1f), HasLocalScaleOverride = label == "CameraText",
                    LocalScale = new float3(2f, 3f, 1f), HasLocalOrientationOverride = label == "CameraText", LocalOrientation = new float4(0f, 0f, 0.25f, 0.9f)
                } };
                return entity;
            }
            return new SceneEntityAsset { Id = id, Name = name, Children = new[] { Label("CameraText", id + 1, cameraY), Label("LightIconText", id + 2, lightY) } };
        }
        return new SceneAsset { Id = "synthetic", RootEntities = new[] { Panel("DemoSceneInstructionPanel", 10), Panel("ConsoleCameraLightInstructionsPanel", 20),
            new SceneEntityAsset { Id = 30, Name = "Unrelated", LocalPosition = new float3(7f, 8f, 9f) } } };
    }

    /// <summary>Enumerates serialized entity trees recursively.</summary>
    /// <param name="roots">Root entities.</param><returns>Every non-null entity.</returns>
    private static IEnumerable<SceneEntityAsset> Walk(IEnumerable<SceneEntityAsset> roots) {
        foreach (SceneEntityAsset entity in roots ?? Array.Empty<SceneEntityAsset>()) { if (entity == null) continue; yield return entity; foreach (SceneEntityAsset child in Walk(entity.Children)) yield return child; }
    }

    /// <summary>Checks that a transformed label retained expected X/Z and has the requested Y.</summary>
    /// <param name="position">Position to validate.</param><param name="y">Expected Y.</param>
    private static void AssertPosition(float3 position, float y) { if (position.X != 130f || position.Y != y || position.Z != 0.1f) throw new InvalidOperationException("Position mismatch."); }

    /// <summary>Compares transform scopes while allowing only the matching Micro SD position fields to differ.</summary>
    /// <param name="expected">In-memory mutated override array before serialization.</param><param name="actual">Override array after candidate deserialization.</param>
    /// <returns>True when every non-position field and every other position field is identical.</returns>
    private static bool TransformArraysEqualExceptPosition(SceneEntityPlatformTransformOverrideAsset[]? expected, SceneEntityPlatformTransformOverrideAsset[]? actual) {
        expected ??= Array.Empty<SceneEntityPlatformTransformOverrideAsset>(); actual ??= Array.Empty<SceneEntityPlatformTransformOverrideAsset>();
        if (expected.Length != actual.Length) return false;
        for (int i = 0; i < expected.Length; i++) {
            SceneEntityPlatformTransformOverrideAsset a = expected[i], b = actual[i]; bool positionMayDiffer = ScopeMatches(a.Scope);
            if (!Scope(a.Scope).SequenceEqual(Scope(b.Scope)) || (!positionMayDiffer && (a.HasLocalPositionOverride != b.HasLocalPositionOverride || !a.LocalPosition.Equals(b.LocalPosition))) ||
                a.HasLocalScaleOverride != b.HasLocalScaleOverride || !a.LocalScale.Equals(b.LocalScale) ||
                a.HasLocalOrientationOverride != b.HasLocalOrientationOverride || !a.LocalOrientation.Equals(b.LocalOrientation)) return false;
        }
        return true;
    }

    /// <summary>Checks Micro SD override coordinates against common X/Z and prior common Y.</summary>
    private static bool SamePosition(float3 scoped, float3 common, float oldY) => scoped.X == common.X && scoped.Y == oldY && scoped.Z == common.Z;
    private static int OldY(string name) => name switch { "CameraText" => 26, "LightIconText" => 94, _ => throw new InvalidDataException($"Unexpected instruction label '{name}'.") };
    private static string Key(Target target) => $"{target.Panel.Name}:{target.Entity.Id}:{target.Entity.Name}";
    private static bool ScopeMatches(SceneOverrideScopeStepAsset[]? scope) => scope != null && scope.Length == 2 && scope[0].Kind == SceneOverrideScopeStepKind.Group && scope[0].Id == "sd" && scope[1].Kind == SceneOverrideScopeStepKind.Group && scope[1].Id == "msd";
    private static object Position(float3 position) => new { x = position.X, y = position.Y, z = position.Z };
    private static string[] Scope(SceneOverrideScopeStepAsset[]? scope) => (scope ?? Array.Empty<SceneOverrideScopeStepAsset>()).Select(step => $"{step.Kind}:{step.Id}").ToArray();
    private static string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
    private static void WriteJson(object value) => Console.WriteLine(JsonSerializer.Serialize(value));
    private static string RequireFile(string path) { if (!Path.IsPathFullyQualified(path)) throw new ArgumentException("Asset path must be absolute."); string full = Path.GetFullPath(path); if (!File.Exists(full)) throw new FileNotFoundException("Asset file not found.", full); return full; }
    private static string RequireNewBuildPath(string path, string source) {
        if (!Path.IsPathFullyQualified(path)) throw new ArgumentException("Output path must be absolute."); string full = Path.GetFullPath(path);
        string root = Path.GetFullPath(BuildRoot).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        if (!full.StartsWith(root, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Prepared output must be under the approved task build root.");
        if (string.Equals(full, source, StringComparison.OrdinalIgnoreCase) || File.Exists(full)) throw new IOException("Prepared output must be a new file and cannot overwrite an input.");
        return full;
    }
    private sealed record Target(SceneEntityAsset Panel, SceneEntityAsset Entity);
}
