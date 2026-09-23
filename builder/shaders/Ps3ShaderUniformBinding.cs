namespace helengine.ps3.builder;

/// <summary>Associates one authored constant-buffer member with its validated RSX constant parameter.</summary>
public sealed class Ps3ShaderUniformBinding {
    /// <summary>Creates a mapping for one reflected uniform member and its actual SDK parameter.</summary>
    /// <param name="sourceMember">Original authored buffer member reflection record.</param>
    /// <param name="rsxParameter">Validated constant parameter containing vertex registers or fragment relocations.</param>
    public Ps3ShaderUniformBinding(helengine.ShaderStage stage, Ps3ShaderUniformMember sourceMember, Ps3RsxParameter rsxParameter) {
        Stage = stage;
        SourceMember = sourceMember ?? throw new ArgumentNullException(nameof(sourceMember));
        RsxParameter = rsxParameter ?? throw new ArgumentNullException(nameof(rsxParameter));
        AuthoredBufferName = sourceMember.BufferName;
        AuthoredBufferBinding = sourceMember.BufferBinding;
        AuthoredMemberPath = sourceMember.MemberPath;
        GeneratedName = sourceMember.GeneratedName;
    }

    /// <summary>Gets the source reflection record with the member's declared physical layout.</summary>
    public Ps3ShaderUniformMember SourceMember { get; }

    /// <summary>Gets the stage whose binary constant references are carried by the SDK parameter.</summary>
    public helengine.ShaderStage Stage { get; }

    /// <summary>Gets the exact validated SDK constant parameter.</summary>
    public Ps3RsxParameter RsxParameter { get; }

    /// <summary>Gets the authored constant-buffer name.</summary>
    public string AuthoredBufferName { get; }

    /// <summary>Gets the authored constant-buffer binding.</summary>
    public uint AuthoredBufferBinding { get; }

    /// <summary>Gets the complete member path within the authored buffer.</summary>
    public string AuthoredMemberPath { get; }

    /// <summary>Gets the exact SPIRV-Cross-generated Cg parameter identity.</summary>
    public string GeneratedName { get; }
}
