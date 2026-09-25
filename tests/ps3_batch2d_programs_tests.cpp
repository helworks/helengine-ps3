#include "platform/ps3/Ps3Batch2DPrograms.hpp"
#include "platform/ps3/Ps3Batch2DUniformProvider.hpp"
#include "platform/ps3/Ps3ContentStreamSource.hpp"
#include "platform/ps3/Ps3CookedShaderUniform.hpp"
#include "platform/ps3/Ps3ShaderArtifactReader.hpp"
#include "platform/ps3/Ps3Video.hpp"
#include "system/io/stream.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

/// <summary>Counts named fixture assertions and emits stable PASS or FAIL TTY records.</summary>
std::size_t Passed = 0u;

/// <summary>Counts named fixture assertions that did not meet their expected native behavior.</summary>
std::size_t Failed = 0u;

/// <summary>Reports one boolean fixture assertion under its stable machine-readable name.</summary>
void Expect(bool condition, const char* name) {
    if (condition) {
        ++Passed;
        std::printf("PASS %s\n", name);
    } else {
        ++Failed;
        std::printf("FAIL %s\n", name);
    }
}

/// <summary>Checks every required field of the measured projection mapping before CPU resolution.</summary>
bool HasUniformShape(const helengine::ps3::Ps3CookedShaderUniform& uniform) {
    return uniform.BufferName == "BatchCameraBuffer"
        && uniform.AuthoredBinding == 0u
        && uniform.MemberPath == "projection"
        && uniform.ByteOffset == 0u && uniform.ByteSize == 64u
        && uniform.MatrixStride == 16u && uniform.ArrayStride == 0u
        && uniform.ScalarType == "FP3232" && uniform.VectorWidth == 4u
        && uniform.Columns == 4u && uniform.ArrayLength == 1u
        && uniform.RowMajor == 1u && uniform.SdkDataType == 11u
        && uniform.SdkElementCount == 4u;
}

/// <summary>Loads one packaged P3SP through the supplied content-source interface and the production artifact reader.</summary>
bool ReadProgram(::IContentStreamSource& source, const std::string& path,
    helengine::ps3::Ps3CookedShaderProgram& program, std::string& error) {
    std::unique_ptr<::Stream> stream(source.OpenRead(path));
    if (stream == nullptr) { error = "required P3SP stream is missing"; return false; }
    const std::size_t length = stream->Length();
    if (length == 0u || length > 16u * 1024u * 1024u) { error = "test P3SP length is outside bounds"; return false; }
    std::vector<std::uint8_t> bytes(length);
    std::size_t offset = 0u;
    while (offset < length) {
        const std::size_t amount = stream->Read(bytes.data(), offset, length - offset);
        if (amount == 0u || amount > length - offset) { error = "test stream made no valid progress"; return false; }
        offset += amount;
    }
    if (stream->ReadByte() != -1) { error = "test P3SP stream contains trailing bytes"; return false; }
    stream->Dispose();
    return helengine::ps3::Ps3ShaderArtifactReader::Read(bytes.data(), bytes.size(), program, error);
}

