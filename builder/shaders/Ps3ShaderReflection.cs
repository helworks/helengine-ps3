using helengine;
using Silk.NET.SPIRV.Cross;

namespace helengine.ps3.builder;

/// <summary>Reads user stage interfaces from SPIR-V and assigns stable GLSL names before emission.</summary>
internal sealed unsafe class Ps3ShaderReflection {
    /// <summary>SPIR-V registry value for the compiler-authored HlslSemanticGOOGLE string decoration.</summary>
    const Silk.NET.SPIRV.Decoration HlslSemanticGoogleDecoration = (Silk.NET.SPIRV.Decoration)5635;

    /// <summary>SPIRV-Cross entry points used to read metadata and assign generated identifiers.</summary>
    readonly Cross Api;

    /// <summary>Context that owns the parsed intermediate representation and reflection handles.</summary>
    readonly Context* Context;

    /// <summary>Compiler whose SPIR-V metadata and emitted names are being processed.</summary>
    readonly Compiler* Compiler;

    /// <summary>Initializes a reflection pass for one live SPIRV-Cross compiler.</summary>
    /// <param name="api">Loaded SPIRV-Cross API.</param>
    /// <param name="context">Owning native compilation context.</param>
    /// <param name="compiler">Compiler created from the parsed SPIR-V module.</param>
    public Ps3ShaderReflection(Cross api, Context* context, Compiler* compiler) {
        Api = api ?? throw new ArgumentNullException(nameof(api));
        if (context == null) throw new ArgumentNullException(nameof(context));
        if (compiler == null) throw new ArgumentNullException(nameof(compiler));
        Context = context;
        Compiler = compiler;
    }

    /// <summary>Gets active user-declared stage inputs after compiler optimization.</summary>
    public IReadOnlyList<Ps3ShaderInterfaceVariable> Inputs { get; private set; } = Array.Empty<Ps3ShaderInterfaceVariable>();

    /// <summary>Gets active user-declared stage outputs after compiler optimization.</summary>
    public IReadOnlyList<Ps3ShaderInterfaceVariable> Outputs { get; private set; } = Array.Empty<Ps3ShaderInterfaceVariable>();

    /// <summary>Gets official SPIR-V built-in names consumed by this stage.</summary>
    public IReadOnlyList<string> InputBuiltIns { get; private set; } = Array.Empty<string>();

    /// <summary>Gets official SPIR-V built-in names produced by this stage.</summary>
    public IReadOnlyList<string> OutputBuiltIns { get; private set; } = Array.Empty<string>();

    /// <summary>Gets active constant-buffer leaf members and their compiler-declared layout.</summary>
    public IReadOnlyList<Ps3ShaderUniformMember> UniformMembers { get; private set; } = Array.Empty<Ps3ShaderUniformMember>();

    /// <summary>Gets active texture and sampler pairs after compiler-driven combined-image construction.</summary>
    public IReadOnlyList<Ps3ShaderSamplerBinding> Samplers { get; private set; } = Array.Empty<Ps3ShaderSamplerBinding>();

    /// <summary>Reflects active user inputs and outputs and assigns their stable GLSL names.</summary>
    public void ReflectInterfaces() {
        Resources* resources = null;
        Check(Api.CompilerCreateShaderResources(Compiler, &resources));
        Inputs = ReadInterface(resources, ResourceType.StageInput, "input");
        Outputs = ReadInterface(resources, ResourceType.StageOutput, "output");
        Api.CompilerUpdateActiveBuiltins(Compiler);
        InputBuiltIns = ReadBuiltIns(Silk.NET.SPIRV.StorageClass.Input);
        OutputBuiltIns = ReadBuiltIns(Silk.NET.SPIRV.StorageClass.Output);
        ValidateUnsupportedResources(resources);
        UniformMembers = ReadUniformMembers(resources);
    }

