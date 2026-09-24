using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;

namespace helengine.ps3.builder;

/// <summary>Identifies a cooked vertex and pixel shader pair by its four authored identity components.</summary>
public sealed class Ps3ShaderArtifactIdentity {
    /// <summary>Creates a validated shader identity and derives its stable relative artifact path.</summary>
    /// <param name="shaderAssetId">Stable identifier of the authored shader asset.</param>
    /// <param name="vertexProgramName">Authored vertex program name.</param>
    /// <param name="pixelProgramName">Authored pixel program name.</param>
    /// <param name="variantName">Authored shader variant name.</param>
    public Ps3ShaderArtifactIdentity(string shaderAssetId, string vertexProgramName, string pixelProgramName, string variantName) {
        ShaderAssetId = ValidateText(shaderAssetId, nameof(shaderAssetId));
        VertexProgramName = ValidateText(vertexProgramName, nameof(vertexProgramName));
        PixelProgramName = ValidateText(pixelProgramName, nameof(pixelProgramName));
        VariantName = ValidateText(variantName, nameof(variantName));
        RelativePath = "shaders/" + ComputeKey(ShaderAssetId, VertexProgramName, PixelProgramName, VariantName) + ".ps3sp";
    }

    /// <summary>Gets the shader asset identifier used in the artifact body and path key.</summary>
    public string ShaderAssetId { get; }

    /// <summary>Gets the authored vertex program name used in the artifact body and path key.</summary>
    public string VertexProgramName { get; }

    /// <summary>Gets the authored pixel program name used in the artifact body and path key.</summary>
    public string PixelProgramName { get; }

    /// <summary>Gets the authored variant name used in the artifact body and path key.</summary>
    public string VariantName { get; }

    /// <summary>Gets the stable lowercase SHA-256 path relative to the physical cook root.</summary>
    public string RelativePath { get; }

    /// <summary>Gets the packaged content-source path used by declarations and material references.</summary>
    public string RuntimeRelativePath => "cooked/" + RelativePath;

    /// <summary>Validates required text using the artifact's strict UTF-8 and bounded string rules.</summary>
    /// <param name="value">Text to validate.</param>
    /// <param name="fieldName">Field name included in validation diagnostics.</param>
    /// <returns>The original validated value.</returns>
    internal static string ValidateText(string value, string fieldName) {
        if (string.IsNullOrWhiteSpace(value)) throw new ArgumentException($"Required shader artifact text '{fieldName}' is blank.", fieldName);
        if (value.IndexOf('\0') >= 0) throw new ArgumentException($"Shader artifact text '{fieldName}' contains NUL.", fieldName);
        byte[] encoded;
        try {
            encoded = new UTF8Encoding(false, true).GetBytes(value);
        } catch (EncoderFallbackException exception) {
            throw new ArgumentException($"Shader artifact text '{fieldName}' is not valid Unicode.", fieldName, exception);
        }
        if (encoded.Length > Ps3ShaderArtifactWriter.MaximumStringBytes) {
            throw new ArgumentException($"Shader artifact text '{fieldName}' exceeds {Ps3ShaderArtifactWriter.MaximumStringBytes} UTF-8 bytes.", fieldName);
        }
        return value;
    }

    /// <summary>Computes the unambiguous path digest from length-prefixed UTF-8 identity components.</summary>
    /// <param name="shaderAssetId">Shader asset identifier.</param>
    /// <param name="vertexProgramName">Vertex program name.</param>
    /// <param name="pixelProgramName">Pixel program name.</param>
    /// <param name="variantName">Variant name.</param>
    /// <returns>Lowercase hexadecimal SHA-256 digest.</returns>
    static string ComputeKey(string shaderAssetId, string vertexProgramName, string pixelProgramName, string variantName) {
        using IncrementalHash hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        string[] components = [shaderAssetId, vertexProgramName, pixelProgramName, variantName];
        Span<byte> length = stackalloc byte[4];
        foreach (string component in components) {
            byte[] encoded = new UTF8Encoding(false, true).GetBytes(component);
            BinaryPrimitives.WriteUInt32BigEndian(length, checked((uint)encoded.Length));
            hash.AppendData(length);
            hash.AppendData(encoded);
        }
        return Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
    }
}
