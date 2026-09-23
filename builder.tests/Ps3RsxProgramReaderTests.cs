using helengine;
using helengine.ps3.builder;
using System.Buffers.Binary;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies strict parsing of pinned SDK RSX program headers and parameter records.</summary>
public sealed class Ps3RsxProgramReaderTests {
    /// <summary>The byte size of one SDK attribute record.</summary>
    const int AttributeRecordSize = 12;

    /// <summary>The byte size of one SDK constant record.</summary>
    const int ConstantRecordSize = 28;

    /// <summary>The byte size of one SDK program header.</summary>
    const int HeaderSize = 36;

    /// <summary>A valid compiler dump with shared vertex attributes and a register-bound constant.</summary>
    const string VertexAssembly = "#profile vp40\n#var float3 POSITION : $vin.ATTR0 : ATTR0 : -1 : 1\n#var float4 tint :  : c[2] : -1 : 1\n#var float4 inactive :  : c[8] : -1 : 0\n";

    /// <summary>A valid compiler dump with a fragment constant but no fabricated constant register.</summary>
    const string FragmentAssembly = "#profile fp40\n#var float4 tint :  : c[0] : -1 : 1\n";

    /// <summary>A valid vertex fixture exposes attributes and one named vector constant.</summary>
    [Fact]
    public void ReadsVertexAttributesAndConstantRegisters() {
        byte[] bytes = CreateVertexProgram();

        Ps3RsxProgram program = Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "sdk-id");

