using System.Text;
using helengine.baseplatform.Manifest;

namespace helengine.ps3.builder;

/// <summary>Publishes identity-validated renderer-owned shader paths for generated native Core sources.</summary>
public sealed class Ps3RendererShaderManifestWriter {
    /// <summary>Writes the exact cooked runtime paths required by the PS3 sprite batch renderer.</summary>
    /// <param name="generatedCoreCppRootPath">Generated Core root receiving the renderer shader header.</param>
    /// <param name="manifest">Resolved build manifest containing the cooked renderer shader artifacts.</param>
    /// <returns>Absolute path of the generated renderer shader header.</returns>
    /// <exception cref="ArgumentException">Thrown when the generated Core root is missing.</exception>
    /// <exception cref="ArgumentNullException">Thrown when the build manifest is missing.</exception>
    /// <exception cref="InvalidOperationException">Thrown when any required shader artifact is missing, duplicated, or inconsistent.</exception>
    public string Write(string generatedCoreCppRootPath, PlatformBuildManifest manifest) {
        if (string.IsNullOrWhiteSpace(generatedCoreCppRootPath)) {
            throw new ArgumentException("Generated Core root is required.", nameof(generatedCoreCppRootPath));
        }
        ArgumentNullException.ThrowIfNull(manifest);
        for (int artifactIndex = 0; artifactIndex < manifest.CookedArtifacts.Length; artifactIndex++) {
            if (manifest.CookedArtifacts[artifactIndex] == null) {
                throw new InvalidOperationException($"PlayStation 3 cooked artifact manifest entry at index {artifactIndex} is null.");
            }
        }

        Ps3ShaderArtifactIdentity texturedIdentity = CreateIdentity("Textured");
        Ps3ShaderArtifactIdentity roundedShapeIdentity = CreateIdentity("RoundedShape");
        PlatformBuildArtifact texturedArtifact = ResolveRequiredArtifact(manifest.CookedArtifacts, texturedIdentity);
        PlatformBuildArtifact roundedShapeArtifact = ResolveRequiredArtifact(manifest.CookedArtifacts, roundedShapeIdentity);
        string headerText = CreateHeader(texturedArtifact.RelativePath, roundedShapeArtifact.RelativePath);

        string generatedRoot = Path.GetFullPath(generatedCoreCppRootPath);
        Directory.CreateDirectory(generatedRoot);
        string headerPath = Path.Combine(generatedRoot, "ps3_renderer_shaders.hpp");
        string temporaryPath = Path.Combine(generatedRoot, $".ps3_renderer_shaders.{Guid.NewGuid():N}.tmp");
        try {
            File.WriteAllText(temporaryPath, headerText, new UTF8Encoding(false));
            File.Move(temporaryPath, headerPath, true);
        } catch {
            if (File.Exists(temporaryPath)) File.Delete(temporaryPath);
            throw;
        }

        return headerPath;
    }

    /// <summary>Finds exactly one manifest artifact matching the stable identity and verifies its complete mapping.</summary>
    /// <param name="artifacts">Cooked artifacts from the resolved manifest.</param>
    /// <param name="identity">Expected renderer shader identity.</param>
    /// <returns>The validated shader artifact.</returns>
    static PlatformBuildArtifact ResolveRequiredArtifact(IReadOnlyList<PlatformBuildArtifact> artifacts, Ps3ShaderArtifactIdentity identity) {
        PlatformBuildArtifact[] candidates = artifacts.Where(artifact =>
            string.Equals(artifact.LogicalArtifactId, identity.RelativePath, StringComparison.Ordinal)
            || string.Equals(artifact.RelativePath, identity.RuntimeRelativePath, StringComparison.Ordinal)).ToArray();

        if (candidates.Length == 0) {
            throw new InvalidOperationException($"PlayStation 3 renderer shader '{identity.VariantName}' is missing from the cooked artifact manifest.");
        }
        if (candidates.Length != 1) {
            throw new InvalidOperationException($"PlayStation 3 renderer shader '{identity.VariantName}' has duplicate cooked artifact identities.");
        }

        PlatformBuildArtifact artifact = candidates[0];
        if (!string.Equals(artifact.ArtifactKind, "shader", StringComparison.Ordinal)
            || !string.Equals(artifact.VariantId, identity.VariantName, StringComparison.Ordinal)
            || !string.Equals(artifact.LogicalArtifactId, identity.RelativePath, StringComparison.Ordinal)
            || !string.Equals(artifact.RelativePath, identity.RuntimeRelativePath, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"PlayStation 3 renderer shader '{identity.VariantName}' has an invalid cooked artifact mapping.");
        }

        return artifact;
    }

    /// <summary>Derives one PS3 renderer dependency identity from its authored programs and variant.</summary>
    /// <param name="variantName">The required renderer shader variant.</param>
    /// <returns>The stable identity that defines the manifest paths.</returns>
    static Ps3ShaderArtifactIdentity CreateIdentity(string variantName) => new(
        "SpriteBatchShader", "SpriteBatchShader.vs", "SpriteBatchShader.ps", variantName);

    /// <summary>Formats the deterministic generated header after both manifest records have been validated.</summary>
    /// <param name="texturedRuntimePath">Validated Textured runtime-relative path.</param>
    /// <param name="roundedShapeRuntimePath">Validated RoundedShape runtime-relative path.</param>
    /// <returns>Complete UTF-8 header contents ending in a newline.</returns>
    static string CreateHeader(string texturedRuntimePath, string roundedShapeRuntimePath) {
        StringBuilder header = new();
        header.AppendLine("// Generated by helengine.ps3.builder; do not edit.");
        header.AppendLine("#pragma once");
        header.AppendLine("namespace helengine::ps3 {");
        header.AppendLine("/// Cooked shader pair used by textured sprite batches.");
        header.Append("inline constexpr char BatchTexturedShaderPath[] = \"").Append(texturedRuntimePath).AppendLine("\";");
        header.AppendLine("/// Cooked shader pair used by rounded-shape sprite batches.");
        header.Append("inline constexpr char BatchRoundedShapeShaderPath[] = \"").Append(roundedShapeRuntimePath).AppendLine("\";");
        header.AppendLine("}");
        return header.ToString();
    }
}
