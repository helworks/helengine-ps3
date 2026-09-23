using helengine;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies declared constant-buffer metadata validation and immutable translation snapshots.</summary>
public sealed class Ps3ShaderUniformBufferTests {
    /// <summary>Rejects a buffer record without a compiler-preserved name.</summary>
    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    public void RejectsBlankBufferName(string bufferName) {
        Assert.Throws<ArgumentException>(() => new Ps3ShaderUniformBuffer(bufferName, 0, 16));
    }

    /// <summary>Rejects a buffer record without a positive declared byte size.</summary>
    [Fact]
    public void RejectsZeroBufferSize() {
        Assert.Throws<ArgumentOutOfRangeException>(() => new Ps3ShaderUniformBuffer("Frame", 0, 0));
    }

    /// <summary>Rejects a null uniform-buffer collection when constructing a translation.</summary>
    [Fact]
    public void RejectsNullUniformBufferCollection() {
        Assert.Throws<ArgumentNullException>(() => CreateTranslation(null));
    }

    /// <summary>Rejects null records inside the required uniform-buffer collection.</summary>
    [Fact]
    public void RejectsNullUniformBufferItem() {
        Ps3ShaderUniformBuffer[] buffers = [null];

        Assert.Throws<ArgumentException>(() => CreateTranslation(buffers));
    }

    /// <summary>Copies buffer metadata so later caller mutations cannot alter a translation.</summary>
    [Fact]
    public void CopiesUniformBufferCollection() {
        List<Ps3ShaderUniformBuffer> buffers = [new("Frame", 0, 128)];

        Ps3ShaderTranslation translation = CreateTranslation(buffers);
        buffers.Clear();
        buffers.Add(new Ps3ShaderUniformBuffer("Replacement", 1, 16));

        Ps3ShaderUniformBuffer buffer = Assert.Single(translation.UniformBuffers);
        Assert.Equal("Frame", buffer.BufferName);
        Assert.Equal(0u, buffer.BufferBinding);
        Assert.Equal(128u, buffer.ByteSize);
        Assert.True(Assert.IsAssignableFrom<IList<Ps3ShaderUniformBuffer>>(translation.UniformBuffers).IsReadOnly);
    }

    /// <summary>Constructs a minimal translation with the explicit buffer metadata under test.</summary>
    /// <param name="buffers">Required source buffer collection.</param>
    /// <returns>The copied translation.</returns>
    static Ps3ShaderTranslation CreateTranslation(IEnumerable<Ps3ShaderUniformBuffer> buffers) {
        return new Ps3ShaderTranslation(ShaderStage.Pixel, "shader source", [], [], [], [], [], [], [], buffers);
    }
}
