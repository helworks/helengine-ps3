using helengine;
using System.Globalization;

namespace helengine.ps3.builder;

/// <summary>Reconciles reflected authored stage interfaces with Cg's final RSX routing declarations.</summary>
public sealed class Ps3ShaderBindingMapBuilder {
    /// <summary>Builds validated inter-stage links from source reflection and the final stage dumps.</summary>
    /// <param name="vertexSource">Reflected vertex-stage HLSL translation.</param>
    /// <param name="vertexProgram">Validated vertex-stage SDK program and Cg dump.</param>
    /// <param name="pixelSource">Reflected pixel-stage HLSL translation.</param>
    /// <param name="pixelProgram">Validated pixel-stage SDK program and Cg dump.</param>
    /// <returns>A map with every active pixel input linked to a compatible vertex output.</returns>
    public Ps3ShaderBindingMap Build(Ps3ShaderTranslation vertexSource, Ps3RsxProgram vertexProgram,
        Ps3ShaderTranslation pixelSource, Ps3RsxProgram pixelProgram) {
        ArgumentNullException.ThrowIfNull(vertexSource);
        ArgumentNullException.ThrowIfNull(vertexProgram);
        ArgumentNullException.ThrowIfNull(pixelSource);
        ArgumentNullException.ThrowIfNull(pixelProgram);
        if (vertexSource.Stage != ShaderStage.Vertex || vertexProgram.Stage != ShaderStage.Vertex) {
            throw new InvalidOperationException("The first source translation and RSX program must both be vertex stages.");
        }
        if (pixelSource.Stage != ShaderStage.Pixel || pixelProgram.Stage != ShaderStage.Pixel) {
            throw new InvalidOperationException("The second source translation and RSX program must both be pixel stages.");
        }

        IReadOnlyList<Ps3CgDumpVariable> vertexVariables = Ps3CgDumpReader.Read(ShaderStage.Vertex, vertexProgram.Assembly);
        IReadOnlyList<Ps3CgDumpVariable> pixelVariables = Ps3CgDumpReader.Read(ShaderStage.Pixel, pixelProgram.Assembly);
        ValidateActiveDeclarations(ShaderStage.Vertex, vertexVariables);
        ValidateActiveDeclarations(ShaderStage.Pixel, pixelVariables);
        ValidateVertexPosition(vertexSource, vertexVariables);
        List<Ps3ShaderAttributeBinding> attributes = BuildAttributes(vertexSource, vertexProgram, vertexVariables);
        List<Ps3ShaderUniformBinding> uniforms = BuildUniforms(vertexSource, vertexProgram, vertexVariables);
        uniforms.AddRange(BuildUniforms(pixelSource, pixelProgram, pixelVariables));
        List<Ps3ShaderSamplerMapEntry> samplers = BuildSamplers(vertexSource, vertexProgram, vertexVariables);
        samplers.AddRange(BuildSamplers(pixelSource, pixelProgram, pixelVariables));
        Dictionary<string, Ps3CgDumpVariable> vertexOutputs = ReadVertexOutputs(vertexSource, vertexVariables);
        Dictionary<string, Ps3CgDumpVariable> pixelInputs = ReadPixelInputs(pixelSource, pixelVariables);
        List<Ps3ShaderStageLink> links = new();
        HashSet<string> linkedVertexChannels = new(StringComparer.Ordinal);
        HashSet<string> linkedPixelChannels = new(StringComparer.Ordinal);

        foreach (Ps3ShaderInterfaceVariable pixelInput in pixelSource.Inputs) {
            if (!pixelInputs.TryGetValue(pixelInput.Semantic, out Ps3CgDumpVariable pixelVariable)) continue;
            if (!vertexOutputs.TryGetValue(pixelInput.Semantic, out Ps3CgDumpVariable vertexVariable)) {
                throw new InvalidOperationException($"Pixel input semantic '{pixelInput.Semantic}' has no active vertex-output producer.");
            }
            Ps3ShaderInterfaceVariable vertexOutput = FindInterfaceByGeneratedName(vertexSource.Outputs, vertexVariable.Name);
            VerifyInterfaceShape(pixelInput, vertexOutput);
            string vertexChannel = GetVertexUserChannel(vertexVariable);
            string pixelChannel = GetPixelUserChannel(pixelInputs[pixelInput.Semantic]);
            if (!string.Equals(vertexChannel, pixelChannel, StringComparison.Ordinal)) {
                throw new InvalidOperationException($"Semantic '{pixelInput.Semantic}' routes to vertex channel '{vertexChannel}' and pixel channel '{pixelChannel}'.");
            }
            if (!linkedVertexChannels.Add(vertexChannel) || !linkedPixelChannels.Add(pixelChannel)) {
                throw new InvalidOperationException($"Active stage links overlap RSX channel '{vertexChannel}'.");
            }
            links.Add(new Ps3ShaderStageLink(
                pixelInput.Semantic,
                pixelInput.ScalarType,
                pixelInput.VectorWidth,
                pixelInput.Columns,
                pixelInput.ArrayLength,
                pixelInput.Interpolation,
                vertexChannel,
                pixelChannel,
                GetVertexSlot(vertexChannel),
                GetPixelSlot(pixelChannel)));
        }

        ValidateNonOverlappingBindings(uniforms, attributes, samplers);
        return new Ps3ShaderBindingMap(uniforms, attributes, samplers, links);
    }