    /// <summary>Assigns stable names to active combined image samplers and captures their source bindings.</summary>
    /// <remarks>Call only after CompilerBuildCombinedImageSamplers has completed on the same compiler.</remarks>
    public void ReflectSamplers() {
        Resources* resources = null;
        Check(Api.CompilerCreateShaderResources(Compiler, &resources));
        Dictionary<uint, ReflectedResource> images = ReadSamplerResources(resources, ResourceType.SeparateImage, "texture");
        Dictionary<uint, ReflectedResource> samplers = ReadSamplerResources(resources, ResourceType.SeparateSamplers, "sampler");
        CombinedImageSampler* combinedSamplers = null;
        nuint combinedSamplerCount = 0;
        Api.CompilerGetCombinedImageSamplers(Compiler, &combinedSamplers, &combinedSamplerCount);

        List<Ps3ShaderSamplerBinding> reflectedSamplers = new(checked((int)combinedSamplerCount));
        HashSet<string> generatedNames = new(StringComparer.Ordinal);
        for (nuint index = 0; index < combinedSamplerCount; index++) {
            CombinedImageSampler combined = combinedSamplers[index];
            if (!images.TryGetValue(combined.ImageId, out ReflectedResource image)) {
                throw new InvalidOperationException($"SPIRV-Cross combined sampler references unknown texture id {combined.ImageId}.");
            }
            if (!samplers.TryGetValue(combined.SamplerId, out ReflectedResource sampler)) {
                throw new InvalidOperationException($"SPIRV-Cross combined sampler references unknown sampler id {combined.SamplerId}.");
            }

            uint textureBinding = Api.CompilerGetDecoration(Compiler, image.Id, Silk.NET.SPIRV.Decoration.Binding);
            uint samplerBinding = Api.CompilerGetDecoration(Compiler, sampler.Id, Silk.NET.SPIRV.Decoration.Binding);
            string generatedName = $"hel_ps3_sampler_t{textureBinding}_s{samplerBinding}";
            if (!generatedNames.Add(generatedName)) {
                throw new NotSupportedException($"Active PS3 texture/sampler pairs map to duplicate binding identity '{generatedName}'.");
            }

            string textureName = Api.CompilerGetNameS(Compiler, image.Id);
            string samplerName = Api.CompilerGetNameS(Compiler, sampler.Id);
            if (string.IsNullOrWhiteSpace(textureName) || string.IsNullOrWhiteSpace(samplerName)) {
                throw new InvalidOperationException($"Active PS3 combined sampler at texture binding {textureBinding}, sampler binding {samplerBinding} has no compiler-preserved authored resource name.");
            }
            CrossType* imageType = Api.CompilerGetTypeHandle(Compiler, image.TypeId);
            string dimension = Api.TypeGetImageDimension(imageType).ToString();
            if (string.IsNullOrWhiteSpace(dimension)) {
                throw new InvalidOperationException($"Active PS3 texture '{textureName}' has no compiler-reported image dimension.");
            }

            Api.CompilerSetName(Compiler, combined.CombinedId, generatedName);
            reflectedSamplers.Add(new Ps3ShaderSamplerBinding(
                textureName,
                textureBinding,
                samplerName,
                samplerBinding,
                generatedName,
                dimension));
        }

        Samplers = reflectedSamplers.AsReadOnly();
    }

