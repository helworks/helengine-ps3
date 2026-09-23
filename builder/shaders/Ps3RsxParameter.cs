namespace helengine.ps3.builder;

/// <summary>Describes one named or SDK-internal RSX attribute, sampler or constant parameter.</summary>
public sealed class Ps3RsxParameter {
    /// <summary>Creates a validated parameter with copied constant elements.</summary>
    /// <param name="name">Exact SDK compiler name, or null for a valid unnamed internal constant.</param>
    /// <param name="category">SDK resource category.</param>
    /// <param name="hardwareIndex">Attribute or texture-unit index, or null for constants.</param>
    /// <param name="elementCount">Number of physical constant vectors represented by this parameter.</param>
    /// <param name="dataType">Exact SDK type code.</param>
    /// <param name="elements">Copied per-vector constant elements.</param>
    internal Ps3RsxParameter(string name, Ps3RsxParameterCategory category, uint? hardwareIndex, uint elementCount, Ps3RsxDataType dataType, IEnumerable<Ps3RsxConstantElement> elements) {
        ArgumentNullException.ThrowIfNull(elements);
        Ps3RsxConstantElement[] copiedElements = elements.ToArray();
        if (elementCount == 0) throw new ArgumentOutOfRangeException(nameof(elementCount), "RSX parameter element count must be nonzero.");
        if (category == Ps3RsxParameterCategory.Constant && (hardwareIndex.HasValue || copiedElements.Length != elementCount)) {
            throw new ArgumentException("Constant parameters require one copied element per vector and no aggregate hardware index.", nameof(elements));
        }
        if (category != Ps3RsxParameterCategory.Constant && (!hardwareIndex.HasValue || copiedElements.Length != 0 || elementCount != 1)) {
            throw new ArgumentException("Attribute and sampler parameters require one hardware index and no constant elements.", nameof(elements));
        }
        Name = name;
        Category = category;
        HardwareIndex = hardwareIndex;
        ElementCount = elementCount;
        DataType = dataType;
        Elements = Array.AsReadOnly(copiedElements);
    }

    /// <summary>Gets the exact compiler-provided parameter name, or null for unnamed SDK-internal constants.</summary>
    public string Name { get; }

    /// <summary>Gets whether this record is an input attribute, sampler or constant.</summary>
    public Ps3RsxParameterCategory Category { get; }

    /// <summary>Gets the attribute or texture-unit index, or null for constants.</summary>
    public uint? HardwareIndex { get; }

    /// <summary>Gets the number of vector elements in this constant parameter, or one for attributes and samplers.</summary>
    public uint ElementCount { get; }

    /// <summary>Gets the exact SDK parameter data type code.</summary>
    public Ps3RsxDataType DataType { get; }

    /// <summary>Gets copied per-vector constant register or relocation data; empty for non-constant resources.</summary>
    public IReadOnlyList<Ps3RsxConstantElement> Elements { get; }
}
