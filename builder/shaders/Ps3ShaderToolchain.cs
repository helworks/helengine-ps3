using System.Security.Cryptography;
using System.Text.Json;

namespace helengine.ps3.builder;

/// <summary>Loads and validates the repository-locked Docker image and read-only NVIDIA Cg mount.</summary>
public sealed class Ps3ShaderToolchain {
    /// <summary>Creates an immutable toolchain description after its lock and mounted Cg library are validated.</summary>
    /// <param name="imageId">Immutable Docker image identifier.</param>
    /// <param name="cgMountSource">Resolved host directory containing the Cg toolkit.</param>
    /// <param name="cgMountTarget">Locked absolute path inside the Docker image.</param>
    /// <param name="compilerPath">Locked cgcomp executable path inside the Docker image.</param>
    /// <param name="cgLibraryDirectory">Locked Cg library directory inside the image.</param>
    /// <param name="identity">Stable identity formed from image, compiler and Cg library digest.</param>
    Ps3ShaderToolchain(string imageId, string cgMountSource, string cgMountTarget, string compilerPath, string cgLibraryDirectory, string identity) {
        ImageId = imageId;
        CgMountSource = cgMountSource;
        CgMountTarget = cgMountTarget;
        CompilerPath = compilerPath;
        CgLibraryDirectory = cgLibraryDirectory;
        Identity = identity;
    }

    /// <summary>Gets the content-addressed Docker image ID from toolchain-lock.json.</summary>
    public string ImageId { get; }

    /// <summary>Gets the resolved host path mounted read-only for cgcomp's Cg shared library.</summary>
    public string CgMountSource { get; }

    /// <summary>Gets the locked absolute container target of the read-only Cg mount.</summary>
    public string CgMountTarget { get; }

    /// <summary>Gets the cgcomp executable path inside the pinned SDK image.</summary>
    public string CompilerPath { get; }

    /// <summary>Gets the locked Cg library directory used for the compiler process environment.</summary>
    internal string CgLibraryDirectory { get; }

    /// <summary>Gets the stable image, compiler and Cg digest identity attached to each compiled program.</summary>
    public string Identity { get; }