    /// <summary>Reads one SPIRV-Cross resource class and applies stable names through compiler APIs.</summary>
    /// <param name="resources">SPIRV-Cross reflection resource handle.</param>
    /// <param name="resourceType">Stage input or output resource class.</param>
    /// <param name="nameSpace">Generated-name category for this interface direction.</param>
    /// <returns>Immutable reflected interface records.</returns>
    IReadOnlyList<Ps3ShaderInterfaceVariable> ReadInterface(Resources* resources, ResourceType resourceType, string nameSpace) {
        ReflectedResource* resourceList = null;
        nuint resourceCount = 0;
        Check(Api.ResourcesGetResourceListForType(resources, resourceType, ref resourceList, ref resourceCount));

        List<Ps3ShaderInterfaceVariable> variables = new(checked((int)resourceCount));
        HashSet<string> generatedNames = new(StringComparer.Ordinal);
        for (nuint index = 0; index < resourceCount; index++) {
            ReflectedResource resource = resourceList[index];
            if (HasDecoration(resource.Id, Silk.NET.SPIRV.Decoration.BuiltIn)) continue;
            if (!HasDecoration(resource.Id, HlslSemanticGoogleDecoration)) {
                string sourceName = Api.CompilerGetNameS(Compiler, resource.Id);
                throw new InvalidOperationException(
                    $"Active PS3 shader {nameSpace} '{sourceName}' has no compiler-provided HlslSemanticGOOGLE decoration; reflection cannot safely identify its authored HLSL semantic.");
            }
            if (!HasDecoration(resource.Id, Silk.NET.SPIRV.Decoration.Location)) {
                string sourceName = Api.CompilerGetNameS(Compiler, resource.Id);
                throw new InvalidOperationException($"Active PS3 shader {nameSpace} '{sourceName}' has no SPIR-V Location decoration.");
            }

            string semantic = Api.CompilerGetDecorationStringS(Compiler, resource.Id, HlslSemanticGoogleDecoration);
            if (string.IsNullOrWhiteSpace(semantic)) {
                string sourceName = Api.CompilerGetNameS(Compiler, resource.Id);
                throw new InvalidOperationException($"Active PS3 shader {nameSpace} '{sourceName}' has an empty HlslSemanticGOOGLE decoration.");
            }
            uint location = Api.CompilerGetDecoration(Compiler, resource.Id, Silk.NET.SPIRV.Decoration.Location);
            string generatedName = $"hel_ps3_{nameSpace}_location_{location}_{NormalizeSemantic(semantic)}";
            if (!generatedNames.Add(generatedName)) {
                throw new NotSupportedException($"Active PS3 shader {nameSpace} resources produce the same stable name '{generatedName}'.");
            }

            CrossType* type = Api.CompilerGetTypeHandle(Compiler, resource.TypeId);
            uint arrayLength = GetArrayLength(type, semantic);
            string scalarType = $"{Api.TypeGetBasetype(type)}{Api.TypeGetBitWidth(type)}";
            string interpolation = GetInterpolation(resource.Id);
            Api.CompilerSetName(Compiler, resource.Id, generatedName);
            variables.Add(new Ps3ShaderInterfaceVariable(
                semantic,
                location,
                scalarType,
                Api.TypeGetVectorSize(type),
                Api.TypeGetColumns(type),
                arrayLength,
                interpolation,
                generatedName));
        }

        return variables.AsReadOnly();
    }

    /// <summary>Computes the checked length of literal interface array dimensions.</summary>
    /// <param name="type">Reflected interface type handle.</param>
    /// <param name="semantic">Semantic included in unsupported-shape diagnostics.</param>
    /// <returns>Product of array dimensions, or one for a non-array type.</returns>
    uint GetArrayLength(CrossType* type, string semantic) {
        uint dimensionCount = Api.TypeGetNumArrayDimensions(type);
        uint arrayLength = 1;
        for (uint dimensionIndex = 0; dimensionIndex < dimensionCount; dimensionIndex++) {
            if (Api.TypeArrayDimensionIsLiteral(type, dimensionIndex) == 0) {
                throw new NotSupportedException($"PS3 shader interface '{semantic}' has a non-literal array dimension without a tested RSX mapping.");
            }
            uint dimensionLength = Api.TypeGetArrayDimension(type, dimensionIndex);
            if (dimensionLength == 0) {
                throw new NotSupportedException($"PS3 shader interface '{semantic}' has a runtime array without a tested RSX mapping.");
            }
            arrayLength = checked(arrayLength * dimensionLength);
        }
        return arrayLength;
    }

    /// <summary>Reads explicit interpolation decorations without deriving them from variable order.</summary>
    /// <param name="id">SPIR-V variable id.</param>
    /// <returns>Comma-separated interpolation mode and modifiers, including the SPIR-V default when undecorated.</returns>
    string GetInterpolation(uint id) {
        List<string> decorations = [];
        if (HasDecoration(id, Silk.NET.SPIRV.Decoration.Flat)) decorations.Add("Flat");
        if (HasDecoration(id, Silk.NET.SPIRV.Decoration.NoPerspective)) decorations.Add("NoPerspective");
        if (HasDecoration(id, Silk.NET.SPIRV.Decoration.Centroid)) decorations.Add("Centroid");
        if (HasDecoration(id, Silk.NET.SPIRV.Decoration.Sample)) decorations.Add("Sample");
        if (HasDecoration(id, Silk.NET.SPIRV.Decoration.Invariant)) decorations.Add("Invariant");
        return decorations.Count == 0 ? "Smooth" : string.Join(",", decorations);
    }