    /// <summary>Rejects active declarations that are outside the documented Cg and RSX binding subset.</summary>
    /// <param name="stage">Stage owning the declarations.</param>
    /// <param name="variables">Parsed stage declarations.</param>
    static void ValidateActiveDeclarations(ShaderStage stage, IReadOnlyList<Ps3CgDumpVariable> variables) {
        foreach (Ps3CgDumpVariable variable in variables) {
            if (!variable.IsActive) continue;
            if (Ps3CgDumpReader.ParseDataType(variable.TypeToken) == Ps3RsxDataType.Unknown) {
                throw new InvalidOperationException($"Active Cg parameter '{variable.Name}' uses unsupported type '{variable.TypeToken}'.");
            }
            if (variable.SemanticBinding.StartsWith("$vout.", StringComparison.Ordinal)) {
                if (stage == ShaderStage.Vertex) {
                    _ = GetVertexUserChannel(variable);
                } else {
                    ValidatePixelOutput(variable);
                }
                continue;
            }
            if (variable.SemanticBinding.StartsWith("$vin.", StringComparison.Ordinal)) {
                if (stage == ShaderStage.Vertex) {
                    _ = ParseVertexAttributeSlot(variable);
                } else {
                    _ = GetPixelUserChannel(variable);
                }
                continue;
            }
            if (variable.SemanticBinding.Length == 0 && variable.HardwareBinding.StartsWith("c[", StringComparison.Ordinal)) continue;
            if (variable.SemanticBinding.Length == 0 && variable.HardwareBinding.StartsWith("texunit ", StringComparison.Ordinal)) {
                _ = ParseTextureUnit(variable);
                continue;
            }
            throw new InvalidOperationException($"Active Cg parameter '{variable.Name}' uses unsupported binding '{variable.SemanticBinding}' / '{variable.HardwareBinding}'.");
        }
    }

    /// <summary>Maps every active reflected vertex input to its exact SDK attribute record.</summary>
    /// <param name="source">Reflected vertex translation.</param>
    /// <param name="program">Validated vertex RSX program.</param>
    /// <param name="variables">Parsed vertex Cg declarations.</param>
    /// <returns>Authored input mappings in source-reflection order.</returns>
    static List<Ps3ShaderAttributeBinding> BuildAttributes(Ps3ShaderTranslation source, Ps3RsxProgram program, IReadOnlyList<Ps3CgDumpVariable> variables) {
        List<Ps3ShaderAttributeBinding> result = new();
        foreach (Ps3ShaderInterfaceVariable variable in source.Inputs) {
            if (variable.ArrayLength != 1) {
                throw new NotSupportedException($"Active vertex attribute '{variable.Semantic}' has unsupported array length {variable.ArrayLength}.");
            }
            Ps3CgDumpVariable declaration = FindActiveVariable(variables, variable.GeneratedName, "vertex attribute");
            uint slot = ParseVertexAttributeSlot(declaration);
            Ps3RsxParameter parameter = FindParameter(program, declaration.Name, Ps3RsxParameterCategory.Attribute);
            if (parameter.HardwareIndex != slot) throw new InvalidOperationException($"Vertex input '{variable.Semantic}' Cg slot {slot} disagrees with SDK attribute index {parameter.HardwareIndex}.");
            ValidateParameterType(variable.ScalarType, variable.VectorWidth, variable.Columns, parameter, declaration);
            result.Add(new Ps3ShaderAttributeBinding(variable, parameter));
        }
        foreach (Ps3CgDumpVariable declaration in variables) {
            if (!declaration.IsActive || !declaration.SemanticBinding.StartsWith("$vin.", StringComparison.Ordinal)) continue;
            if (FindInterfaceByGeneratedName(source.Inputs, declaration.Name) == null) {
                throw new InvalidOperationException($"Active vertex input '{declaration.Name}' has no authored source reflection record.");
            }
        }
        return result;
    }

