using helengine;

namespace helengine.ps3.builder;

/// <summary>Contains PS3 GLSL output together with immutable reflection and active include dependencies.</summary>
public sealed class Ps3ShaderTranslation {
    /// <summary>Initializes a translation and copies all supplied collections into read-only snapshots.</summary>
    /// <param name="stage">Shader stage used for this translation.</param>
    /// <param name="glslSource">Compiler-generated GLSL source.</param>
    /// <param name="inputs">Active authored stage inputs.</param>
    /// <param name="outputs">Active authored stage outputs.</param>
    /// <param name="uniformMembers">Active constant-buffer members.</param>
    /// <param name="samplers">Active combined texture and sampler bindings.</param>
    /// <param name="dependencies">Active include dependencies resolved for this translation.</param>
    public Ps3ShaderTranslation(
        ShaderStage stage,
        string glslSource,
        IEnumerable<string> inputBuiltIns,
        IEnumerable<string> outputBuiltIns,
        IEnumerable<Ps3ShaderInterfaceVariable> inputs,
        IEnumerable<Ps3ShaderInterfaceVariable> outputs,
        IEnumerable<Ps3ShaderUniformMember> uniformMembers,
        IEnumerable<Ps3ShaderSamplerBinding> samplers,
        IEnumerable<Ps3ShaderSourceDependency> dependencies) {
        if (string.IsNullOrWhiteSpace(glslSource)) throw new ArgumentException("Generated GLSL source must be provided.", nameof(glslSource));
        Stage = stage;
        GlslSource = glslSource;
        InputBuiltIns = Copy(inputBuiltIns, nameof(inputBuiltIns));
        OutputBuiltIns = Copy(outputBuiltIns, nameof(outputBuiltIns));
        Inputs = Copy(inputs, nameof(inputs));
        Outputs = Copy(outputs, nameof(outputs));
        UniformMembers = Copy(uniformMembers, nameof(uniformMembers));
        Samplers = Copy(samplers, nameof(samplers));
        Dependencies = Copy(dependencies, nameof(dependencies));
    }

    /// <summary>Gets the compiled vertex or pixel stage.</summary>
    public ShaderStage Stage { get; }

    /// <summary>Gets the SPIRV-Cross-generated GLSL source without post-emission rewriting.</summary>
    public string GlslSource { get; }

    /// <summary>Gets the copied official SPIR-V built-in names consumed by this stage.</summary>
    public IReadOnlyList<string> InputBuiltIns { get; }

    /// <summary>Gets the copied official SPIR-V built-in names produced by this stage.</summary>
    public IReadOnlyList<string> OutputBuiltIns { get; }

    /// <summary>Gets the copied active input interface records.</summary>
    public IReadOnlyList<Ps3ShaderInterfaceVariable> Inputs { get; }

    /// <summary>Gets the copied active output interface records.</summary>
    public IReadOnlyList<Ps3ShaderInterfaceVariable> Outputs { get; }

    /// <summary>Gets the copied active uniform-member records.</summary>
    public IReadOnlyList<Ps3ShaderUniformMember> UniformMembers { get; }

    /// <summary>Gets the copied active combined sampler records.</summary>
    public IReadOnlyList<Ps3ShaderSamplerBinding> Samplers { get; }

    /// <summary>Gets the copied active include dependency records.</summary>
    public IReadOnlyList<Ps3ShaderSourceDependency> Dependencies { get; }

    /// <summary>Creates a read-only snapshot of one required collection.</summary>
    /// <typeparam name="T">Collection element type.</typeparam>
    /// <param name="items">Collection to snapshot.</param>
    /// <param name="parameterName">Argument name used in null diagnostics.</param>
    /// <returns>A read-only copied collection.</returns>
    static IReadOnlyList<T> Copy<T>(IEnumerable<T> items, string parameterName) {
        ArgumentNullException.ThrowIfNull(items, parameterName);
        return Array.AsReadOnly(items.ToArray());
    }
}