    /// <summary>Returns active compiler built-ins by SPIR-V storage class, using official enum names.</summary>
    /// <param name="storageClass">Input or output storage class to inspect.</param>
    /// <returns>Names of active SPIR-V built-ins in enum order.</returns>
    IReadOnlyList<string> ReadBuiltIns(Silk.NET.SPIRV.StorageClass storageClass) {
        return Enum.GetValues<Silk.NET.SPIRV.BuiltIn>()
            .Distinct()
            .Where(builtIn => builtIn != Silk.NET.SPIRV.BuiltIn.Max && Api.CompilerHasActiveBuiltin(Compiler, builtIn, storageClass) != 0)
            .Select(builtIn => builtIn.ToString())
            .ToArray();
    }

    /// <summary>Reads active uniform buffers, gives each buffer a unique root name and walks nested members.</summary>
    /// <param name="resources">SPIRV-Cross resources associated with the active entry point.</param>
    /// <returns>All active scalar, vector and matrix leaf members.</returns>
    IReadOnlyList<Ps3ShaderUniformMember> ReadUniformMembers(Resources* resources) {
        ReflectedResource* resourceList = null;
        nuint resourceCount = 0;
        Check(Api.ResourcesGetResourceListForType(resources, ResourceType.UniformBuffer, ref resourceList, ref resourceCount));

        List<Ps3ShaderUniformMember> members = [];
        HashSet<uint> bindings = [];
        Dictionary<uint, Dictionary<uint, string>> memberNamesByType = [];
        Dictionary<uint, string[]> authoredMemberNamesByType = [];
        for (nuint resourceIndex = 0; resourceIndex < resourceCount; resourceIndex++) {
            ReflectedResource resource = resourceList[resourceIndex];
            uint descriptorSet = HasDecoration(resource.Id, Silk.NET.SPIRV.Decoration.DescriptorSet)
                ? Api.CompilerGetDecoration(Compiler, resource.Id, Silk.NET.SPIRV.Decoration.DescriptorSet)
                : 0;
            if (descriptorSet != 0) {
                throw new NotSupportedException($"PS3 uniform buffer '{Api.CompilerGetNameS(Compiler, resource.Id)}' uses unsupported resource space {descriptorSet}.");
            }
            if (!HasDecoration(resource.Id, Silk.NET.SPIRV.Decoration.Binding)) {
                throw new InvalidOperationException($"Active PS3 uniform buffer '{Api.CompilerGetNameS(Compiler, resource.Id)}' has no SPIR-V Binding decoration.");
            }
            uint binding = Api.CompilerGetDecoration(Compiler, resource.Id, Silk.NET.SPIRV.Decoration.Binding);
            if (!bindings.Add(binding)) {
                throw new NotSupportedException($"Active PS3 uniform buffers share binding {binding}, which has no tested RSX mapping.");
            }

            string resourceName = Api.CompilerGetNameS(Compiler, resource.Id);
            string bufferName = string.IsNullOrWhiteSpace(resourceName)
                ? Api.CompilerGetNameS(Compiler, resource.BaseTypeId)
                : resourceName;
            if (string.IsNullOrWhiteSpace(bufferName)) {
                throw new InvalidOperationException($"Active PS3 uniform buffer at binding {binding} has neither a compiler-preserved resource name nor a named reflected base type.");
            }
            string generatedBufferName = $"hel_ps3_uniform_buffer_binding_{binding}";
            Api.CompilerSetName(Compiler, resource.Id, generatedBufferName);
            ReflectUniformStruct(resource.BaseTypeId, bufferName, binding, generatedBufferName, string.Empty, 0, 1, 0,
                memberNamesByType, authoredMemberNamesByType, members);
        }
        foreach (KeyValuePair<uint, Dictionary<uint, string>> typeNames in memberNamesByType) {
            foreach (KeyValuePair<uint, string> memberName in typeNames.Value) {
                Api.CompilerSetMemberName(Compiler, typeNames.Key, memberName.Key, memberName.Value);
            }
        }
        return members.AsReadOnly();
    }