    /// <summary>Maps active Cg constants to their source uniform members without synthesizing optimized-out members.</summary>
    /// <param name="source">Reflected source translation for the stage.</param>
    /// <param name="program">Validated RSX program for the stage.</param>
    /// <param name="variables">Parsed stage Cg declarations.</param>
    /// <returns>Active authored member mappings in dump order.</returns>
    static List<Ps3ShaderUniformBinding> BuildUniforms(Ps3ShaderTranslation source, Ps3RsxProgram program, IReadOnlyList<Ps3CgDumpVariable> variables) {
        List<Ps3ShaderUniformBinding> result = new();
        HashSet<string> mappedNames = new(StringComparer.Ordinal);
        foreach (Ps3CgDumpVariable declaration in variables) {
            if (!declaration.IsActive || declaration.SemanticBinding.Length != 0 || !declaration.HardwareBinding.StartsWith("c[", StringComparison.Ordinal)) continue;
            Ps3ShaderUniformMember member = FindUniformByGeneratedName(source.UniformMembers, declaration.Name);
            if (!mappedNames.Add(declaration.Name)) throw new InvalidOperationException($"Active uniform parameter '{declaration.Name}' is declared more than once.");
            Ps3RsxParameter parameter = FindParameter(program, declaration.Name, Ps3RsxParameterCategory.Constant);
            ValidateParameterType(member.ScalarType, member.VectorWidth, member.Columns, parameter, declaration);
            uint expectedElementCount;
            try {
                expectedElementCount = checked(member.Columns * member.ArrayLength);
            } catch (OverflowException exception) {
                throw new InvalidOperationException($"Uniform '{member.MemberPath}' has a physical element count that overflows the RSX range.", exception);
            }
            if (expectedElementCount == 0 || parameter.ElementCount != expectedElementCount) {
                throw new InvalidOperationException($"Uniform '{member.MemberPath}' requires {expectedElementCount} RSX vectors but parameter '{declaration.Name}' contains {parameter.ElementCount}.");
            }
            result.Add(new Ps3ShaderUniformBinding(source.Stage, member, parameter));
        }
        return result;
    }

    /// <summary>Maps every active combined sampler identity to its distinct SDK texture-unit parameter.</summary>
    /// <param name="source">Reflected source translation for the stage.</param>
    /// <param name="program">Validated RSX program for the stage.</param>
    /// <param name="variables">Parsed stage Cg declarations.</param>
    /// <returns>Active combined-sampler mappings in dump order.</returns>
    static List<Ps3ShaderSamplerMapEntry> BuildSamplers(Ps3ShaderTranslation source, Ps3RsxProgram program, IReadOnlyList<Ps3CgDumpVariable> variables) {
        List<Ps3ShaderSamplerMapEntry> result = new();
        HashSet<string> mappedNames = new(StringComparer.Ordinal);
        foreach (Ps3CgDumpVariable declaration in variables) {
            if (!declaration.IsActive || declaration.SemanticBinding.Length != 0 || !declaration.HardwareBinding.StartsWith("texunit ", StringComparison.Ordinal)) continue;
            Ps3ShaderSamplerBinding sourceSampler = FindSamplerByGeneratedName(source.Samplers, declaration.Name);
            if (!mappedNames.Add(declaration.Name)) throw new InvalidOperationException($"Active sampler parameter '{declaration.Name}' is declared more than once.");
            Ps3RsxParameter parameter = FindParameter(program, declaration.Name, Ps3RsxParameterCategory.Sampler);
            Ps3RsxDataType expectedType = Ps3CgDumpReader.ParseDataType(GetSamplerTypeToken(sourceSampler.Dimension));
            if (expectedType == Ps3RsxDataType.Unknown || declaration.TypeToken != GetSamplerTypeToken(sourceSampler.Dimension)
                || parameter.DataType != expectedType || Ps3CgDumpReader.ParseDataType(declaration.TypeToken) != parameter.DataType) {
                throw new InvalidOperationException($"Sampler '{sourceSampler.TextureName}/{sourceSampler.SamplerName}' disagrees with Cg or SDK sampler type '{declaration.TypeToken}'.");
            }
            uint textureUnit = ParseTextureUnit(declaration);
            if (parameter.HardwareIndex != textureUnit) throw new InvalidOperationException($"Sampler '{declaration.Name}' Cg texture unit {textureUnit} disagrees with SDK unit {parameter.HardwareIndex}.");
            result.Add(new Ps3ShaderSamplerMapEntry(sourceSampler, parameter));
        }
        foreach (Ps3ShaderSamplerBinding sourceSampler in source.Samplers) {
            if (!mappedNames.Contains(sourceSampler.GeneratedName)) {
                throw new InvalidOperationException($"Active authored sampler '{sourceSampler.TextureName}/{sourceSampler.SamplerName}' has no active RSX sampler parameter.");
            }
        }
        return result;
    }

    /// <summary>Rejects hardware-slot and constant-value collisions while accepting identical fragment writes within one element.</summary>
    /// <remarks>The SDK can repeat relocation addresses for repeated source operands; distinct elements still cannot share an address.</remarks>
    /// <param name="uniforms">Mapped active uniform parameters from both stages.</param>
    /// <param name="attributes">Mapped vertex input parameters.</param>
    /// <param name="samplers">Mapped combined sampler parameters.</param>
    static void ValidateNonOverlappingBindings(IReadOnlyList<Ps3ShaderUniformBinding> uniforms,
        IReadOnlyList<Ps3ShaderAttributeBinding> attributes, IReadOnlyList<Ps3ShaderSamplerMapEntry> samplers) {
        HashSet<uint> attributeSlots = new();
        foreach (Ps3ShaderAttributeBinding attribute in attributes) {
            if (!attributeSlots.Add(attribute.HardwareSlot)) throw new InvalidOperationException($"Multiple authored vertex inputs use SDK attribute slot {attribute.HardwareSlot}.");
        }
        HashSet<uint> textureUnits = new();
        foreach (Ps3ShaderSamplerMapEntry sampler in samplers) {
            if (!textureUnits.Add(sampler.TextureUnit)) throw new InvalidOperationException($"Multiple authored sampler identities use RSX texture unit {sampler.TextureUnit}.");
        }
        HashSet<string> constantReferences = new(StringComparer.Ordinal);
        foreach (Ps3ShaderUniformBinding uniform in uniforms) {
            foreach (Ps3RsxConstantElement element in uniform.RsxParameter.Elements) {
                if (uniform.Stage == ShaderStage.Vertex) {
                    if (!element.VertexRegister.HasValue) throw new InvalidOperationException($"Vertex uniform '{uniform.AuthoredMemberPath}' has no SDK constant register.");
                    string reference = "v:" + element.VertexRegister.Value.ToString(CultureInfo.InvariantCulture);
                    if (!constantReferences.Add(reference)) throw new InvalidOperationException($"Active vertex uniforms overlap SDK constant register {element.VertexRegister.Value}.");
                } else {
                    HashSet<uint> elementOffsets = new();
                    foreach (uint offset in element.FragmentRelocationOffsets) {
                        if (!elementOffsets.Add(offset)) continue;
                        string reference = "f:" + offset.ToString(CultureInfo.InvariantCulture);
                        if (!constantReferences.Add(reference)) throw new InvalidOperationException($"Active pixel uniforms overlap fragment relocation offset {offset}.");
                    }
                }
            }
        }
    }

