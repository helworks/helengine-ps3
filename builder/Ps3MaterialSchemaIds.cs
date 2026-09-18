namespace helengine.ps3.builder;

/// <summary>Defines stable field identifiers for the PlayStation 3 standard material schema.</summary>
public static class Ps3MaterialSchemaIds {
    /// <summary>Stable PlayStation 3 material schema id.</summary>
    public const string StandardSchemaId = "ps3-standard-v1";

    /// <summary>Stable PlayStation 3 graphics profile and cooked renderer family id.</summary>
    public const string StandardGraphicsProfileId = "ps3-standard";

    /// <summary>Authored base-color field identifier.</summary>
    public const string BaseColorFieldId = "base-color";

    /// <summary>Authored texture path field identifier.</summary>
    public const string TextureRelativePathFieldId = "texture-relative-path";

    /// <summary>Authored lighting mode field identifier.</summary>
    public const string LightingModeFieldId = "lighting-mode";

    /// <summary>Authored double-sided mode field identifier.</summary>
    public const string DoubleSidedFieldId = "double-sided";

    /// <summary>Authored vertex-color mode field identifier.</summary>
    public const string VertexColorModeFieldId = "vertex-color-mode";

    /// <summary>Authored lighting choice that shades the surface with scene lights.</summary>
    public const string LightingLitValue = "lit";

    /// <summary>Authored lighting choice that keeps the flat base color.</summary>
    public const string LightingUnlitValue = "unlit";

    /// <summary>Authored vertex-color choice that discards mesh vertex colors.</summary>
    public const string VertexColorIgnoreValue = "ignore";

    /// <summary>Authored vertex-color choice that modulates the base color by mesh vertex colors.</summary>
    public const string VertexColorModulateValue = "modulate";
}