    /// <summary>Rejects active resource categories and descriptor spaces without a tested PS3 mapping.</summary>
    /// <param name="resources">SPIRV-Cross resources associated with the active entry point.</param>
    void ValidateUnsupportedResources(Resources* resources) {
        ReflectedResource* storageImages = null;
        nuint storageImageCount = 0;
        Check(Api.ResourcesGetResourceListForType(resources, ResourceType.StorageImage, ref storageImages, ref storageImageCount));
        if (storageImageCount > 0) {
            ReflectedResource resource = storageImages[0];
            string name = GetResourceName(resource, "storage image");
            throw new NotSupportedException($"Active PS3 storage image '{name}' has no tested RSX mapping.");
        }

        ReflectedResource* storageBuffers = null;
        nuint storageBufferCount = 0;
        Check(Api.ResourcesGetResourceListForType(resources, ResourceType.StorageBuffer, ref storageBuffers, ref storageBufferCount));
        if (storageBufferCount > 0) {
            ReflectedResource resource = storageBuffers[0];
            string name = GetResourceName(resource, "storage buffer");
            EnsureDefaultResourceSpace(resource.Id, "storage buffer", name);
            string reason = ContainsRuntimeArray(resource.BaseTypeId, [])
                ? "contains an unsupported runtime array"
                : "has no tested RSX storage-buffer mapping";
            throw new NotSupportedException($"Active PS3 storage buffer '{name}' {reason}.");
        }
    }

    /// <summary>Walks a compiler-provided type graph and detects literal-free or zero-length array dimensions.</summary>
    /// <param name="typeId">SPIR-V type identifier to inspect.</param>
    /// <param name="visitedTypeIds">Type identifiers already visited to avoid repeated graph traversal.</param>
    /// <returns>True when this type or a nested member contains a runtime array.</returns>
    bool ContainsRuntimeArray(uint typeId, HashSet<uint> visitedTypeIds) {
        if (!visitedTypeIds.Add(typeId)) return false;
        CrossType* type = Api.CompilerGetTypeHandle(Compiler, typeId);
        uint dimensionCount = Api.TypeGetNumArrayDimensions(type);
        for (uint dimensionIndex = 0; dimensionIndex < dimensionCount; dimensionIndex++) {
            if (Api.TypeArrayDimensionIsLiteral(type, dimensionIndex) == 0 || Api.TypeGetArrayDimension(type, dimensionIndex) == 0) return true;
        }
        if (Api.TypeGetBasetype(type).ToString() != "Struct") return false;

        uint memberCount = Api.TypeGetNumMemberTypes(type);
        for (uint memberIndex = 0; memberIndex < memberCount; memberIndex++) {
            uint memberTypeId = Api.TypeGetMemberType(type, memberIndex);
            if (ContainsRuntimeArray(memberTypeId, visitedTypeIds)) return true;
        }
        return false;
    }

    /// <summary>Creates a compiler-resource map keyed by SPIR-V identifier and validates names and bindings.</summary>
    /// <param name="resources">SPIRV-Cross resources associated with the active entry point.</param>
    /// <param name="resourceType">Separate image or sampler resource category.</param>
    /// <param name="category">Resource category used in diagnostics.</param>
    /// <returns>Resources indexed by their SPIR-V ids.</returns>
    Dictionary<uint, ReflectedResource> ReadSamplerResources(Resources* resources, ResourceType resourceType, string category) {
        ReflectedResource* resourceList = null;
        nuint resourceCount = 0;
        Check(Api.ResourcesGetResourceListForType(resources, resourceType, ref resourceList, ref resourceCount));

        Dictionary<uint, ReflectedResource> reflectedResources = [];
        HashSet<uint> bindings = [];
        for (nuint index = 0; index < resourceCount; index++) {
            ReflectedResource resource = resourceList[index];
            string name = Api.CompilerGetNameS(Compiler, resource.Id);
            if (string.IsNullOrWhiteSpace(name)) {
                throw new InvalidOperationException($"Active PS3 {category} at SPIR-V id {resource.Id} has no compiler-preserved authored resource name.");
            }
            EnsureDefaultResourceSpace(resource.Id, category, name);
            if (!HasDecoration(resource.Id, Silk.NET.SPIRV.Decoration.Binding)) {
                throw new InvalidOperationException($"Active PS3 {category} '{name}' has no SPIR-V Binding decoration.");
            }
            uint binding = Api.CompilerGetDecoration(Compiler, resource.Id, Silk.NET.SPIRV.Decoration.Binding);
            if (!bindings.Add(binding)) {
                throw new NotSupportedException($"Active PS3 {category} resources share binding {binding}, which has no tested RSX mapping.");
            }
            reflectedResources.Add(resource.Id, resource);
        }
        return reflectedResources;
    }

