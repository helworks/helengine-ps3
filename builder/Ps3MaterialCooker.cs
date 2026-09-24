using System.Globalization;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.ps3.builder;

/// <summary>Translates the PlayStation 3 standard material schema into a cooked asset.</summary>
///
/// <remarks>
/// The schema is deliberately permissive about surface state. Translucent base colors and
/// double-sided and vertex-color choices are recorded faithfully in the P3MT envelope.
/// The standard packager supplies canonical cooked texture paths, which this envelope
/// preserves after relative-path validation. Native material loading and rendering still
/// require a later integration step before this producer can be treated as runtime acceptance.
/// </remarks>
public sealed class Ps3MaterialCooker {
    /// <summary>Field key for the general editor's optional custom-shader selection.</summary>
    const string UseCustomShaderFieldId = "use-custom-shader";

    /// <summary>Cooks one standard-schema material into the bounded P3MT payload and shader dependency.</summary>
    /// <param name="request">Material schema request supplied by the generic build graph.</param>
    /// <returns>Versioned PS3 material bytes and the exact standard shader pair they reference.</returns>
    /// <exception cref="ArgumentNullException">Thrown when the request is missing.</exception>
    /// <exception cref="InvalidOperationException">Thrown when the request targets another platform, schema or graphics profile, or authors state the platform cannot represent.</exception>
    public PlatformMaterialCookResult Cook(PlatformMaterialCookRequest request) {
        if (request == null) {
            throw new ArgumentNullException(nameof(request));
        } else if (!string.Equals(request.TargetPlatformId, "ps3", StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"PlayStation 3 cannot cook materials for target platform '{request.TargetPlatformId}'.");
        } else if (!string.Equals(request.SchemaId, Ps3MaterialSchemaIds.StandardSchemaId, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"PlayStation 3 does not support material schema '{request.SchemaId}'.");
        } else if (!string.IsNullOrWhiteSpace(request.SelectedGraphicsProfileId)
            && !string.Equals(request.SelectedGraphicsProfileId, Ps3MaterialSchemaIds.StandardGraphicsProfileId, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"PlayStation 3 material requires graphics profile '{Ps3MaterialSchemaIds.StandardGraphicsProfileId}', received '{request.SelectedGraphicsProfileId}'.");
        }

        // Every schema field falls back to its platform definition default. The editor may
        // additionally send general material fields; this PS3 schema ignores its unrelated
        // Mesh variant handoff and owns the fixed ForwardStandard shader pair.
        IReadOnlyDictionary<string, string> fields = request.FieldValues;
        string baseColorValue = Resolve(fields, Ps3MaterialSchemaIds.BaseColorFieldId, "#FFFFFFFF");
        string texturePath = ResolveTexturePath(fields);
        string lightingMode = Resolve(fields, Ps3MaterialSchemaIds.LightingModeFieldId, Ps3MaterialSchemaIds.LightingUnlitValue);
        string doubleSidedMode = Resolve(fields, Ps3MaterialSchemaIds.DoubleSidedFieldId, "false");
        string vertexColorMode = Resolve(fields, Ps3MaterialSchemaIds.VertexColorModeFieldId, Ps3MaterialSchemaIds.VertexColorIgnoreValue);

        string customShaderValue = Resolve(fields, UseCustomShaderFieldId, string.Empty);
        if (!string.IsNullOrEmpty(customShaderValue) && ParseBoolean(customShaderValue, UseCustomShaderFieldId)) {
            throw new InvalidOperationException(
                $"PlayStation 3 {Ps3MaterialSchemaIds.StandardSchemaId} does not support custom shader selection. Use the PS3 standard material schema and its ForwardStandard shader pair.");
        }

        bool lit = ParseChoice(
            lightingMode,
            Ps3MaterialSchemaIds.LightingModeFieldId,
            Ps3MaterialSchemaIds.LightingLitValue,
            Ps3MaterialSchemaIds.LightingUnlitValue);
        bool useVertexColor = ParseChoice(
            vertexColorMode,
            Ps3MaterialSchemaIds.VertexColorModeFieldId,
            Ps3MaterialSchemaIds.VertexColorModulateValue,
            Ps3MaterialSchemaIds.VertexColorIgnoreValue);
        bool doubleSided = ParseBoolean(doubleSidedMode, Ps3MaterialSchemaIds.DoubleSidedFieldId);
        ParseColor(baseColorValue, out byte red, out byte green, out byte blue, out byte alpha);

        Ps3ShaderArtifactIdentity shaderIdentity = Ps3MaterialArtifactWriter.CreateShaderIdentity();
        byte[] payload = Ps3MaterialArtifactWriter.Write(
            request.MaterialAssetId,
            red,
            green,
            blue,
            alpha,
            lit,
            doubleSided,
            useVertexColor,
            texturePath);
        PlatformShaderDependency dependency = new(
            shaderIdentity.ShaderAssetId,
            shaderIdentity.VertexProgramName,
            shaderIdentity.PixelProgramName,
            shaderIdentity.VariantName);

        return new PlatformMaterialCookResult(payload, [dependency]);
    }

