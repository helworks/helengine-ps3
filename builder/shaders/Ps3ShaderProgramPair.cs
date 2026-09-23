namespace helengine.ps3.builder;

/// <summary>Contains both validated SDK stages, their source reflection, and the reconciled binding map.</summary>
public sealed class Ps3ShaderProgramPair {
    /// <summary>Creates a complete immutable pair after both source translations and SDK programs validate.</summary>
    /// <param name="vertex">Validated vertex SDK program.</param>
    /// <param name="pixel">Validated pixel SDK program.</param>
    /// <param name="vertexTranslation">Vertex source translation and reflection.</param>
    /// <param name="pixelTranslation">Pixel source translation and reflection.</param>
    /// <param name="bindings">Validated authored-to-RSX binding map.</param>
    internal Ps3ShaderProgramPair(Ps3RsxProgram vertex, Ps3RsxProgram pixel, Ps3ShaderTranslation vertexTranslation,
        Ps3ShaderTranslation pixelTranslation, Ps3ShaderBindingMap bindings) {
        Vertex = vertex ?? throw new ArgumentNullException(nameof(vertex));
        Pixel = pixel ?? throw new ArgumentNullException(nameof(pixel));
        VertexTranslation = vertexTranslation ?? throw new ArgumentNullException(nameof(vertexTranslation));
        PixelTranslation = pixelTranslation ?? throw new ArgumentNullException(nameof(pixelTranslation));
        Bindings = bindings ?? throw new ArgumentNullException(nameof(bindings));
        if (Vertex.Stage != helengine.ShaderStage.Vertex || Pixel.Stage != helengine.ShaderStage.Pixel
            || VertexTranslation.Stage != helengine.ShaderStage.Vertex || PixelTranslation.Stage != helengine.ShaderStage.Pixel) {
            throw new ArgumentException("A PS3 shader program pair requires matching vertex and pixel stages.");
        }
    }

    /// <summary>Gets the validated RSX vertex program.</summary>
    public Ps3RsxProgram Vertex { get; }

    /// <summary>Gets the validated RSX pixel program.</summary>
    public Ps3RsxProgram Pixel { get; }

    /// <summary>Gets the vertex source translation and authored reflection.</summary>
    public Ps3ShaderTranslation VertexTranslation { get; }

    /// <summary>Gets the pixel source translation and authored reflection.</summary>
    public Ps3ShaderTranslation PixelTranslation { get; }

    /// <summary>Gets the validated map between authored resources and final RSX parameters/channels.</summary>
    public Ps3ShaderBindingMap Bindings { get; }
}