    /// <summary>Finds one active declaration by exact generated name and rejects missing or duplicate active records.</summary>
    /// <param name="variables">Parsed declarations from one stage.</param>
    /// <param name="name">Exact compiler-generated identity.</param>
    /// <param name="role">Resource role for diagnostics.</param>
    /// <returns>The unique active declaration.</returns>
    static Ps3CgDumpVariable FindActiveVariable(IReadOnlyList<Ps3CgDumpVariable> variables, string name, string role) {
        Ps3CgDumpVariable found = null;
        foreach (Ps3CgDumpVariable variable in variables) {
            if (!variable.IsActive || !string.Equals(variable.Name, name, StringComparison.Ordinal)) continue;
            if (found != null) throw new InvalidOperationException($"Active {role} '{name}' has more than one Cg declaration.");
            found = variable;
        }
        return found ?? throw new InvalidOperationException($"Active {role} '{name}' has no Cg declaration.");
    }

    /// <summary>Finds one uniquely named SDK parameter and verifies its expected resource category.</summary>
    /// <param name="program">Validated stage program.</param>
    /// <param name="name">Exact Cg parameter name.</param>
    /// <param name="category">Required SDK parameter category.</param>
    /// <returns>The matching parameter.</returns>
    static Ps3RsxParameter FindParameter(Ps3RsxProgram program, string name, Ps3RsxParameterCategory category) {
        Ps3RsxParameter found = null;
        foreach (Ps3RsxParameter parameter in program.Parameters) {
            if (!string.Equals(parameter.Name, name, StringComparison.Ordinal)) continue;
            if (found != null) throw new InvalidOperationException($"SDK parameter '{name}' appears more than once.");
            found = parameter;
        }
        if (found == null) throw new InvalidOperationException($"Active Cg parameter '{name}' is absent from the SDK parameter tables.");
        if (found.Category != category) throw new InvalidOperationException($"Cg parameter '{name}' requires SDK category '{category}' but found '{found.Category}'.");
        return found;
    }

    /// <summary>Finds one uniform reflection record by its unique compiler-generated full member name.</summary>
    /// <param name="members">Source-reflected uniform members.</param>
    /// <param name="generatedName">Exact Cg parameter name.</param>
    /// <returns>The corresponding authored member.</returns>
    static Ps3ShaderUniformMember FindUniformByGeneratedName(IEnumerable<Ps3ShaderUniformMember> members, string generatedName) {
        Ps3ShaderUniformMember found = null;
        foreach (Ps3ShaderUniformMember member in members) {
            if (!string.Equals(member.GeneratedName, generatedName, StringComparison.Ordinal)) continue;
            if (found != null) throw new InvalidOperationException($"Generated uniform name '{generatedName}' is ambiguous.");
            found = member;
        }
        return found ?? throw new InvalidOperationException($"Active Cg constant '{generatedName}' has no authored uniform reflection record.");
    }

    /// <summary>Finds one combined sampler reflection record by its compiler-generated pair identity.</summary>
    /// <param name="samplers">Source-reflected combined samplers.</param>
    /// <param name="generatedName">Exact Cg parameter name.</param>
    /// <returns>The corresponding authored texture/sampler pair.</returns>
    static Ps3ShaderSamplerBinding FindSamplerByGeneratedName(IEnumerable<Ps3ShaderSamplerBinding> samplers, string generatedName) {
        Ps3ShaderSamplerBinding found = null;
        foreach (Ps3ShaderSamplerBinding sampler in samplers) {
            if (!string.Equals(sampler.GeneratedName, generatedName, StringComparison.Ordinal)) continue;
            if (found != null) throw new InvalidOperationException($"Generated sampler name '{generatedName}' is ambiguous.");
            found = sampler;
        }
        return found ?? throw new InvalidOperationException($"Active Cg sampler '{generatedName}' has no authored combined sampler reflection record.");
    }

