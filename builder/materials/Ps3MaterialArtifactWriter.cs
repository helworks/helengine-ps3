using System.Buffers.Binary;
using System.Text;

namespace helengine.ps3.builder;

/// <summary>Writes the bounded PlayStation 3 material envelope and fixed standard-shader identity.</summary>
public static class Ps3MaterialArtifactWriter {
    /// <summary>First literal byte of the P3MT envelope magic.</summary>
    const byte MagicP = 80;

    /// <summary>Second literal byte of the P3MT envelope magic.</summary>
    const byte Magic3 = 51;

    /// <summary>Third literal byte of the P3MT envelope magic.</summary>
    const byte MagicM = 77;

    /// <summary>Fourth literal byte of the P3MT envelope magic.</summary>
    const byte MagicT = 84;

    /// <summary>Version written into every material envelope.</summary>
    const uint EnvelopeVersion = 1;

    /// <summary>Maximum accepted size of one UTF-8 string in the envelope.</summary>
    const int MaximumStringBytes = 65_536;

    /// <summary>Maximum accepted size of the complete P3MT envelope in bytes.</summary>
    const int MaximumEnvelopeBytes = 1_048_576;

    /// <summary>Number of fixed bytes before the first length-prefixed string.</summary>
    const int HeaderByteCount = 12;

    /// <summary>Number of raw base-color channel bytes.</summary>
    const int BaseColorByteCount = 4;

    /// <summary>Creates the standard shader identity selected by the PS3 material schema.</summary>
    /// <returns>Immutable identity for the shared standard shader pair.</returns>
    public static Ps3ShaderArtifactIdentity CreateShaderIdentity() {
        return new Ps3ShaderArtifactIdentity(
            "ForwardStandardShader",
            "ForwardStandardShader.vs",
            "ForwardStandardShader.ps",
            "ForwardStandard");
    }

    /// <summary>Writes one P3MT material envelope from authored surface values.</summary>
    /// <param name="materialAssetId">Exact material asset identifier.</param>
    /// <param name="red">Authored red color byte.</param>
    /// <param name="green">Authored green color byte.</param>
    /// <param name="blue">Authored blue color byte.</param>
    /// <param name="alpha">Authored alpha color byte.</param>
    /// <param name="lit">Whether the material receives scene lighting.</param>
    /// <param name="doubleSided">Whether both surface orientations are visible.</param>
    /// <param name="useVertexColor">Whether vertex color modulates the base color.</param>
    /// <param name="diffuseTextureRelativePath">Optional validated content-relative texture path.</param>
    /// <returns>Complete P3MT version 1 byte envelope.</returns>
    /// <exception cref="ArgumentNullException">Thrown when a required identity or texture string is null.</exception>
    /// <exception cref="ArgumentException">Thrown when text, a texture path or the complete envelope exceeds the wire contract.</exception>
    public static byte[] Write(
        string materialAssetId,
        byte red,
        byte green,
        byte blue,
        byte alpha,
        bool lit,
        bool doubleSided,
        bool useVertexColor,
        string diffuseTextureRelativePath) {
        byte[] materialAssetIdBytes = EncodeRequiredText(materialAssetId, nameof(materialAssetId));
        Ps3ShaderArtifactIdentity shaderIdentity = CreateShaderIdentity();
        byte[] rendererFamilyIdBytes = EncodeRequiredText(
            Ps3MaterialSchemaIds.StandardGraphicsProfileId,
            nameof(Ps3MaterialSchemaIds.StandardGraphicsProfileId));
        byte[] shaderAssetIdBytes = EncodeRequiredText(shaderIdentity.ShaderAssetId, nameof(shaderIdentity.ShaderAssetId));
        byte[] vertexProgramNameBytes = EncodeRequiredText(shaderIdentity.VertexProgramName, nameof(shaderIdentity.VertexProgramName));
        byte[] pixelProgramNameBytes = EncodeRequiredText(shaderIdentity.PixelProgramName, nameof(shaderIdentity.PixelProgramName));
        byte[] variantNameBytes = EncodeRequiredText(shaderIdentity.VariantName, nameof(shaderIdentity.VariantName));
        byte[] shaderRelativePathBytes = EncodeRequiredText(shaderIdentity.RuntimeRelativePath, nameof(shaderIdentity.RuntimeRelativePath));
        byte[] textureRelativePathBytes = EncodeTexturePath(diffuseTextureRelativePath);
        byte[][] encodedStrings = [
            materialAssetIdBytes,
            rendererFamilyIdBytes,
            shaderAssetIdBytes,
            vertexProgramNameBytes,
            pixelProgramNameBytes,
            variantNameBytes,
            shaderRelativePathBytes,
            textureRelativePathBytes
        ];

        int totalByteLength = checked(HeaderByteCount + BaseColorByteCount + sizeof(uint));
        foreach (byte[] encodedString in encodedStrings) {
            totalByteLength = checked(totalByteLength + sizeof(uint) + encodedString.Length);
        }
        if (totalByteLength > MaximumEnvelopeBytes) {
            throw new ArgumentException(
                $"P3MT material envelope exceeds the {MaximumEnvelopeBytes}-byte limit.",
                nameof(diffuseTextureRelativePath));
        }

        byte[] envelope = new byte[totalByteLength];
        int offset = 0;
        envelope[offset++] = MagicP;
        envelope[offset++] = Magic3;
        envelope[offset++] = MagicM;
        envelope[offset++] = MagicT;
        WriteUInt32(envelope, ref offset, EnvelopeVersion);
        WriteUInt32(envelope, ref offset, checked((uint)totalByteLength));
        foreach (byte[] encodedString in encodedStrings[..7]) {
            WriteString(envelope, ref offset, encodedString);
        }

        envelope[offset++] = red;
        envelope[offset++] = green;
        envelope[offset++] = blue;
        envelope[offset++] = alpha;

        uint surfaceFlags = 0;
        if (lit) surfaceFlags |= 1u;
        if (doubleSided) surfaceFlags |= 2u;
        if (useVertexColor) surfaceFlags |= 4u;
        WriteUInt32(envelope, ref offset, surfaceFlags);
        WriteString(envelope, ref offset, textureRelativePathBytes);

        if (offset != envelope.Length) {
            throw new InvalidOperationException(
                $"P3MT writer produced {offset} bytes for a {envelope.Length}-byte envelope.");
        }
        return envelope;
    }