    /// <summary>Returns a compiler-preserved resource name, falling back only to its compiler-provided base type name.</summary>
    /// <param name="resource">Reflected resource record.</param>
    /// <param name="category">Resource category included in the error message.</param>
    /// <returns>Compiler-provided resource or base-type identifier.</returns>
    string GetResourceName(ReflectedResource resource, string category) {
        string name = Api.CompilerGetNameS(Compiler, resource.Id);
        if (string.IsNullOrWhiteSpace(name)) name = Api.CompilerGetNameS(Compiler, resource.BaseTypeId);
        if (string.IsNullOrWhiteSpace(name)) {
            throw new InvalidOperationException($"Active PS3 {category} at SPIR-V id {resource.Id} has no compiler-preserved name.");
        }
        return name;
    }

    /// <summary>Rejects descriptor sets other than the PS3-supported default set zero.</summary>
    /// <param name="resourceId">SPIR-V resource identifier.</param>
    /// <param name="category">Resource category used in diagnostics.</param>
    /// <param name="name">Compiler-provided resource name.</param>
    void EnsureDefaultResourceSpace(uint resourceId, string category, string name) {
        if (!HasDecoration(resourceId, Silk.NET.SPIRV.Decoration.DescriptorSet)) return;
        uint descriptorSet = Api.CompilerGetDecoration(Compiler, resourceId, Silk.NET.SPIRV.Decoration.DescriptorSet);
        if (descriptorSet != 0) {
            throw new NotSupportedException($"Active PS3 {category} '{name}' uses unsupported resource space {descriptorSet}.");
        }
    }