    /// <summary>Compares Cg and SDK types against the explicit SPIR-V scalar type and physical matrix shape.</summary>
    /// <param name="scalarType">SPIRV-Cross scalar type text.</param>
    /// <param name="vectorWidth">Physical vector width.</param>
    /// <param name="columns">Physical matrix column count.</param>
    /// <param name="parameter">Final SDK parameter.</param>
    /// <param name="declaration">Final Cg declaration.</param>
    static void ValidateParameterType(string scalarType, uint vectorWidth, uint columns, Ps3RsxParameter parameter, Ps3CgDumpVariable declaration) {
        string expectedToken = GetFloatTypeToken(scalarType, vectorWidth, columns);
        Ps3RsxDataType expectedType = Ps3CgDumpReader.ParseDataType(expectedToken);
        Ps3RsxDataType dumpType = Ps3CgDumpReader.ParseDataType(declaration.TypeToken);
        if (expectedType == Ps3RsxDataType.Unknown || dumpType != expectedType || parameter.DataType != expectedType) {
            throw new InvalidOperationException($"Source metadata, Cg declaration '{declaration.TypeToken}', and SDK parameter '{declaration.Name}' do not agree on type '{expectedToken}'.");
        }
    }

    /// <summary>Translates supported FP3232 source reflection and physical shape into its exact Cg type token.</summary>
    /// <param name="scalarType">SPIRV-Cross scalar base type and bit width.</param>
    /// <param name="vectorWidth">Physical vector width.</param>
    /// <param name="columns">Physical matrix column count.</param>
    /// <returns>The Cg type token expected for this scalar and shape.</returns>
    static string GetFloatTypeToken(string scalarType, uint vectorWidth, uint columns) {
        if (!string.Equals(scalarType, "FP3232", StringComparison.Ordinal)) {
            throw new NotSupportedException($"Active PS3 source scalar type '{scalarType}' has no documented Cg/RSX mapping.");
        }
        if (vectorWidth == 0 || vectorWidth > 4 || columns == 0 || columns > 4) {
            throw new NotSupportedException($"Active PS3 source shape {columns} columns by vector width {vectorWidth} is outside the supported RSX range.");
        }
        if (columns > 1) return "float" + columns.ToString(CultureInfo.InvariantCulture) + "x" + vectorWidth.ToString(CultureInfo.InvariantCulture);
        return vectorWidth == 1 ? "float" : "float" + vectorWidth.ToString(CultureInfo.InvariantCulture);
    }

    /// <summary>Maps the reflected SPIRV-Cross image dimension to the pinned Cg sampler spelling.</summary>
    /// <param name="dimension">SPIRV-Cross dimension text.</param>
    /// <returns>The matching Cg sampler type token.</returns>
    static string GetSamplerTypeToken(string dimension) {
        return dimension switch {
            "Dim1D" => "sampler1D",
            "Dim2D" => "sampler2D",
            "Dim3D" => "sampler3D",
            "DimCube" or "Cube" => "samplerCUBE",
            "DimRect" or "Rect" => "samplerRECT",
            _ => throw new NotSupportedException($"Active PS3 sampler dimension '{dimension}' has no documented Cg mapping.")
        };
    }

    /// <summary>Validates the Cg $vin.ATTRn route and returns its numeric SDK attribute index.</summary>
    /// <param name="variable">Active vertex input declaration.</param>
    /// <returns>The parsed input attribute slot.</returns>
    static uint ParseVertexAttributeSlot(Ps3CgDumpVariable variable) {
        const string prefix = "$vin.ATTR";
        if (!variable.SemanticBinding.StartsWith(prefix, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"Active vertex input '{variable.Name}' has unsupported Cg binding '{variable.SemanticBinding}'.");
        }
        string suffix = variable.SemanticBinding[prefix.Length..];
        if (!uint.TryParse(suffix, NumberStyles.None, CultureInfo.InvariantCulture, out uint slot) || slot > 15) {
            throw new InvalidOperationException($"Active vertex input '{variable.Name}' has unsupported attribute slot '{variable.SemanticBinding}'.");
        }
        string expectedHardware = "ATTR" + slot.ToString(CultureInfo.InvariantCulture);
        if (!string.Equals(variable.HardwareBinding, expectedHardware, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"Active vertex input '{variable.Name}' has inconsistent semantic and hardware bindings.");
        }
        return slot;
    }

    /// <summary>Validates and parses the exact texunit n spelling of one active sampler declaration.</summary>
    /// <param name="variable">Active sampler declaration.</param>
    /// <returns>The SDK texture unit.</returns>
    static uint ParseTextureUnit(Ps3CgDumpVariable variable) {
        const string prefix = "texunit ";
        if (!variable.HardwareBinding.StartsWith(prefix, StringComparison.Ordinal)
            || !uint.TryParse(variable.HardwareBinding[prefix.Length..], NumberStyles.None, CultureInfo.InvariantCulture, out uint unit)) {
            throw new InvalidOperationException($"Active sampler '{variable.Name}' has unsupported hardware binding '{variable.HardwareBinding}'.");
        }
        return unit;
    }

