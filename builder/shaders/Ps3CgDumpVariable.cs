namespace helengine.ps3.builder;

/// <summary>Preserves one exact Cg dump variable declaration, including inactive optimized declarations.</summary>
internal sealed class Ps3CgDumpVariable {
    /// <summary>Creates a parsed Cg variable declaration.</summary>
    /// <param name="typeToken">Exact Cg type token.</param>
    /// <param name="name">Exact compiler parameter name.</param>
    /// <param name="semanticBinding">Primary Cg binding, such as $vin.ATTR0 or $vout.TEX0.</param>
    /// <param name="hardwareBinding">Secondary Cg binding, such as ATTR0, c[0], 4, or texunit 0.</param>
    /// <param name="compilerParameterIndex">Cg compiler parameter index from the dump.</param>
    /// <param name="isActive">Whether the compiler marks the declaration active.</param>
    public Ps3CgDumpVariable(string typeToken, string name, string semanticBinding, string hardwareBinding, int compilerParameterIndex, bool isActive) {
        TypeToken = typeToken;
        Name = name;
        SemanticBinding = semanticBinding;
        HardwareBinding = hardwareBinding;
        CompilerParameterIndex = compilerParameterIndex;
        IsActive = isActive;
    }

    /// <summary>Gets the exact compiler-reported Cg type spelling.</summary>
    public string TypeToken { get; }

    /// <summary>Gets the exact generated or built-in parameter name.</summary>
    public string Name { get; }

    /// <summary>Gets the primary semantic or resource binding field.</summary>
    public string SemanticBinding { get; }

    /// <summary>Gets the secondary hardware or constant-register binding field.</summary>
    public string HardwareBinding { get; }

    /// <summary>Gets the signed compiler parameter index stored in the Cg dump.</summary>
    public int CompilerParameterIndex { get; }

    /// <summary>Gets whether this declaration is active in the final Cg program.</summary>
    public bool IsActive { get; }
}