/// <summary>Prints measured identities and metadata from a decoded packaged P3SP for the parent and renderer owner.</summary>
void PrintMeasuredArtifact(const helengine::ps3::Ps3CookedShaderProgram& program) {
    std::printf("PS3_BATCH2D_ARTIFACT asset=%s variant=%s sourceHash=%s toolchain=%s attributes=%lu vertexBuffers=%lu vertexUniforms=%lu pixelBuffers=%lu pixelUniforms=%lu samplers=%lu\n",
        program.ShaderAssetId.c_str(), program.VariantName.c_str(), program.SourceHash.c_str(),
        program.ToolchainIdentity.c_str(), static_cast<unsigned long>(program.Attributes.size()),
        static_cast<unsigned long>(program.Vertex.Buffers.size()), static_cast<unsigned long>(program.Vertex.Uniforms.size()),
        static_cast<unsigned long>(program.Pixel.Buffers.size()), static_cast<unsigned long>(program.Pixel.Uniforms.size()),
        static_cast<unsigned long>(program.PixelSamplers.size()));
    for (const auto& buffer : program.Vertex.Buffers) {
        std::printf("PS3_BATCH2D_BUFFER variant=%s stage=vertex name=%s binding=%lu bytes=%lu\n",
            program.VariantName.c_str(), buffer.BufferName.c_str(),
            static_cast<unsigned long>(buffer.AuthoredBinding), static_cast<unsigned long>(buffer.DeclaredByteSize));
    }
    for (const auto& uniform : program.Vertex.Uniforms) {
        std::printf("PS3_BATCH2D_UNIFORM variant=%s stage=vertex buffer=%s binding=%lu member=%s offset=%lu bytes=%lu arrayStride=%lu matrixStride=%lu rowMajor=%lu scalar=%s vector=%lu columns=%lu arrayLength=%lu sdkType=%lu sdkElements=%lu generated=%s\n",
            program.VariantName.c_str(), uniform.BufferName.c_str(), static_cast<unsigned long>(uniform.AuthoredBinding),
            uniform.MemberPath.c_str(), static_cast<unsigned long>(uniform.ByteOffset), static_cast<unsigned long>(uniform.ByteSize),
            static_cast<unsigned long>(uniform.ArrayStride), static_cast<unsigned long>(uniform.MatrixStride),
            static_cast<unsigned long>(uniform.RowMajor), uniform.ScalarType.c_str(),
            static_cast<unsigned long>(uniform.VectorWidth), static_cast<unsigned long>(uniform.Columns),
            static_cast<unsigned long>(uniform.ArrayLength), static_cast<unsigned long>(uniform.SdkDataType),
            static_cast<unsigned long>(uniform.SdkElementCount), uniform.GeneratedName.c_str());
    }
    for (const auto& attribute : program.Attributes) {
        std::printf("PS3_BATCH2D_ATTRIBUTE variant=%s semantic=%s generated=%s scalar=%s vector=%lu columns=%lu arrayLength=%lu sdkType=%lu slot=%lu\n",
            program.VariantName.c_str(), attribute.FullAuthoredSemantic.c_str(), attribute.GeneratedName.c_str(),
            attribute.ScalarType.c_str(), static_cast<unsigned long>(attribute.VectorWidth),
            static_cast<unsigned long>(attribute.Columns), static_cast<unsigned long>(attribute.ArrayLength),
            static_cast<unsigned long>(attribute.SdkDataType), static_cast<unsigned long>(attribute.HardwareSlot));
    }
    for (const auto& sampler : program.PixelSamplers) {
        std::printf("PS3_BATCH2D_SAMPLER variant=%s texture=%s textureBinding=%lu sampler=%s samplerBinding=%lu dimension=%s generated=%s unit=%lu sdkType=%lu\n",
            program.VariantName.c_str(), sampler.TextureName.c_str(), static_cast<unsigned long>(sampler.TextureBinding),
            sampler.SamplerName.c_str(), static_cast<unsigned long>(sampler.SamplerBinding), sampler.Dimension.c_str(),
            sampler.GeneratedName.c_str(), static_cast<unsigned long>(sampler.TextureUnit),
            static_cast<unsigned long>(sampler.SdkDataType));
    }
}

/// <summary>Requires one invalid projection input to reject with zero writes and unchanged destination sentinels.</summary>
void ExpectProviderReject(const char* name, const helengine::ps3::Ps3CookedShaderUniform& uniform,
    const float* source, std::size_t sourceCount, bool destinationPresent, std::size_t capacity) {
    float local[16];
    for (float& value : local) value = 91.0f;
    float* output = destinationPresent ? local : nullptr;
    std::size_t written = 77u;
    std::string error;
    const bool accepted = helengine::ps3::Ps3Batch2DUniformProvider::Resolve(
        uniform, source, sourceCount, output, capacity, written, error);
    bool untouched = output == nullptr;
    if (output != nullptr) {
        untouched = true;
        for (float value : local) untouched = untouched && value == 91.0f;
    }
    Expect(!accepted && written == 0u && !error.empty() && untouched, name);
}