    /// <summary>Encodes one required nonblank string using strict UTF-8 and the P3MT byte bound.</summary>
    /// <param name="value">Required string to validate and encode.</param>
    /// <param name="fieldName">Argument name used in validation diagnostics.</param>
    /// <returns>Bounded UTF-8 bytes for the original string.</returns>
    static byte[] EncodeRequiredText(string value, string fieldName) {
        if (value == null) {
            throw new ArgumentNullException(fieldName);
        } else if (string.IsNullOrWhiteSpace(value)) {
            throw new ArgumentException($"P3MT field '{fieldName}' must not be blank.", fieldName);
        } else if (value.IndexOf('\0') >= 0) {
            throw new ArgumentException($"P3MT field '{fieldName}' contains NUL.", fieldName);
        }

        return EncodeBoundedUtf8(value, fieldName);
    }

    /// <summary>Validates the optional texture path without changing any valid spelling, then encodes it.</summary>
    /// <param name="texturePath">Optional content-relative texture path; empty requests the default white texture.</param>
    /// <returns>Bounded UTF-8 bytes for the empty or validated path.</returns>
    static byte[] EncodeTexturePath(string texturePath) {
        if (texturePath == null) {
            throw new ArgumentNullException(nameof(texturePath));
        }
        if (texturePath.Length == 0) {
            return Array.Empty<byte>();
        }
        byte[] encodedPath = EncodeBoundedUtf8(texturePath, nameof(texturePath));

        if (texturePath[0] == '/' || texturePath[^1] == '/') {
            throw new ArgumentException("P3MT texture path must not start or end with a slash.", nameof(texturePath));
        }
        for (int index = 0; index < texturePath.Length; index++) {
            char character = texturePath[index];
            if (character == '\\' || character == ':') {
                throw new ArgumentException("P3MT texture path must use relative forward-slash components.", nameof(texturePath));
            } else if (character == '\0') {
                throw new ArgumentException("P3MT texture path contains NUL.", nameof(texturePath));
            } else if (char.IsControl(character)) {
                throw new ArgumentException("P3MT texture path contains a control character.", nameof(texturePath));
            }
        }

        string[] components = texturePath.Split('/');
        foreach (string component in components) {
            if (component.Length == 0 || component == "." || component == ".." || string.IsNullOrWhiteSpace(component)) {
                throw new ArgumentException("P3MT texture path contains an empty, blank, or traversal component.", nameof(texturePath));
            }
        }

        return encodedPath;
    }

    /// <summary>Encodes valid Unicode into strict UTF-8 after measuring its byte count against the wire limit.</summary>
    /// <param name="value">String to encode.</param>
    /// <param name="fieldName">Argument name used in validation diagnostics.</param>
    /// <returns>UTF-8 bytes that do not exceed the P3MT per-string bound.</returns>
    static byte[] EncodeBoundedUtf8(string value, string fieldName) {
        UTF8Encoding strictUtf8 = new(false, true);
        int byteCount;
        try {
            byteCount = strictUtf8.GetByteCount(value);
        } catch (EncoderFallbackException exception) {
            throw new ArgumentException($"P3MT field '{fieldName}' is not valid Unicode text.", fieldName, exception);
        }
        if (byteCount > MaximumStringBytes) {
            throw new ArgumentException(
                $"P3MT field '{fieldName}' exceeds the {MaximumStringBytes}-byte string limit.",
                fieldName);
        }

        try {
            return strictUtf8.GetBytes(value);
        } catch (EncoderFallbackException exception) {
            throw new ArgumentException($"P3MT field '{fieldName}' is not valid Unicode text.", fieldName, exception);
        }
    }

    /// <summary>Writes one byte-length-prefixed string record using the required big-endian length encoding.</summary>
    /// <param name="destination">Envelope receiving the record.</param>
    /// <param name="offset">Cursor position, advanced past the record.</param>
    /// <param name="encodedString">Already validated UTF-8 string bytes.</param>
    static void WriteString(byte[] destination, ref int offset, byte[] encodedString) {
        WriteUInt32(destination, ref offset, checked((uint)encodedString.Length));
        encodedString.CopyTo(destination, offset);
        offset = checked(offset + encodedString.Length);
    }

    /// <summary>Writes one unsigned integer in big-endian byte order and advances the output cursor.</summary>
    /// <param name="destination">Envelope receiving the integer.</param>
    /// <param name="offset">Cursor position, advanced by four bytes.</param>
    /// <param name="value">Unsigned integer to encode.</param>
    static void WriteUInt32(byte[] destination, ref int offset, uint value) {
        BinaryPrimitives.WriteUInt32BigEndian(destination.AsSpan(offset, sizeof(uint)), value);
        offset = checked(offset + sizeof(uint));
    }
}