    /// <summary>Recursively reflects a structure and preserves each nested member's absolute buffer offset.</summary>
    /// <param name="structureTypeId">SPIR-V type id for the current structure.</param>
    /// <param name="bufferName">Authored constant-buffer name.</param>
    /// <param name="binding">Constant-buffer binding.</param>
    /// <param name="generatedPathPrefix">Complete generated parent path to this structure.</param>
    /// <param name="memberPrefix">Complete parent member path.</param>
    /// <param name="baseOffset">Absolute byte offset of the current nested structure.</param>
    /// <param name="parentArrayLength">Array length inherited from containing structure members.</param>
    /// <param name="parentArrayStride">Array stride inherited from containing structure members.</param>
    /// <param name="memberNamesByType">Stable names to assign once per SPIR-V structure member type.</param>
    /// <param name="authoredMemberNamesByType">Original compiler-provided member names cached before any renaming.</param>
    /// <param name="members">Destination collection for reflected leaf members.</param>
    void ReflectUniformStruct(
        uint structureTypeId,
        string bufferName,
        uint binding,
        string generatedPathPrefix,
        string memberPrefix,
        uint baseOffset,
        uint parentArrayLength,
        uint parentArrayStride,
        Dictionary<uint, Dictionary<uint, string>> memberNamesByType,
        Dictionary<uint, string[]> authoredMemberNamesByType,
        List<Ps3ShaderUniformMember> members) {
        CrossType* structureType = Api.CompilerGetTypeHandle(Compiler, structureTypeId);
        uint memberCount = Api.TypeGetNumMemberTypes(structureType);
        if (!authoredMemberNamesByType.TryGetValue(structureTypeId, out string[] authoredMemberNames)) {
            authoredMemberNames = new string[memberCount];
            for (uint memberIndex = 0; memberIndex < memberCount; memberIndex++) {
                authoredMemberNames[memberIndex] = Api.CompilerGetMemberNameS(Compiler, structureTypeId, memberIndex);
            }
            authoredMemberNamesByType.Add(structureTypeId, authoredMemberNames);
        }
        string structureName = Api.CompilerGetNameS(Compiler, structureTypeId);
        if (string.IsNullOrWhiteSpace(structureName)) {
            throw new InvalidOperationException($"Active PS3 uniform buffer '{bufferName}' contains an unnamed reflected structure.");
        }

        for (uint memberIndex = 0; memberIndex < memberCount; memberIndex++) {
            string memberName = authoredMemberNames[memberIndex];
            if (string.IsNullOrWhiteSpace(memberName)) {
                throw new InvalidOperationException($"Active PS3 uniform buffer '{bufferName}' contains an unnamed member at index {memberIndex}.");
            }
            string memberPath = string.IsNullOrEmpty(memberPrefix) ? memberName : $"{memberPrefix}.{memberName}";
            string generatedMemberName = $"hel_ps3_uniform_type_{NormalizeSemantic(structureName)}_member_{memberIndex}";
            string generatedMemberPath = $"{generatedPathPrefix}.{generatedMemberName}";
            RegisterMemberName(memberNamesByType, structureTypeId, memberIndex, generatedMemberName);
            uint relativeOffset = 0;
            Check(Api.CompilerTypeStructMemberOffset(Compiler, structureType, memberIndex, &relativeOffset));
            uint byteOffset = checked(baseOffset + relativeOffset);
            nuint nativeByteSize = 0;
            Check(Api.CompilerGetDeclaredStructMemberSize(Compiler, structureType, memberIndex, &nativeByteSize));
            uint byteSize = checked((uint)nativeByteSize);

            uint memberTypeId = Api.TypeGetMemberType(structureType, memberIndex);
            CrossType* memberType = Api.CompilerGetTypeHandle(Compiler, memberTypeId);
            uint arrayDimensionCount = Api.TypeGetNumArrayDimensions(memberType);
            if (arrayDimensionCount > 1 || (arrayDimensionCount > 0 && parentArrayStride != 0)) {
                throw new NotSupportedException(
                    $"Active PS3 uniform member '{bufferName}.{memberPath}' requires multiple array strides, which the reflection DTO cannot represent exactly.");
            }
            bool memberIsStruct = Api.TypeGetBasetype(memberType).ToString() == "Struct";
            if (memberIsStruct && (arrayDimensionCount > 0 || parentArrayStride != 0)
                && ContainsArrayInStructureMembers(memberTypeId, [])) {
                throw new NotSupportedException(
                    $"Active PS3 uniform member '{bufferName}.{memberPath}' requires multiple array strides, which the reflection DTO cannot represent exactly.");
            }
            uint arrayLength = GetArrayLength(memberType, memberPath);
            uint fullArrayLength = checked(parentArrayLength * arrayLength);
            uint arrayStride = parentArrayStride;
            if (arrayDimensionCount > 0) {
                Check(Api.CompilerTypeStructMemberArrayStride(Compiler, structureType, memberIndex, &arrayStride));
            }

            if (memberIsStruct) {
                ReflectUniformStruct(memberTypeId, bufferName, binding, generatedMemberPath, memberPath, byteOffset,
                    fullArrayLength, arrayStride, memberNamesByType, authoredMemberNamesByType, members);
                continue;
            }

            uint columns = Api.TypeGetColumns(memberType);
            uint matrixStride = 0;
            bool rowMajor = false;
            if (columns > 1) {
                Check(Api.CompilerTypeStructMemberMatrixStride(Compiler, structureType, memberIndex, &matrixStride));
                rowMajor = Api.CompilerHasMemberDecoration(Compiler, structureTypeId, memberIndex, Silk.NET.SPIRV.Decoration.RowMajor) != 0;
            }

            string scalarType = $"{Api.TypeGetBasetype(memberType)}{Api.TypeGetBitWidth(memberType)}";
            members.Add(new Ps3ShaderUniformMember(
                bufferName,
                binding,
                memberPath,
                byteOffset,
                byteSize,
                arrayDimensionCount > 0 || parentArrayStride != 0 ? arrayStride : 0,
                matrixStride,
                rowMajor,
                generatedMemberPath,
                scalarType,
                Api.TypeGetVectorSize(memberType),
                columns,
                fullArrayLength));
        }
    }