    /// <summary>Returns an optional schema field, substituting the schema default when it is absent.</summary>
    /// <param name="fields">Authored field map.</param>
    /// <param name="fieldId">Field identifier.</param>
    /// <param name="defaultValue">Schema default used when the field is absent or blank.</param>
    /// <returns>Trimmed authored value, or the default.</returns>
    static string Resolve(IReadOnlyDictionary<string, string> fields, string fieldId, string defaultValue) {
        string value;
        if (fields == null) {
            throw new ArgumentNullException(nameof(fields));
        } else if (!fields.TryGetValue(fieldId, out value) || string.IsNullOrWhiteSpace(value)) {
            return defaultValue;
        }

        return value.Trim();
    }

    /// <summary>Returns the supplied texture field unchanged, defaulting only when the field is absent.</summary>
    /// <param name="fields">Authored field map.</param>
    /// <returns>Exact supplied value, or empty when the field is absent.</returns>
    static string ResolveTexturePath(IReadOnlyDictionary<string, string> fields) {
        string value;
        if (fields == null) {
            throw new ArgumentNullException(nameof(fields));
        } else if (!fields.TryGetValue(Ps3MaterialSchemaIds.TextureRelativePathFieldId, out value)) {
            return string.Empty;
        }

        return value;
    }

    /// <summary>Parses one authored two-way choice field.</summary>
    /// <param name="value">Authored choice text.</param>
    /// <param name="fieldId">Field identifier used in diagnostics.</param>
    /// <param name="trueValue">Choice that maps to true.</param>
    /// <param name="falseValue">Choice that maps to false.</param>
    /// <returns>True when the value matches the true choice.</returns>
    static bool ParseChoice(string value, string fieldId, string trueValue, string falseValue) {
        if (string.Equals(value, trueValue, StringComparison.OrdinalIgnoreCase)) {
            return true;
        } else if (string.Equals(value, falseValue, StringComparison.OrdinalIgnoreCase)) {
            return false;
        }

        throw new InvalidOperationException(
            $"PlayStation 3 material field '{fieldId}' must be '{trueValue}' or '{falseValue}', received '{value}'.");
    }

    /// <summary>Parses a strict authored boolean field.</summary>
    /// <param name="value">Text value to parse.</param>
    /// <param name="fieldId">Field identifier used in diagnostics.</param>
    /// <returns>Parsed boolean.</returns>
    static bool ParseBoolean(string value, string fieldId) {
        if (!bool.TryParse(value, out bool parsed)) {
            throw new InvalidOperationException($"PlayStation 3 material field '{fieldId}' must be true or false, received '{value}'.");
        }

        return parsed;
    }

    /// <summary>Parses an authored hexadecimal RGB or RGBA color.</summary>
    /// <param name="value">Color in #RRGGBB or #RRGGBBAA form.</param>
    /// <param name="red">Parsed red channel.</param>
    /// <param name="green">Parsed green channel.</param>
    /// <param name="blue">Parsed blue channel.</param>
    /// <param name="alpha">Parsed alpha channel.</param>
    static void ParseColor(string value, out byte red, out byte green, out byte blue, out byte alpha) {
        string normalized = value.Trim();
        if (normalized.StartsWith('#')) {
            normalized = normalized[1..];
        }
        if (normalized.Length == 6) {
            normalized += "FF";
        } else if (normalized.Length != 8) {
            throw new InvalidOperationException($"PlayStation 3 color '{value}' must use #RRGGBB or #RRGGBBAA format.");
        }

        try {
            red = byte.Parse(normalized.AsSpan(0, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            green = byte.Parse(normalized.AsSpan(2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            blue = byte.Parse(normalized.AsSpan(4, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            alpha = byte.Parse(normalized.AsSpan(6, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
        } catch (FormatException exception) {
            throw new InvalidOperationException($"PlayStation 3 color '{value}' contains invalid hexadecimal channels.", exception);
        }
    }
}
