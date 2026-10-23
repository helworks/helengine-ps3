using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace helengine.ps3.builder;

/// <summary>Verifies raw OCI proof before resolving the locked SDK to a Docker-store-local image identifier.</summary>
public sealed class Ps3SdkImageIdentity {
    /// <summary>Names the external directory containing the original manifest.json and config.json bytes.</summary>
    public const string IdentityRootVariable = "HELENGINE_PS3_SDK_IDENTITY_ROOT";

    /// <summary>Gets the unchanged SDK identity recorded in the tracked toolchain lock.</summary>
    public string CanonicalImageId { get; }

    /// <summary>Gets the verified immutable identifier accepted by the local Docker image store.</summary>
    public string LocalImageId { get; }

    /// <summary>Gets the SHA-256 of the original manifest bytes, without the algorithm prefix.</summary>
    public string ManifestSha256 { get; }

    /// <summary>Gets the SHA-256 of the original config bytes, without the algorithm prefix.</summary>
    public string ConfigSha256 { get; }

    /// <summary>Stores the identities only after the proof and local image have both been verified.</summary>
    Ps3SdkImageIdentity(string canonicalImageId, string localImageId, string manifestSha256, string configSha256) {
        CanonicalImageId = canonicalImageId;
        LocalImageId = localImageId;
        ManifestSha256 = manifestSha256;
        ConfigSha256 = configSha256;
    }

