namespace helengine.ps3.builder;

/// <summary>Names the exact byte values used by the pinned SDK's RSX parameter records.</summary>
public enum Ps3RsxDataType : byte {
    /// <summary>SDK BOOL type code 0.</summary>
    Bool = 0,

    /// <summary>SDK BOOL1 type code 1.</summary>
    Bool1 = 1,

    /// <summary>SDK BOOL2 type code 2.</summary>
    Bool2 = 2,

    /// <summary>SDK BOOL3 type code 3.</summary>
    Bool3 = 3,

    /// <summary>SDK BOOL4 type code 4.</summary>
    Bool4 = 4,

    /// <summary>SDK scalar FLOAT type code 5.</summary>
    Float = 5,

    /// <summary>SDK FLOAT1 type code 6, distinct from scalar FLOAT.</summary>
    Float1 = 6,

    /// <summary>SDK FLOAT2 type code 7.</summary>
    Float2 = 7,

    /// <summary>SDK FLOAT3 type code 8.</summary>
    Float3 = 8,

    /// <summary>SDK FLOAT4 type code 9.</summary>
    Float4 = 9,

    /// <summary>SDK FLOAT3x4 type code 10.</summary>
    Float3x4 = 10,

    /// <summary>SDK FLOAT4x4 type code 11.</summary>
    Float4x4 = 11,

    /// <summary>SDK FLOAT3x3 type code 12.</summary>
    Float3x3 = 12,

    /// <summary>SDK FLOAT4x3 type code 13.</summary>
    Float4x3 = 13,

    /// <summary>SDK SAMPLER1D type code 14.</summary>
    Sampler1D = 14,

    /// <summary>SDK SAMPLER2D type code 15.</summary>
    Sampler2D = 15,

    /// <summary>SDK SAMPLER3D type code 16.</summary>
    Sampler3D = 16,

    /// <summary>SDK SAMPLERCUBE type code 17.</summary>
    SamplerCube = 17,

    /// <summary>SDK SAMPLERRECT type code 18.</summary>
    SamplerRect = 18,

    /// <summary>SDK UNKNOWN type code 255.</summary>
    Unknown = 255
}
