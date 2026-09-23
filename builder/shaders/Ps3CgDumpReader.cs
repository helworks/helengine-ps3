using helengine;
using System.Globalization;

namespace helengine.ps3.builder;

/// <summary>Decodes shared Cg profile and variable declarations without interpreting RSX binary records.</summary>
internal static class Ps3CgDumpReader {
    /// <summary>Maps one exact Cg declaration type token to the pinned SDK parameter type.</summary>
    /// <param name="typeToken">Cg type token from a #var declaration.</param>
    /// <returns>The matching SDK type code, or Unknown when the token is outside the pinned subset.</returns>
    public static Ps3RsxDataType ParseDataType(string typeToken) {
        return typeToken.ToLowerInvariant() switch {
            "bool" => Ps3RsxDataType.Bool,
            "bool1" => Ps3RsxDataType.Bool1,
            "bool2" => Ps3RsxDataType.Bool2,
            "bool3" => Ps3RsxDataType.Bool3,
            "bool4" => Ps3RsxDataType.Bool4,
            "float" => Ps3RsxDataType.Float,
            "float1" => Ps3RsxDataType.Float1,
            "float2" => Ps3RsxDataType.Float2,
            "float3" => Ps3RsxDataType.Float3,
            "float4" => Ps3RsxDataType.Float4,
            "float3x4" => Ps3RsxDataType.Float3x4,
            "float4x4" => Ps3RsxDataType.Float4x4,
            "float3x3" => Ps3RsxDataType.Float3x3,
            "float4x3" => Ps3RsxDataType.Float4x3,
            "sampler1d" => Ps3RsxDataType.Sampler1D,
            "sampler2d" => Ps3RsxDataType.Sampler2D,
            "sampler3d" => Ps3RsxDataType.Sampler3D,
            "samplercube" => Ps3RsxDataType.SamplerCube,
            "samplerrect" => Ps3RsxDataType.SamplerRect,
            _ => Ps3RsxDataType.Unknown
        };
    }

    /// <summary>Reads every variable declaration after confirming the dump profile matches the requested stage.</summary>
    /// <param name="stage">Expected vertex or pixel stage.</param>
    /// <param name="assembly">Unmodified cgcomp dump text.</param>
    /// <returns>All parsed declarations in dump order, including inactive declarations.</returns>
    public static IReadOnlyList<Ps3CgDumpVariable> Read(ShaderStage stage, string assembly) {
        if (stage != ShaderStage.Vertex && stage != ShaderStage.Pixel) throw new NotSupportedException($"Cg dump stage '{stage}' is unsupported.");
        if (string.IsNullOrWhiteSpace(assembly)) throw new InvalidDataException("cgcomp dump text is required to read declarations.");

        string expectedProfile = stage == ShaderStage.Vertex ? "vp40" : "fp40";
        bool foundProfile = false;
        List<Ps3CgDumpVariable> variables = new();
        using StringReader reader = new(assembly);
        string line;
        while ((line = reader.ReadLine()) != null) {
            string trimmed = line.Trim();
            if (trimmed.StartsWith("#profile ", StringComparison.Ordinal)) {
                if (foundProfile) throw new InvalidDataException("cgcomp dump contains more than one profile declaration.");
                string profile = trimmed[9..].Trim();
                if (!string.Equals(profile, expectedProfile, StringComparison.Ordinal)) {
                    throw new InvalidDataException($"cgcomp dump profile '{profile}' does not match SDK stage '{stage}'.");
                }
                foundProfile = true;
                continue;
            }
            if (!trimmed.StartsWith("#var ", StringComparison.Ordinal)) continue;

            string[] fields = trimmed[5..].Split(':');
            if (fields.Length != 5) throw new InvalidDataException($"Malformed cgcomp #var declaration: {trimmed}");
            string declaration = fields[0].Trim();
            int nameStart = declaration.IndexOfAny([' ', '\t']);
            if (nameStart <= 0 || nameStart == declaration.Length - 1) throw new InvalidDataException($"Malformed cgcomp #var declaration: {trimmed}");
            string typeToken = declaration[..nameStart].Trim();
            string name = declaration[(nameStart + 1)..].Trim();
            if (typeToken.Length == 0 || name.Length == 0) throw new InvalidDataException($"Malformed cgcomp #var declaration: {trimmed}");
            if (!int.TryParse(fields[3].Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out int compilerParameterIndex)) {
                throw new InvalidDataException($"cgcomp #var declaration has an invalid compiler parameter index: {trimmed}");
            }
            if (!int.TryParse(fields[4].Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out int activeFlag) || (activeFlag != 0 && activeFlag != 1)) {
                throw new InvalidDataException($"cgcomp #var declaration has an invalid activity flag: {trimmed}");
            }
            variables.Add(new Ps3CgDumpVariable(
                typeToken,
                name,
                fields[1].Trim(),
                fields[2].Trim(),
                compilerParameterIndex,
                activeFlag == 1));
        }

        if (!foundProfile) throw new InvalidDataException("cgcomp dump has no stage profile declaration.");
        return Array.AsReadOnly(variables.ToArray());
    }
}