    /// <summary>Validates the raw manifest/config chain and the local image's platform and ordered RootFS DiffIDs.</summary>
    /// <param name="canonicalImageId">Immutable manifest identity from the unchanged toolchain lock.</param>
    /// <param name="lockedTag">Optional lookup reference from that same lock; never trusted as image identity.</param>
    /// <param name="identityRoot">Explicit external directory holding the raw proof documents.</param>
    /// <param name="inspectImage">Optional Docker adapter; null inspection means the reference is not present locally.</param>
    /// <returns>The canonical identity and a separately verified local execution ID.</returns>
    public static Ps3SdkImageIdentity Resolve(string canonicalImageId, string lockedTag, string identityRoot, Func<string, JsonObject> inspectImage = null) {
        ValidateDigest(canonicalImageId, "locked SDK image");
        if (string.IsNullOrWhiteSpace(identityRoot) || !Path.IsPathRooted(identityRoot)) {
            throw new InvalidDataException("SDK identity proof requires an explicit absolute root.");
        }
        byte[] manifestBytes = File.ReadAllBytes(Path.Combine(identityRoot, "manifest.json"));
        byte[] configBytes = File.ReadAllBytes(Path.Combine(identityRoot, "config.json"));
        string manifestDigest = Digest(manifestBytes);
        if (!string.Equals(manifestDigest, canonicalImageId, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidDataException("SDK raw manifest SHA-256 does not match the locked image identity.");
        }
        JsonObject manifest = ParseObject(manifestBytes, "manifest");
        if (ReadInteger(manifest, "schemaVersion") != 2) throw new InvalidDataException("SDK manifest schemaVersion must be 2.");
        RequireMediaType(manifest, "mediaType", "application/vnd.oci.image.manifest.v1+json", "application/vnd.docker.distribution.manifest.v2+json");
        JsonObject configDescriptor = ReadObject(manifest, "config");
        RequireMediaType(configDescriptor, "mediaType", "application/vnd.oci.image.config.v1+json", "application/vnd.docker.container.image.v1+json");
        string expectedConfigDigest = ReadString(configDescriptor, "digest");
        ValidateDigest(expectedConfigDigest, "config descriptor");
        string configDigest = Digest(configBytes);
        if (!string.Equals(configDigest, expectedConfigDigest, StringComparison.OrdinalIgnoreCase) ||
            ReadInteger(configDescriptor, "size") != configBytes.LongLength) {
            throw new InvalidDataException("SDK raw config SHA-256 or byte length does not match the manifest descriptor.");
        }
        JsonObject config = ParseObject(configBytes, "config");
        RequirePlatform(config, "os", "architecture");
        JsonObject rootfs = ReadObject(config, "rootfs");
        if (ReadString(rootfs, "type") != "layers") throw new InvalidDataException("SDK config rootfs.type must be layers.");
        string[] diffIds = ReadDiffIds(rootfs, "diff_ids");
        if (manifest["layers"] is not JsonArray layers || layers.Count != diffIds.Length) {
            throw new InvalidDataException("SDK manifest layer count differs from the config DiffIDs.");
        }

        inspectImage ??= InspectDockerImage;
        string[] references = new[] { lockedTag, configDigest, canonicalImageId }
            .Where(reference => !string.IsNullOrWhiteSpace(reference)).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        JsonObject inspection = null;
        foreach (string reference in references) {
            inspection = inspectImage(reference);
            if (inspection != null) break;
        }
        if (inspection == null) throw new InvalidDataException("Neither the locked SDK manifest nor its proven config exists in the local Docker image store.");
        string localId = ReadString(inspection, "Id");
        if (!string.Equals(localId, canonicalImageId, StringComparison.OrdinalIgnoreCase) &&
            !string.Equals(localId, configDigest, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidDataException("Local Docker image ID is neither the locked manifest nor its proven config digest.");
        }
        RequirePlatform(inspection, "Os", "Architecture");
        JsonObject localRootfs = ReadObject(inspection, "RootFS");
        if (ReadString(localRootfs, "Type") != "layers" ||
            !diffIds.SequenceEqual(ReadDiffIds(localRootfs, "Layers"), StringComparer.OrdinalIgnoreCase)) {
            throw new InvalidDataException("Local Docker RootFS layers do not match the SDK config's ordered DiffIDs.");
        }
        return new Ps3SdkImageIdentity(canonicalImageId, localId, manifestDigest[7..], configDigest[7..]);
    }

    /// <summary>Inspects an immutable candidate or the lock's lookup tag without invoking a shell or pulling an image.</summary>
    static JsonObject InspectDockerImage(string reference) {
        ProcessStartInfo startInfo = new("docker") {
            UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true
        };
        foreach (string argument in new[] { "image", "inspect", reference, "--format", "{{json .}}" }) startInfo.ArgumentList.Add(argument);
        using Process process = Process.Start(startInfo) ?? throw new InvalidOperationException("Docker image inspection could not be started.");
        Task<string> output = process.StandardOutput.ReadToEndAsync();
        Task<string> error = process.StandardError.ReadToEndAsync();
        process.WaitForExit();
        string outputText = output.GetAwaiter().GetResult();
        string errorText = error.GetAwaiter().GetResult();
        if (process.ExitCode != 0) {
            if (errorText.Contains("No such image:", StringComparison.OrdinalIgnoreCase)) return null;
            throw new InvalidOperationException($"Docker SDK image inspection failed ({process.ExitCode}): {errorText}");
        }
        return ParseObject(System.Text.Encoding.UTF8.GetBytes(outputText), "Docker inspection");
    }

    /// <summary>Hashes the exact proof bytes before any parsing or normalization.</summary>
    static string Digest(byte[] bytes) => "sha256:" + Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

    /// <summary>Parses a required JSON object and reports malformed proof as an explicit data-validation failure.</summary>
    static JsonObject ParseObject(byte[] bytes, string name) {
        try {
            return JsonNode.Parse(bytes) as JsonObject ?? throw new InvalidDataException($"SDK {name} must be a JSON object.");
        } catch (JsonException exception) {
            throw new InvalidDataException($"SDK {name} is not valid JSON.", exception);
        }
    }

    /// <summary>Reads a required JSON object without accepting arrays or scalar values.</summary>
    static JsonObject ReadObject(JsonObject parent, string name) =>
        parent[name] as JsonObject ?? throw new InvalidDataException($"SDK identity is missing object '{name}'.");

    /// <summary>Reads one nonempty required string without implicit numeric or boolean conversion.</summary>
    static string ReadString(JsonObject parent, string name) {
        if (parent[name] is not JsonValue value || !value.TryGetValue(out string result) || string.IsNullOrWhiteSpace(result)) {
            throw new InvalidDataException($"SDK identity is missing string '{name}'.");
        }
        return result;
    }

    /// <summary>Reads integral schema and byte-size values without accepting strings or fractional numbers.</summary>
    static long ReadInteger(JsonObject parent, string name) {
        if (parent[name] is not JsonValue value || !value.TryGetValue(out long result)) throw new InvalidDataException($"SDK identity is missing integer '{name}'.");
        return result;
    }

    /// <summary>Accepts only the supported runnable OCI or Docker schema-2 document media types.</summary>
    static void RequireMediaType(JsonObject parent, string name, string oci, string docker) {
        string mediaType = ReadString(parent, name);
        if (mediaType != oci && mediaType != docker) throw new InvalidDataException($"Unsupported SDK identity media type '{mediaType}'.");
    }

    /// <summary>Requires the SDK container's execution platform, independently of the compiler's PowerPC target.</summary>
    static void RequirePlatform(JsonObject parent, string osName, string architectureName) {
        if (ReadString(parent, osName) != "linux" || ReadString(parent, architectureName) != "amd64") {
            throw new InvalidDataException("SDK image platform must be linux/amd64.");
        }
    }

    /// <summary>Preserves layer order while validating every required SHA-256 DiffID.</summary>
    static string[] ReadDiffIds(JsonObject parent, string name) {
        if (parent[name] is not JsonArray array) throw new InvalidDataException($"SDK identity is missing array '{name}'.");
        return array.Select(item => {
            if (item is not JsonValue value || !value.TryGetValue(out string digest)) throw new InvalidDataException("SDK layer DiffID must be a string.");
            ValidateDigest(digest, "layer DiffID");
            return digest;
        }).ToArray();
    }

    /// <summary>Rejects mutable tags and truncated or non-SHA-256 digests in content-addressed fields.</summary>
    static void ValidateDigest(string digest, string name) {
        if (digest == null || !digest.StartsWith("sha256:", StringComparison.Ordinal) || digest.Length != 71 ||
            digest.AsSpan(7).ContainsAnyExcept("0123456789abcdefABCDEF")) {
            throw new InvalidDataException($"SDK {name} must be a full sha256 digest.");
        }
    }
}