    /// <summary>Validates the fixed-function fragment color output declarations supported by the Cg frontend.</summary>
    /// <param name="variable">Active fragment output declaration.</param>
    static void ValidatePixelOutput(Ps3CgDumpVariable variable) {
        if (variable.SemanticBinding == "$vout.COL" && variable.HardwareBinding == "COL") return;
        if ((variable.SemanticBinding == "$vout.COL0" || variable.SemanticBinding == "$vout.COL1")
            && variable.HardwareBinding == variable.SemanticBinding[6..]) return;
        throw new InvalidOperationException($"Active pixel output '{variable.Name}' uses unsupported Cg output route '{variable.SemanticBinding}' / '{variable.HardwareBinding}'.");
    }

    /// <summary>Requires the reflected Position built-in and its active Cg HPOS output route.</summary>
    /// <param name="vertexSource">Vertex-stage source reflection.</param>
    /// <param name="variables">Parsed vertex Cg declarations.</param>
    static void ValidateVertexPosition(Ps3ShaderTranslation vertexSource, IReadOnlyList<Ps3CgDumpVariable> variables) {
        if (!vertexSource.OutputBuiltIns.Contains("Position", StringComparer.Ordinal)) {
            throw new InvalidOperationException("Vertex translation does not declare the required Position built-in output.");
        }
        int matchingPositionCount = 0;
        foreach (Ps3CgDumpVariable variable in variables) {
            if (!variable.IsActive) continue;
            if (string.Equals(variable.Name, "gl_Position", StringComparison.Ordinal)) {
                if (!string.Equals(variable.SemanticBinding, "$vout.POSITION", StringComparison.Ordinal)
                    || !string.Equals(variable.HardwareBinding, "HPOS", StringComparison.Ordinal)) {
                    throw new InvalidOperationException("Active gl_Position does not route through the Cg POSITION/HPOS output channel.");
                }
                matchingPositionCount++;
            } else if (string.Equals(variable.SemanticBinding, "$vout.POSITION", StringComparison.Ordinal)
                || string.Equals(variable.SemanticBinding, "$vout.HPOS", StringComparison.Ordinal)) {
                throw new InvalidOperationException($"Active vertex position route is declared by unexpected parameter '{variable.Name}'.");
            }
        }
        if (matchingPositionCount != 1) throw new InvalidOperationException("Vertex Cg dump must contain one active gl_Position routed to HPOS.");
    }

    /// <summary>Matches active Cg user outputs to reflected authored vertex outputs.</summary>
    /// <param name="source">Reflected vertex translation.</param>
    /// <param name="variables">Parsed vertex declarations.</param>
    /// <returns>Active user outputs keyed by authored semantic.</returns>
    static Dictionary<string, Ps3CgDumpVariable> ReadVertexOutputs(Ps3ShaderTranslation source, IReadOnlyList<Ps3CgDumpVariable> variables) {
        Dictionary<string, Ps3ShaderInterfaceVariable> reflected = IndexInterfaces(source.Outputs, "vertex output");
        Dictionary<string, Ps3CgDumpVariable> result = new(StringComparer.Ordinal);
        foreach (Ps3CgDumpVariable variable in variables) {
            if (!variable.IsActive || !variable.SemanticBinding.StartsWith("$vout.", StringComparison.Ordinal)) continue;
            if (variable.Name == "gl_Position") continue;
            if (variable.SemanticBinding == "$vout.COL" && variable.HardwareBinding == "COL") continue;
            string channel = GetVertexUserChannel(variable);
            if (channel == "TEMP") continue;
            Ps3ShaderInterfaceVariable authored = FindInterfaceByGeneratedName(source.Outputs, variable.Name);
            if (!reflected.ContainsKey(authored.Semantic)) throw new InvalidOperationException($"Active Cg output '{variable.Name}' has no matching reflected semantic.");
            ValidateInterfaceDeclaration(authored, variable);
            if (!result.TryAdd(authored.Semantic, variable)) throw new InvalidOperationException($"Vertex semantic '{authored.Semantic}' has multiple active Cg output routes.");
        }
        return result;
    }

    /// <summary>Matches active Cg pixel inputs to reflected authored pixel inputs.</summary>
    /// <param name="source">Reflected pixel translation.</param>
    /// <param name="variables">Parsed pixel declarations.</param>
    /// <returns>Active pixel inputs keyed by authored semantic.</returns>
    static Dictionary<string, Ps3CgDumpVariable> ReadPixelInputs(Ps3ShaderTranslation source, IReadOnlyList<Ps3CgDumpVariable> variables) {
        Dictionary<string, Ps3ShaderInterfaceVariable> reflected = IndexInterfaces(source.Inputs, "pixel input");
        Dictionary<string, Ps3CgDumpVariable> result = new(StringComparer.Ordinal);
        foreach (Ps3CgDumpVariable variable in variables) {
            if (!variable.IsActive || !variable.SemanticBinding.StartsWith("$vin.", StringComparison.Ordinal)) continue;
            string channel = GetPixelUserChannel(variable);
            if (channel == "WPOS") {
                if (!source.InputBuiltIns.Contains("FragCoord", StringComparer.Ordinal)) {
                    throw new InvalidOperationException("Active Cg WPOS input has no matching reflected FragCoord built-in.");
                }
                continue;
            }
            Ps3ShaderInterfaceVariable authored = FindInterfaceByGeneratedName(source.Inputs, variable.Name);
            if (!reflected.ContainsKey(authored.Semantic)) throw new InvalidOperationException($"Active Cg input '{variable.Name}' has no matching reflected semantic.");
            ValidateInterfaceDeclaration(authored, variable);
            if (!result.TryAdd(authored.Semantic, variable)) throw new InvalidOperationException($"Pixel semantic '{authored.Semantic}' has multiple active Cg input routes.");
        }
        return result;
    }