    /// <summary>Records one stable type-scoped member name and rejects conflicting aliases for the same SPIR-V member.</summary>
    /// <param name="namesByType">Names already collected by SPIR-V structure type id.</param>
    /// <param name="structureTypeId">SPIR-V structure type id.</param>
    /// <param name="memberIndex">Member index within that type.</param>
    /// <param name="generatedName">Stable name derived from compiler-preserved type identity and member index.</param>
    void RegisterMemberName(Dictionary<uint, Dictionary<uint, string>> namesByType, uint structureTypeId, uint memberIndex, string generatedName) {
        if (!namesByType.TryGetValue(structureTypeId, out Dictionary<uint, string> typeNames)) {
            typeNames = [];
            namesByType.Add(structureTypeId, typeNames);
        }
        if (typeNames.TryGetValue(memberIndex, out string existingName)) {
            if (!string.Equals(existingName, generatedName, StringComparison.Ordinal)) {
                throw new InvalidOperationException($"SPIR-V structure member {structureTypeId}:{memberIndex} maps to conflicting generated names.");
            }
            return;
        }
        typeNames.Add(memberIndex, generatedName);
    }

    /// <summary>Checks nested structure members for array dimensions without requiring any struct or member names.</summary>
    /// <param name="structureTypeId">SPIR-V type id for the structure or array-of-structure type.</param>
    /// <param name="visitedTypeIds">Structure ids already inspected to avoid repeated graph traversal.</param>
    /// <returns>True when the structure or any nested member declares an array dimension.</returns>
    bool ContainsArrayInStructureMembers(uint structureTypeId, HashSet<uint> visitedTypeIds) {
        if (!visitedTypeIds.Add(structureTypeId)) return false;
        CrossType* structureType = Api.CompilerGetTypeHandle(Compiler, structureTypeId);
        if (Api.TypeGetBasetype(structureType).ToString() != "Struct") return false;

        uint memberCount = Api.TypeGetNumMemberTypes(structureType);
        for (uint memberIndex = 0; memberIndex < memberCount; memberIndex++) {
            uint memberTypeId = Api.TypeGetMemberType(structureType, memberIndex);
            CrossType* memberType = Api.CompilerGetTypeHandle(Compiler, memberTypeId);
            if (Api.TypeGetNumArrayDimensions(memberType) > 0) return true;
            if (Api.TypeGetBasetype(memberType).ToString() == "Struct"
                && ContainsArrayInStructureMembers(memberTypeId, visitedTypeIds)) return true;
        }
        return false;
    }

    /// <summary>Reads a decoration flag from SPIRV-Cross's byte-valued binding.</summary>
    /// <param name="id">SPIR-V object id.</param>
    /// <param name="decoration">Decoration to inspect.</param>
    /// <returns>True when the compiler reports the decoration.</returns>
    bool HasDecoration(uint id, Silk.NET.SPIRV.Decoration decoration) {
        return Api.CompilerHasDecoration(Compiler, id, decoration) != 0;
    }

    /// <summary>Produces an identifier-safe semantic suffix while retaining its authored characters where legal.</summary>
    /// <param name="semantic">Compiler-provided HLSL semantic.</param>
    /// <returns>ASCII letters, digits and underscore only.</returns>
    static string NormalizeSemantic(string semantic) {
        char[] characters = semantic.Select(character => char.IsAsciiLetterOrDigit(character) ? character : '_').ToArray();
        return new string(characters);
    }

    /// <summary>Throws with the compiler-owned diagnostic when a SPIRV-Cross operation fails.</summary>
    /// <param name="result">Native SPIRV-Cross result code.</param>
    void Check(Result result) {
        if (result != Result.Success) {
            throw new InvalidOperationException("PS3 shader reflection failed: " + Api.ContextGetLastErrorStringS(Context));
        }
    }
}