        Ps3RsxParameter attribute = Assert.Single(program.Parameters, parameter => parameter.Name == "POSITION");
        Ps3RsxParameter constant = Assert.Single(program.Parameters, parameter => parameter.Name == "tint");
        Assert.Equal(ShaderStage.Vertex, program.Stage);
        Assert.Equal(Ps3RsxParameterCategory.Attribute, attribute.Category);
        Assert.Equal(0u, attribute.HardwareIndex);
        Assert.Equal(Ps3RsxDataType.Float3, attribute.DataType);
        Assert.Equal(Ps3RsxParameterCategory.Constant, constant.Category);
        Assert.Null(constant.HardwareIndex);
        Assert.Equal(1u, constant.ElementCount);
        Assert.Equal(2u, Assert.Single(constant.Elements).VertexRegister);
        Assert.Equal(new uint[] { 0x3F800000, 0x40000000, 0x40400000, 0x40800000 }, Assert.Single(constant.Elements).InternalValues);
        Assert.DoesNotContain(program.Parameters, parameter => parameter.Name == "inactive");
    }

    /// <summary>Groups a matrix's unnamed continuation rows beneath its named first constant record.</summary>
    [Fact]
    public void PreservesMatrixConstantElementBoundaries() {
        byte[] bytes = CreateVertexMatrixProgram();
        string assembly = "#profile vp40\n#var float4x4 transform :  : c[0], 4 : -1 : 1\n";

        Ps3RsxProgram program = Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, assembly, "sdk-id");

        Ps3RsxParameter matrix = Assert.Single(program.Parameters);
        Assert.Equal("transform", matrix.Name);
        Assert.Equal(4u, matrix.ElementCount);
        Assert.Equal(new uint?[] { 0, 1, 2, 3 }, matrix.Elements.Select(element => element.VertexRegister));
        Assert.Equal(new uint[] { 0, 1, 2, 3 }, matrix.Elements.Select(element => element.ElementOrdinal));
    }

    /// <summary>Program construction copies the SDK bytes and retains the compiler dump and toolchain identity.</summary>
    [Fact]
    public void PreservesCopiedCompilerOutput() {
        byte[] bytes = CreateVertexProgram();
        byte[] expected = (byte[])bytes.Clone();
        Ps3RsxProgram program = Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "pinned-sdk");
        bytes[0] = 0;

        Assert.Equal(expected, program.Bytecode);
        Assert.Equal(VertexAssembly, program.Assembly);
        Assert.Equal("pinned-sdk", program.ToolchainIdentity);
    }

    /// <summary>Rejects a program whose stage magic is not a pinned SDK vertex or fragment signature.</summary>
    [Fact]
    public void RejectsWrongMagic() {
        byte[] bytes = CreateVertexProgram();
        bytes[0] = 0;

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "sdk"));
    }

    /// <summary>Rejects an SDK program when the requested shader stage differs from its encoded stage.</summary>
    [Fact]
    public void RejectsStageMismatch() {
        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Pixel, CreateVertexProgram(), VertexAssembly, "sdk"));
    }

    /// <summary>Rejects headers shorter than the pinned SDK's complete 36-byte common header.</summary>
    [Fact]
    public void RejectsTruncatedHeader() {
        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, new byte[HeaderSize - 1], VertexAssembly, "sdk"));
    }

    /// <summary>Rejects overflowing offset arithmetic before reading an attribute table.</summary>
    [Fact]
    public void RejectsOverflowingTableRange() {
        byte[] bytes = CreateVertexProgram();
        WriteUInt32(bytes, 12, uint.MaxValue - 3);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "sdk"));
    }

    /// <summary>Rejects a name offset that does not point to a terminated string within the program.</summary>
    [Fact]
    public void RejectsOutOfBoundsName() {
        byte[] bytes = CreateVertexProgram();
        WriteUInt32(bytes, HeaderSize, uint.MaxValue - 1);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "sdk"));
    }

    /// <summary>Rejects instruction bytes extending beyond the SDK program file.</summary>
    [Fact]
    public void RejectsOutOfBoundsMicrocode() {
        byte[] bytes = CreateVertexProgram();
        WriteUInt32(bytes, 20, 0xFFFFFFF0);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "sdk"));
    }

    /// <summary>Rejects a microcode start that violates the SDK's 16-byte instruction alignment.</summary>
    [Fact]
    public void RejectsInvalidMicrocodeAlignment() {
        byte[] bytes = CreateVertexProgram();
        WriteUInt32(bytes, 20, 81);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "sdk"));
    }

    /// <summary>Rejects SDK headers that declare no instructions instead of returning an empty program.</summary>
    [Fact]
    public void RejectsEmptyMicrocode() {
        byte[] bytes = CreateVertexProgram();
        WriteUInt16(bytes, 10, 0);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "sdk"));
    }

    /// <summary>A fragment relocation record is resolved as a microcode offset rather than a hardware register.</summary>
    [Fact]
    public void ReadsFragmentRelocationsWithoutInventingRegisterIndices() {
        byte[] bytes = CreateFragmentProgramWithRelocation();

        Ps3RsxProgram program = Ps3RsxProgramReader.Read(ShaderStage.Pixel, bytes, FragmentAssembly, "sdk-id");

        Ps3RsxParameter constant = Assert.Single(program.Parameters, parameter => parameter.Category == Ps3RsxParameterCategory.Constant);
        Ps3RsxConstantElement element = Assert.Single(constant.Elements);
        Assert.Null(constant.HardwareIndex);
        Assert.Null(element.VertexRegister);
        Assert.Equal(new uint[] { 0 }, element.FragmentRelocationOffsets);
    }

    /// <summary>An empty fragment relocation list remains empty instead of becoming register zero.</summary>
    [Fact]
    public void PreservesFragmentConstantWithoutRelocations() {
        byte[] bytes = CreateFragmentProgramWithoutRelocation();

        Ps3RsxProgram program = Ps3RsxProgramReader.Read(ShaderStage.Pixel, bytes, FragmentAssembly, "sdk-id");

        Ps3RsxConstantElement element = Assert.Single(Assert.Single(program.Parameters).Elements);
        Assert.Empty(element.FragmentRelocationOffsets);
        Assert.Null(element.VertexRegister);
    }

    /// <summary>Accepts Cg's distinct COLOR0 and COL0 spellings while preserving the fragment attribute's raw SDK index.</summary>
    [Fact]
    public void ReadsFragmentColorInputAlias() {
        byte[] bytes = CreateFragmentColorInputProgram();
        string assembly = "#profile fp40\n#var float4 gl_Color : $vin.COLOR0 : COL0 : -1 : 1\n";

        Ps3RsxProgram program = Ps3RsxProgramReader.Read(ShaderStage.Pixel, bytes, assembly, "sdk-id");

        Ps3RsxParameter color = Assert.Single(program.Parameters);
        Assert.Equal("gl_Color", color.Name);
        Assert.Equal(Ps3RsxParameterCategory.Attribute, color.Category);
        Assert.Equal(0u, color.HardwareIndex);
        Assert.Equal(Ps3RsxDataType.Float4, color.DataType);
    }

    /// <summary>Rejects relocation references that fall outside the fragment microcode region.</summary>
    [Fact]
    public void RejectsOutOfBoundsFragmentRelocation() {
        byte[] bytes = CreateFragmentProgramWithRelocation();
        WriteUInt32(bytes, 68, 32);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Pixel, bytes, FragmentAssembly, "sdk"));
    }

    /// <summary>Rejects compiler dump input whose parameter name disagrees with the SDK binary name.</summary>
    [Fact]
    public void RejectsDumpNameMismatch() {
        string assembly = VertexAssembly.Replace("POSITION", "NORMAL", StringComparison.Ordinal);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, CreateVertexProgram(), assembly, "sdk"));
    }

    /// <summary>Rejects compiler dump input whose attribute slot disagrees with the SDK hardware index.</summary>
    [Fact]
    public void RejectsDumpAttributeSlotMismatch() {
        string assembly = VertexAssembly.Replace("ATTR0", "ATTR1", StringComparison.Ordinal);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, CreateVertexProgram(), assembly, "sdk"));
    }

    /// <summary>Rejects compiler dump input whose parameter type disagrees with the SDK type code.</summary>
    [Fact]
    public void RejectsDumpTypeMismatch() {
        string assembly = VertexAssembly.Replace("float3 POSITION", "float4 POSITION", StringComparison.Ordinal);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, CreateVertexProgram(), assembly, "sdk"));
    }

    /// <summary>Rejects a stage profile that conflicts with the stage encoded in the SDK binary.</summary>
    [Fact]
    public void RejectsAssemblyProfileMismatch() {
        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, CreateVertexProgram(), FragmentAssembly, "sdk"));
    }

    /// <summary>Rejects profile-only text instead of treating an arbitrary empty dump as compiler evidence.</summary>
    [Fact]
    public void RejectsDumpWithoutVariableDeclarations() {
        string assembly = "#profile vp40\n";

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, CreateEmptyVertexProgram(), assembly, "sdk"));
    }

    /// <summary>Creates a vertex program fixture with exact SDK header, table and name fields.</summary>
    /// <returns>A valid one-instruction vertex program byte sequence.</returns>
    static byte[] CreateVertexProgram() {
        byte[] bytes = new byte[116];
        WriteUInt16(bytes, 0, 0x5650);
        WriteUInt16(bytes, 4, 1);
        WriteUInt16(bytes, 6, 1);
        WriteUInt16(bytes, 8, 1);
        WriteUInt16(bytes, 10, 1);
        WriteUInt32(bytes, 12, HeaderSize);
        WriteUInt32(bytes, 16, HeaderSize + AttributeRecordSize);
        WriteUInt32(bytes, 20, 80);
        WriteUInt32(bytes, 24, 1);
        WriteUInt32(bytes, 28, 0);
        WriteUInt16(bytes, 32, 0);
        WriteUInt16(bytes, 34, 0);
        WriteUInt32(bytes, HeaderSize, 96);
        WriteUInt32(bytes, HeaderSize + 4, 0);
        bytes[HeaderSize + 8] = 8;
        int constantOffset = HeaderSize + AttributeRecordSize;
        WriteUInt32(bytes, constantOffset, 106);
        WriteUInt32(bytes, constantOffset + 4, 2);
        bytes[constantOffset + 8] = 9;
        bytes[constantOffset + 10] = 1;
        WriteUInt32(bytes, constantOffset + 12, 0x3F800000);
        WriteUInt32(bytes, constantOffset + 16, 0x40000000);
        WriteUInt32(bytes, constantOffset + 20, 0x40400000);
        WriteUInt32(bytes, constantOffset + 24, 0x40800000);
        WriteName(bytes, 96, "POSITION");
        WriteName(bytes, 106, "tint");
        return bytes;
    }

    /// <summary>Creates one FLOAT4x4 parameter with four physical vertex constant records.</summary>
    /// <returns>A valid one-instruction vertex program with three unnamed continuation rows.</returns>
    static byte[] CreateVertexMatrixProgram() {
        byte[] bytes = new byte[186];
        WriteUInt16(bytes, 0, 0x5650);
        WriteUInt16(bytes, 4, 4);
        WriteUInt16(bytes, 6, 0);
        WriteUInt16(bytes, 8, 4);
        WriteUInt16(bytes, 10, 1);
        WriteUInt32(bytes, 12, HeaderSize);
        WriteUInt32(bytes, 16, HeaderSize);
        WriteUInt32(bytes, 20, 160);
        for (int element = 0; element < 4; element++) {
            int recordOffset = HeaderSize + element * ConstantRecordSize;
            if (element == 0) WriteUInt32(bytes, recordOffset, 176);
            WriteUInt32(bytes, recordOffset + 4, (uint)element);
            bytes[recordOffset + 8] = element == 0 ? (byte)11 : (byte)0;
            bytes[recordOffset + 10] = element == 0 ? (byte)4 : (byte)0;
        }
        WriteName(bytes, 176, "transform");
        return bytes;
    }

    /// <summary>Creates a valid one-instruction vertex program with no SDK parameter records.</summary>
    /// <returns>A header and aligned microcode block suitable for dump validation.</returns>
    static byte[] CreateEmptyVertexProgram() {
        byte[] bytes = new byte[64];
        WriteUInt16(bytes, 0, 0x5650);
        WriteUInt16(bytes, 10, 1);
        WriteUInt32(bytes, 12, HeaderSize);
        WriteUInt32(bytes, 16, HeaderSize);
        WriteUInt32(bytes, 20, 48);
        return bytes;
    }

    /// <summary>Creates a fragment program fixture with one relocation table and one referenced instruction word.</summary>
    /// <returns>A valid fragment program byte sequence.</returns>
    static byte[] CreateFragmentProgramWithRelocation() {
        byte[] bytes = CreateFragmentHeaderAndConstant(64);
        WriteUInt32(bytes, 64, 1);
        WriteUInt32(bytes, 68, 0);
        return bytes;
    }

    /// <summary>Creates a fragment program fixture whose constant has no relocation table.</summary>
    /// <returns>A valid fragment program byte sequence.</returns>
    static byte[] CreateFragmentProgramWithoutRelocation() {
        byte[] bytes = CreateFragmentHeaderAndConstant(0xFFFFFFFF);
        return bytes;
    }

    /// <summary>Creates a fragment program whose COLOR0 input uses the SDK's raw attribute index zero.</summary>
    /// <returns>A valid one-instruction fragment input program.</returns>
    static byte[] CreateFragmentColorInputProgram() {
        byte[] bytes = new byte[90];
        WriteUInt16(bytes, 0, 0x4650);
        WriteUInt16(bytes, 4, 1);
        WriteUInt16(bytes, 6, 1);
        WriteUInt16(bytes, 8, 0);
        WriteUInt16(bytes, 10, 1);
        WriteUInt32(bytes, 12, HeaderSize);
        WriteUInt32(bytes, 16, HeaderSize + AttributeRecordSize);
        WriteUInt32(bytes, 20, 64);
        WriteUInt32(bytes, HeaderSize, 80);
        WriteUInt32(bytes, HeaderSize + 4, 0);
        bytes[HeaderSize + 8] = 9;
        WriteName(bytes, 80, "gl_Color");
        return bytes;
    }

    /// <summary>Creates an explicitly laid out fragment header, constant and two-instruction microcode area.</summary>
    /// <param name="relocationTableOffset">File offset to the relocation table, or the SDK no-relocations sentinel.</param>
    /// <returns>A fragment program fixture with all ranges sized for one relocation entry.</returns>
    static byte[] CreateFragmentHeaderAndConstant(uint relocationTableOffset) {
        byte[] bytes = new byte[128];
        WriteUInt16(bytes, 0, 0x4650);
        WriteUInt16(bytes, 4, 2);
        WriteUInt16(bytes, 6, 0);
        WriteUInt16(bytes, 8, 1);
        WriteUInt16(bytes, 10, 2);
        WriteUInt32(bytes, 12, HeaderSize);
        WriteUInt32(bytes, 16, HeaderSize);
        WriteUInt32(bytes, 20, 80);
        WriteUInt32(bytes, 24, 0x40);
        WriteUInt16(bytes, 28, 0);
        WriteUInt16(bytes, 30, 0);
        WriteUInt16(bytes, 32, 0);
        WriteUInt16(bytes, 34, 0);
        WriteUInt32(bytes, HeaderSize, 120);
        WriteUInt32(bytes, HeaderSize + 4, relocationTableOffset);
        bytes[HeaderSize + 8] = 9;
        bytes[HeaderSize + 10] = 1;
        WriteUInt32(bytes, HeaderSize + 12, 0x3F800000);
        WriteUInt32(bytes, 80, 0x12345678);
        WriteUInt32(bytes, 84, 0x90ABCDEF);
        WriteName(bytes, 120, "tint");
        return bytes;
    }

    /// <summary>Writes an SDK big-endian 16-bit header field.</summary>
    /// <param name="bytes">Fixture byte array.</param>
    /// <param name="offset">Field byte offset.</param>
    /// <param name="value">Field value.</param>
    static void WriteUInt16(byte[] bytes, int offset, ushort value) {
        BinaryPrimitives.WriteUInt16BigEndian(bytes.AsSpan(offset, sizeof(ushort)), value);
    }

    /// <summary>Writes an SDK big-endian 32-bit field.</summary>
    /// <param name="bytes">Fixture byte array.</param>
    /// <param name="offset">Field byte offset.</param>
    /// <param name="value">Field value.</param>
    static void WriteUInt32(byte[] bytes, int offset, uint value) {
        BinaryPrimitives.WriteUInt32BigEndian(bytes.AsSpan(offset, sizeof(uint)), value);
    }

    /// <summary>Writes a null-terminated UTF-8 fixture name at an exact file offset.</summary>
    /// <param name="bytes">Fixture byte array.</param>
    /// <param name="offset">String byte offset.</param>
    /// <param name="value">String contents.</param>
    static void WriteName(byte[] bytes, int offset, string value) {
        byte[] encoded = System.Text.Encoding.UTF8.GetBytes(value);
        encoded.CopyTo(bytes, offset);
        bytes[offset + encoded.Length] = 0;
    }
}