    /// <summary>Indexes authored interfaces and rejects repeated semantics that make Cg reconciliation ambiguous.</summary>
    /// <param name="variables">Reflected interface records.</param>
    /// <param name="role">Human-readable stage role for diagnostics.</param>
    /// <returns>Interface records keyed by exact authored semantic.</returns>
    static Dictionary<string, Ps3ShaderInterfaceVariable> IndexInterfaces(IEnumerable<Ps3ShaderInterfaceVariable> variables, string role) {
        Dictionary<string, Ps3ShaderInterfaceVariable> result = new(StringComparer.Ordinal);
        foreach (Ps3ShaderInterfaceVariable variable in variables) {
            if (!result.TryAdd(variable.Semantic, variable)) throw new InvalidOperationException($"{role} semantic '{variable.Semantic}' is ambiguous.");
        }
        return result;
    }

    /// <summary>Resolves a generated compiler name to exactly one authored interface record.</summary>
    /// <param name="variables">Reflected interface records.</param>
    /// <param name="generatedName">Exact generated GLSL/Cg identifier.</param>
    /// <returns>The matching reflected interface variable.</returns>
    static Ps3ShaderInterfaceVariable FindInterfaceByGeneratedName(IEnumerable<Ps3ShaderInterfaceVariable> variables, string generatedName) {
        Ps3ShaderInterfaceVariable found = null;
        foreach (Ps3ShaderInterfaceVariable variable in variables) {
            if (!string.Equals(variable.GeneratedName, generatedName, StringComparison.Ordinal)) continue;
            if (found != null) throw new InvalidOperationException($"Generated interface name '{generatedName}' is ambiguous.");
            found = variable;
        }
        return found ?? throw new InvalidOperationException($"Cg interface '{generatedName}' has no matching source reflection record.");
    }

    /// <summary>Checks that an active inter-stage source value is a supported scalar/vector and matches its Cg token.</summary>
    /// <param name="reflected">Source compiler reflection for the interface value.</param>
    /// <param name="declaration">The active Cg dump declaration for that value.</param>
    static void ValidateInterfaceDeclaration(Ps3ShaderInterfaceVariable reflected, Ps3CgDumpVariable declaration) {
        if (reflected.Columns != 1 || reflected.ArrayLength != 1) {
            throw new NotSupportedException($"Active inter-stage semantic '{reflected.Semantic}' uses unsupported matrix or array shape {reflected.Columns}x{reflected.VectorWidth}[{reflected.ArrayLength}].");
        }
        string expectedToken = GetFloatTypeToken(reflected.ScalarType, reflected.VectorWidth, reflected.Columns);
        Ps3RsxDataType expectedType = Ps3CgDumpReader.ParseDataType(expectedToken);
        Ps3RsxDataType declarationType = Ps3CgDumpReader.ParseDataType(declaration.TypeToken);
        if (expectedType == Ps3RsxDataType.Unknown || declarationType != expectedType) {
            throw new InvalidOperationException($"Active inter-stage semantic '{reflected.Semantic}' has source type '{expectedToken}' but Cg declares '{declaration.TypeToken}'.");
        }
    }

    /// <summary>Requires semantic, scalar type, physical shape and interpolation to agree between stages.</summary>
    /// <param name="pixelInput">Reflected pixel input.</param>
    /// <param name="vertexOutput">Matching reflected vertex output.</param>
    static void VerifyInterfaceShape(Ps3ShaderInterfaceVariable pixelInput, Ps3ShaderInterfaceVariable vertexOutput) {
        if (!string.Equals(pixelInput.Semantic, vertexOutput.Semantic, StringComparison.Ordinal)
            || !string.Equals(pixelInput.ScalarType, vertexOutput.ScalarType, StringComparison.Ordinal)
            || pixelInput.VectorWidth != vertexOutput.VectorWidth
            || pixelInput.Columns != vertexOutput.Columns
            || pixelInput.ArrayLength != vertexOutput.ArrayLength
            || !string.Equals(pixelInput.Interpolation, vertexOutput.Interpolation, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"Vertex output and pixel input for semantic '{pixelInput.Semantic}' disagree in semantic, type, shape, or interpolation.");
        }
    }