/// <summary>Exercises representative wrong mapping, shape, SDK, capacity, pointer, and finite-value failures.</summary>
void TestProviderRejections(const helengine::ps3::Ps3CookedShaderUniform& sourceUniform) {
    using helengine::ps3::Ps3CookedShaderUniform;
    const float source[16] = {1.0f};
    Ps3CookedShaderUniform changed = sourceUniform;
    changed.AuthoredBinding = 2u;
    ExpectProviderReject("provider-wrong-binding", changed, source, 16u, true, 16u);
    changed = sourceUniform; changed.ByteOffset = 16u;
    ExpectProviderReject("provider-wrong-offset", changed, source, 16u, true, 16u);
    changed = sourceUniform; changed.ByteSize = 48u;
    ExpectProviderReject("provider-wrong-size", changed, source, 16u, true, 16u);
    changed = sourceUniform; changed.MatrixStride = 8u;
    ExpectProviderReject("provider-wrong-matrix-stride", changed, source, 16u, true, 16u);
    changed = sourceUniform; changed.ArrayStride = 16u;
    ExpectProviderReject("provider-wrong-array-stride", changed, source, 16u, true, 16u);
    changed = sourceUniform; changed.RowMajor = 0u;
    ExpectProviderReject("provider-wrong-row-major", changed, source, 16u, true, 16u);
    changed = sourceUniform; changed.MemberPath = "world";
    ExpectProviderReject("provider-wrong-member", changed, source, 16u, true, 16u);
    changed = sourceUniform; changed.ScalarType = "FP1616";
    ExpectProviderReject("provider-wrong-scalar", changed, source, 16u, true, 16u);
    changed = sourceUniform; changed.VectorWidth = 3u;
    ExpectProviderReject("provider-wrong-vector-width", changed, source, 16u, true, 16u);
    changed = sourceUniform; changed.Columns = 3u;
    ExpectProviderReject("provider-wrong-matrix-columns", changed, source, 16u, true, 16u);
    changed = sourceUniform; changed.ArrayLength = 2u;
    ExpectProviderReject("provider-wrong-array-length", changed, source, 16u, true, 16u);
    changed = sourceUniform; changed.SdkDataType = 9u;
    ExpectProviderReject("provider-wrong-sdk-type", changed, source, 16u, true, 16u);
    changed = sourceUniform; changed.SdkElementCount = 3u;
    ExpectProviderReject("provider-wrong-sdk-elements", changed, source, 16u, true, 16u);
    changed = sourceUniform; changed.GeneratedName = "unknown_projection";
    ExpectProviderReject("provider-wrong-generated-name", changed, source, 16u, true, 16u);
    changed = sourceUniform;
    ExpectProviderReject("provider-short-source", changed, source, 15u, true, 16u);
    ExpectProviderReject("provider-short-capacity", changed, source, 16u, true, 15u);
    ExpectProviderReject("provider-null-source", changed, nullptr, 16u, true, 16u);
    ExpectProviderReject("provider-null-destination", changed, source, 16u, false, 16u);
    float nonfinite[16] = {1.0f};
    nonfinite[8] = std::numeric_limits<float>::infinity();
    ExpectProviderReject("provider-infinite-source", changed, nonfinite, 16u, true, 16u);
}