    /// <summary>Loads the repository lock and verifies the exact mounted Cg library before returning a toolchain.</summary>
    /// <param name="repositoryRoot">Explicit repository root containing toolchain-lock.json.</param>
    /// <returns>A validated immutable toolchain configuration.</returns>
    public static Ps3ShaderToolchain Load(string repositoryRoot) {
        if (string.IsNullOrWhiteSpace(repositoryRoot)) throw new ArgumentException("Repository root must be provided.", nameof(repositoryRoot));
        string fullRepositoryRoot = Path.GetFullPath(repositoryRoot);
        string lockPath = Path.Combine(fullRepositoryRoot, "toolchain-lock.json");
        if (!File.Exists(lockPath)) throw new FileNotFoundException("Pinned PS3 toolchain lock was not found.", lockPath);

        using JsonDocument lockDocument = JsonDocument.Parse(File.ReadAllText(lockPath));
        JsonElement root = lockDocument.RootElement;
        string imageId = ReadRequiredString(root, "ResolvedImage", "ImageId");
        string compilerPath = ReadRequiredString(root, "ResolvedImage", "CgcompPath");
        string mountSourceRelative = ReadRequiredString(root, "ShaderToolkit", "MountSource");
        string mountTarget = ReadRequiredString(root, "ShaderToolkit", "MountTarget");
        string libraryPath = ReadRequiredString(root, "ShaderToolkit", "LibraryPath");
        string expectedDigest = ReadRequiredString(root, "ShaderToolkit", "LibCgSha256");
        ValidateImageId(imageId);
        ValidateContainerPath(compilerPath, nameof(compilerPath));
        ValidateContainerPath(mountTarget, nameof(mountTarget));
        ValidateDigest(expectedDigest);

        if (Path.IsPathRooted(mountSourceRelative)) throw new InvalidDataException("Locked Cg mount source must be relative to the repository root.");
        string cgMountSource = Path.GetFullPath(Path.Combine(fullRepositoryRoot, mountSourceRelative));
        EnsureContained(fullRepositoryRoot, cgMountSource, "Locked Cg mount source escapes the repository root.");
        if (!Directory.Exists(cgMountSource)) throw new DirectoryNotFoundException($"Locked Cg mount source does not exist: {cgMountSource}");

        string libraryRelative = libraryPath.Replace('/', Path.DirectorySeparatorChar);
        if (Path.IsPathRooted(libraryRelative)) throw new InvalidDataException("Locked Cg library path must be relative to its mount.");
        string libraryFullPath = Path.GetFullPath(Path.Combine(cgMountSource, libraryRelative));
        EnsureContained(cgMountSource, libraryFullPath, "Locked Cg library path escapes its mount source.");
        if (!File.Exists(libraryFullPath)) throw new FileNotFoundException("Locked Cg shared library is missing.", libraryFullPath);
        string actualDigest;
        using (FileStream libraryStream = File.OpenRead(libraryFullPath)) actualDigest = Convert.ToHexString(SHA256.HashData(libraryStream));
        if (!string.Equals(actualDigest, expectedDigest, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidDataException($"Mounted Cg library SHA-256 mismatch: expected {expectedDigest}, found {actualDigest}.");
        }

        string containerLibraryDirectory = GetContainerDirectory(mountTarget, libraryPath);
        string identity = $"{imageId}|{compilerPath}|{actualDigest}";
        return new Ps3ShaderToolchain(imageId, cgMountSource, mountTarget, compilerPath, containerLibraryDirectory, identity);
    }

    /// <summary>Reads one required nonempty string from a nested toolchain-lock.json object.</summary>
    /// <param name="root">Parsed lock root.</param>
    /// <param name="objectName">Parent property name.</param>
    /// <param name="propertyName">Required string property name.</param>
    /// <returns>The required lock value.</returns>
    static string ReadRequiredString(JsonElement root, string objectName, string propertyName) {
        if (!root.TryGetProperty(objectName, out JsonElement parent) || parent.ValueKind != JsonValueKind.Object) {
            throw new InvalidDataException($"Toolchain lock is missing object '{objectName}'.");
        }
        if (!parent.TryGetProperty(propertyName, out JsonElement property) || property.ValueKind != JsonValueKind.String) {
            throw new InvalidDataException($"Toolchain lock is missing string '{objectName}.{propertyName}'.");
        }
        string value = property.GetString();
        if (string.IsNullOrWhiteSpace(value)) throw new InvalidDataException($"Toolchain lock value '{objectName}.{propertyName}' must not be empty.");
        return value;
    }

    /// <summary>Validates that a Docker image identity is a full immutable SHA-256 image ID.</summary>
    /// <param name="imageId">Locked Docker image ID.</param>
    static void ValidateImageId(string imageId) {
        const string Prefix = "sha256:";
        if (!imageId.StartsWith(Prefix, StringComparison.Ordinal) || imageId.Length != Prefix.Length + 64 || !IsHex(imageId.AsSpan(Prefix.Length))) {
            throw new InvalidDataException("Locked Docker ImageId must be sha256 followed by exactly 64 hexadecimal characters.");
        }
    }

    /// <summary>Validates that a Cg library digest is exactly 64 hexadecimal characters.</summary>
    /// <param name="digest">Locked SHA-256 value.</param>
    static void ValidateDigest(string digest) {
        if (digest.Length != 64 || !IsHex(digest.AsSpan())) throw new InvalidDataException("Locked Cg LibCgSha256 must contain exactly 64 hexadecimal characters.");
    }

    /// <summary>Determines whether every character in a digest span is hexadecimal.</summary>
    /// <param name="value">Candidate hexadecimal text.</param>
    /// <returns>True when all characters are ASCII hexadecimal digits.</returns>
    static bool IsHex(ReadOnlySpan<char> value) {
        foreach (char character in value) {
            if (!(character >= '0' && character <= '9') && !(character >= 'a' && character <= 'f') && !(character >= 'A' && character <= 'F')) return false;
        }
        return true;
    }

    /// <summary>Validates an absolute POSIX path that is passed as a container executable or mount target.</summary>
    /// <param name="path">Locked container path.</param>
    /// <param name="name">Field name for the diagnostic.</param>
    static void ValidateContainerPath(string path, string name) {
        if (path.Length == 0 || path[0] != '/' || path.Contains(',') || path.Contains('\0')) {
            throw new InvalidDataException($"Locked container path '{name}' must be an absolute POSIX path without mount delimiters.");
        }
    }

    /// <summary>Ensures a normalized path remains at or below its designated root.</summary>
    /// <param name="root">Allowed normalized root.</param>
    /// <param name="path">Candidate normalized path.</param>
    /// <param name="error">Diagnostic used when the path escapes.</param>
    static void EnsureContained(string root, string path, string error) {
        string relative = Path.GetRelativePath(root, path);
        if (Path.IsPathRooted(relative) || relative == ".." || relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)) {
            throw new InvalidDataException(error);
        }
    }

    /// <summary>Combines the locked mount target and Cg library's relative parent directory using POSIX separators.</summary>
    /// <param name="mountTarget">Locked Cg mount target.</param>
    /// <param name="libraryPath">Locked Cg library path below the mount.</param>
    /// <returns>Container directory containing libCg.so.</returns>
    static string GetContainerDirectory(string mountTarget, string libraryPath) {
        string normalizedLibraryPath = libraryPath.Replace('\\', '/');
        int separator = normalizedLibraryPath.LastIndexOf('/');
        string libraryDirectory = separator < 0 ? string.Empty : normalizedLibraryPath[..separator];
        string mount = mountTarget.TrimEnd('/');
        return libraryDirectory.Length == 0 ? mount : $"{mount}/{libraryDirectory}";
    }
}
