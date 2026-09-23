namespace helengine.ps3.builder;

/// <summary>Preserves one vertex constant register or one fragment constant relocation element.</summary>
public sealed class Ps3RsxConstantElement {
    /// <summary>Creates a copied element record for one compiler constant vector.</summary>
    /// <param name="elementOrdinal">Zero-based position within the authored array or matrix parameter.</param>
    /// <param name="vertexRegister">Vertex constant register, or null for fragment-stage constants.</param>
    /// <param name="fragmentRelocationOffsets">Copied microcode-relative fragment patch references.</param>
    /// <param name="internalValues">Copied 32-bit SDK values associated with this constant vector.</param>
    internal Ps3RsxConstantElement(uint elementOrdinal, uint? vertexRegister, IEnumerable<uint> fragmentRelocationOffsets, IEnumerable<uint> internalValues) {
        ArgumentNullException.ThrowIfNull(fragmentRelocationOffsets);
        ArgumentNullException.ThrowIfNull(internalValues);
        ElementOrdinal = elementOrdinal;
        VertexRegister = vertexRegister;
        FragmentRelocationOffsets = Array.AsReadOnly(fragmentRelocationOffsets.ToArray());
        InternalValues = Array.AsReadOnly(internalValues.ToArray());
    }

    /// <summary>Gets this vector's zero-based ordinal within its parameter.</summary>
    public uint ElementOrdinal { get; }

    /// <summary>Gets its vertex constant register, or null when fragment relocation records apply.</summary>
    public uint? VertexRegister { get; }

    /// <summary>Gets copied fragment microcode-relative relocation offsets; an empty list means no references.</summary>
    public IReadOnlyList<uint> FragmentRelocationOffsets { get; }

    /// <summary>Gets the four copied big-endian SDK constant words without numeric reinterpretation.</summary>
    public IReadOnlyList<uint> InternalValues { get; }
}