/// <summary>Measures packaged variants, initializes both owners, resolves projection values, and drains safely.</summary>
void TestPackagedProgramPair() {
    using namespace helengine::ps3;
    Ps3ContentStreamSource source;
    Ps3CookedShaderProgram measuredTextured{};
    Ps3CookedShaderProgram measuredRounded{};
    std::string readError;
    const std::string texturedPath = "cooked/shaders/842895fde6e1db0816b78c52d176284de1fbb2d40dc819196b6ba7db7d09558a.ps3sp";
    const std::string roundedPath = "cooked/shaders/9a0867e0cecd7e04a48ecb452b32db7a42deecaab88912cbcc71427cf4b3233d.ps3sp";
    if (!ReadProgram(source, texturedPath, measuredTextured, readError)
        || !ReadProgram(source, roundedPath, measuredRounded, readError)) {
        std::printf("FAIL packaged-artifact-read: %s\n", readError.c_str());
        ++Failed;
        return;
    }
    PrintMeasuredArtifact(measuredTextured);
    PrintMeasuredArtifact(measuredRounded);
    Expect(Ps3Batch2DPrograms::ValidatePair(measuredTextured, measuredRounded, readError), "cpu-pair-contract");
    Ps3Video video;
    if (!video.Initialize()) {
        Expect(false, "video-initialize");
        return;
    }

    Ps3Batch2DPrograms programs(video, source);
    try {
        programs.Initialize(texturedPath, roundedPath);
        const auto& textured = programs.Textured().Program();
        const auto& rounded = programs.RoundedShape().Program();
        Expect(textured.ShaderAssetId == "SpriteBatchShader"
            && textured.VertexProgramName == "SpriteBatchShader.vs"
            && textured.PixelProgramName == "SpriteBatchShader.ps"
            && textured.VariantName == "Textured", "textured-identity");
        Expect(rounded.ShaderAssetId == "SpriteBatchShader"
            && rounded.VariantName == "RoundedShape", "rounded-identity");
        Expect(!textured.SourceHash.empty() && textured.SourceHash == rounded.SourceHash
            && !textured.ToolchainIdentity.empty()
            && textured.ToolchainIdentity == rounded.ToolchainIdentity, "shared-provenance");

        const auto uniform = std::find_if(textured.Vertex.Uniforms.begin(), textured.Vertex.Uniforms.end(),
            [](const Ps3CookedShaderUniform& item) { return item.MemberPath == "projection"; });
        Expect(uniform != textured.Vertex.Uniforms.end(), "projection-uniform-present");
        if (uniform != textured.Vertex.Uniforms.end()) {
            Expect(HasUniformShape(*uniform), "projection-measured-metadata");
            TestProviderRejections(*uniform);
            const float sdkColumns[16] = {
                2.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 3.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 4.0f, 0.0f,
                5.0f, 6.0f, 7.0f, 1.0f
            };
            float result[16] = {};
            std::size_t written = 0u;
            std::string error;
            const bool accepted = Ps3Batch2DUniformProvider::Resolve(
                *uniform, sdkColumns, 16u, result, 16u, written, error);
            Expect(accepted && written == 16u && result[1] == 0.0f
                && result[4] == 0.0f && result[12] == 5.0f && result[13] == 6.0f
                && result[14] == 7.0f, "projection-column-order-preserved");
        }
        Ps3CookedShaderProgram wrongVariant = textured;
        wrongVariant.VariantName = "RoundedShape";
        Expect(!Ps3Batch2DPrograms::ValidatePair(wrongVariant, rounded, readError), "pair-wrong-variant-rejected");
        Ps3CookedShaderProgram wrongHash = rounded;
        wrongHash.SourceHash.push_back('x');
        Expect(!Ps3Batch2DPrograms::ValidatePair(textured, wrongHash, readError), "pair-source-hash-mismatch-rejected");
        Ps3CookedShaderProgram unknownSemantic = textured;
        unknownSemantic.Attributes[0].FullAuthoredSemantic = "NORMAL0";
        Expect(!Ps3Batch2DPrograms::ValidatePair(unknownSemantic, rounded, readError), "pair-unknown-semantic-rejected");
        Ps3CookedShaderProgram unknownSampler = textured;
        unknownSampler.PixelSamplers[0].SamplerName = "UnknownSampler";
        Expect(!Ps3Batch2DPrograms::ValidatePair(unknownSampler, rounded, readError), "pair-unknown-sampler-rejected");
        Ps3CookedShaderProgram wrongAttributeSdk = textured;
        wrongAttributeSdk.Attributes[0].SdkDataType = 4u;
        Expect(!Ps3Batch2DPrograms::ValidatePair(wrongAttributeSdk, rounded, readError), "pair-wrong-attribute-sdk-type-rejected");
        Ps3CookedShaderProgram wrongSamplerSdk = textured;
        wrongSamplerSdk.PixelSamplers[0].SdkDataType = 14u;
        Expect(!Ps3Batch2DPrograms::ValidatePair(wrongSamplerSdk, rounded, readError), "pair-wrong-sampler-sdk-type-rejected");
        Ps3CookedShaderProgram missingProjectionBuffer = textured;
        missingProjectionBuffer.Vertex.Buffers.clear();
        Expect(!Ps3Batch2DPrograms::ValidatePair(missingProjectionBuffer, rounded, readError), "pair-missing-projection-buffer-rejected");
        Ps3ContentStreamSource otherSource;
        Expect(!programs.MatchesSource(otherSource), "source-object-identity");
        Expect(programs.OwnedGpuBytes() > 0u, "both-programs-own-gpu-bytes");
    } catch (const std::exception& exception) {
        std::printf("FAIL pair-load: %s\n", exception.what());
        ++Failed;
    }

    const bool drained = video.Drain();
    Expect(drained, "video-final-drain");
    if (drained) {
        std::string error;
        Expect(programs.ShutdownAfterDrain(error), "programs-shutdown-after-drain");
        Expect(programs.OwnedGpuBytes() == 0u, "gpu-bytes-zero-after-shutdown");
    }
    video.Shutdown();
}

} // namespace

/// <summary>Runs the native packaged-pair fixture and returns failure when any named assertion fails.</summary>
int main() {
    TestPackagedProgramPair();
    std::printf("PS3_BATCH2D_PROGRAMS_TESTS passed=%lu failed=%lu\n",
        static_cast<unsigned long>(Passed), static_cast<unsigned long>(Failed));
    return Failed == 0u ? 0 : 1;
}