    /// <summary>Validates and returns a vertex output's canonical user varying channel.</summary>
    /// <param name="variable">Active vertex Cg output declaration.</param>
    /// <returns>Canonical output channel name.</returns>
    static string GetVertexUserChannel(Ps3CgDumpVariable variable) {
        if (!variable.SemanticBinding.StartsWith("$vout.", StringComparison.Ordinal)) throw new InvalidOperationException($"Active vertex variable '{variable.Name}' has unsupported binding '{variable.SemanticBinding}'.");
        string semantic = variable.SemanticBinding[6..];
        string canonical = semantic switch {
            "POSITION" or "HPOS" => "POSITION",
            "COL0" => "COL0",
            "COL1" => "COL1",
            "BFC0" => "BFC0",
            "BFC1" => "BFC1",
            "FOGC" => "FOGC",
            "PSIZ" or "PSZ" => "PSIZ",
            "TEMP" => "TEMP",
            _ when IsNumberedChannel(semantic, "TEX", 7) => semantic,
            _ => throw new InvalidOperationException($"Active vertex output '{variable.Name}' uses unsupported Cg route '{variable.SemanticBinding}'.")
        };
        string expectedHardware = canonical switch {
            "POSITION" => "HPOS",
            "PSIZ" => "PSZ",
            _ => canonical
        };
        if (!string.Equals(variable.HardwareBinding, expectedHardware, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"Active vertex output '{variable.Name}' has inconsistent route '{variable.SemanticBinding}' and hardware binding '{variable.HardwareBinding}'.");
        }
        return canonical;
    }

    /// <summary>Validates and returns a pixel input's canonical user varying channel.</summary>
    /// <param name="variable">Active pixel Cg input declaration.</param>
    /// <returns>Canonical input channel name.</returns>
    static string GetPixelUserChannel(Ps3CgDumpVariable variable) {
        if (!variable.SemanticBinding.StartsWith("$vin.", StringComparison.Ordinal)) throw new InvalidOperationException($"Active pixel variable '{variable.Name}' has unsupported binding '{variable.SemanticBinding}'.");
        string semantic = variable.SemanticBinding[5..];
        string canonical = semantic switch {
            "WPOS" => "WPOS",
            "COL0" or "COLOR0" => "COL0",
            "COL1" or "COLOR1" => "COL1",
            "FOGC" => "FOGC",
            _ when IsNumberedChannel(semantic, "TEX", 7) => semantic,
            _ => throw new InvalidOperationException($"Active pixel input '{variable.Name}' uses unsupported Cg route '{variable.SemanticBinding}'.")
        };
        string expectedHardware = semantic.StartsWith("COLOR", StringComparison.Ordinal) ? canonical : semantic;
        if (!string.Equals(variable.HardwareBinding, expectedHardware, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"Active pixel input '{variable.Name}' has inconsistent route '{variable.SemanticBinding}' and hardware binding '{variable.HardwareBinding}'.");
        }
        return canonical;
    }

    /// <summary>Maps a supported canonical vertex output to the SDK's stage-local numeric slot.</summary>
    /// <param name="channel">Canonical Cg/RSX channel name.</param>
    /// <returns>Vertex output slot index.</returns>
    static uint GetVertexSlot(string channel) {
        return channel switch {
            "POSITION" => 0,
            "COL0" => 1,
            "COL1" => 2,
            "BFC0" => 3,
            "BFC1" => 4,
            "FOGC" => 5,
            "PSIZ" => 6,
            _ when IsNumberedChannel(channel, "TEX", 7) => 7u + ParseChannelIndex(channel, "TEX"),
            _ => throw new InvalidOperationException($"Channel '{channel}' has no supported vertex output slot.")
        };
    }

    /// <summary>Maps a supported canonical pixel input to the SDK's stage-local numeric slot.</summary>
    /// <param name="channel">Canonical Cg/RSX channel name.</param>
    /// <returns>Pixel input slot index.</returns>
    static uint GetPixelSlot(string channel) {
        return channel switch {
            "WPOS" => 0,
            "COL0" => 1,
            "COL1" => 2,
            "FOGC" => 3,
            _ when IsNumberedChannel(channel, "TEX", 7) => 4u + ParseChannelIndex(channel, "TEX"),
            _ => throw new InvalidOperationException($"Channel '{channel}' has no supported pixel input slot.")
        };
    }

    /// <summary>Checks a pinned named channel's prefix and supported decimal range.</summary>
    /// <param name="channel">Cg channel spelling.</param>
    /// <param name="prefix">Required channel prefix.</param>
    /// <param name="maximumIndex">Largest supported zero-based channel index.</param>
    /// <returns>True only for a fully numeric supported channel.</returns>
    static bool IsNumberedChannel(string channel, string prefix, uint maximumIndex) {
        if (!channel.StartsWith(prefix, StringComparison.Ordinal)) return false;
        string indexText = channel[prefix.Length..];
        return uint.TryParse(indexText, NumberStyles.None, CultureInfo.InvariantCulture, out uint index) && index <= maximumIndex;
    }

    /// <summary>Parses the already-validated numeric suffix of a canonical channel name.</summary>
    /// <param name="channel">Cg channel spelling.</param>
    /// <param name="prefix">Channel prefix to remove.</param>
    /// <returns>Zero-based channel index.</returns>
    static uint ParseChannelIndex(string channel, string prefix) {
        return uint.Parse(channel[prefix.Length..], NumberStyles.None, CultureInfo.InvariantCulture);
    }
}
