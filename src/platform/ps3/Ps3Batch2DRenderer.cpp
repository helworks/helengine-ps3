#include "platform/ps3/Ps3Batch2DRenderer.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include "platform/ps3/Ps3AuthoredProgram.hpp"
#include "platform/ps3/Ps3AuthoredTexture.hpp"
#include "platform/ps3/Ps3Batch2DPrograms.hpp"
#include "platform/ps3/Ps3Batch2DResources.hpp"
#include "platform/ps3/Ps3Batch2DUniformProvider.hpp"
#include "platform/ps3/Ps3RuntimeTexture.hpp"
#include "platform/ps3/Ps3Video.hpp"

#include "Batch2DBuffer.hpp"
#include "Batch2DRun.hpp"
#include "Batch2DVariant.hpp"
#include "Batch2DVertex.hpp"
#include "float4.hpp"
#include "float4x4.hpp"

#include <rsx/rsx.h>
#include <lv2/systime.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace helengine::ps3 {
namespace {

/// <summary>Capacity of the batch vertex allocation in complete shared vertices.</summary>
constexpr std::size_t MaximumVertices = 4096u;

/// <summary>Capacity of the batch index allocation in complete ushort indices.</summary>
constexpr std::size_t MaximumIndices = 6144u;

/// <summary>Byte stride of the frozen shared vertex ABI.</summary>
constexpr std::size_t VertexStride = 96u;

/// <summary>Byte size of the bounded RSX vertex allocation.</summary>
constexpr std::size_t VertexBytes = MaximumVertices * VertexStride;

/// <summary>Byte size of the bounded RSX index allocation.</summary>
constexpr std::size_t IndexBytes = MaximumIndices * sizeof(std::uint16_t);

static_assert(sizeof(::Batch2DVertex) == VertexStride, "shared Batch2DVertex ABI changed");
static_assert(sizeof(::float4) == 16u, "shared float4 ABI changed");
static_assert(std::is_standard_layout<::Batch2DVertex>::value, "shared Batch2DVertex must remain standard-layout");
static_assert(std::is_standard_layout<::float4>::value, "shared float4 must remain standard-layout");
static_assert(offsetof(::Batch2DVertex, Position) == 0u, "shared position offset changed");
static_assert(offsetof(::Batch2DVertex, TexLocal) == 16u, "shared texture offset changed");
static_assert(offsetof(::Batch2DVertex, Color) == 32u, "shared color offset changed");
static_assert(offsetof(::Batch2DVertex, Shape) == 48u, "shared shape offset changed");
static_assert(offsetof(::Batch2DVertex, Corners) == 64u, "shared corner offset changed");
static_assert(offsetof(::Batch2DVertex, BorderColor) == 80u, "shared border offset changed");

/// <summary>Throws an explicit native error with the failing batch operation and reason.</summary>
[[noreturn]] void Fail(const char* operation, const std::string& reason) {
    throw std::runtime_error(std::string("PS3 2D ") + operation + ": " + reason);
}

/// <summary>Checks one reflected input's complete four-float vector shape and SDK type.</summary>
bool IsFloat4Attribute(const Ps3CookedShaderAttribute& attribute) {
    return attribute.ScalarType == "FP3232" && attribute.VectorWidth == 4u
        && attribute.Columns == 1u && attribute.ArrayLength == 1u && attribute.SdkDataType == 9u;
}

/// <summary>Maps each exact SpriteBatchShader semantic to its byte offset in the shared vertex.</summary>
bool AttributeOffset(const std::string& semantic, std::size_t& offset) {
    if (semantic == "POSITION0") offset = offsetof(::Batch2DVertex, Position);
    else if (semantic == "TEXCOORD0") offset = offsetof(::Batch2DVertex, TexLocal);
    else if (semantic == "COLOR0") offset = offsetof(::Batch2DVertex, Color);
    else if (semantic == "TEXCOORD1") offset = offsetof(::Batch2DVertex, Shape);
    else if (semantic == "TEXCOORD2") offset = offsetof(::Batch2DVertex, Corners);
    else if (semantic == "COLOR1") offset = offsetof(::Batch2DVertex, BorderColor);
    else return false;
    return true;
}

/// <summary>Validates every active reflected vertex input and rejects duplicate semantics or slots.</summary>
void ValidateAttributes(const Ps3CookedShaderProgram& artifact) {
    if (artifact.Attributes.empty() || artifact.Attributes.size() > 6u) {
        Fail("reflection", "expected one through six active SpriteBatchShader vertex inputs");
    }
    bool semantics[6] = {};
    bool slots[16] = {};
    for (const Ps3CookedShaderAttribute& attribute : artifact.Attributes) {
        std::size_t byteOffset = 0u;
        if (!IsFloat4Attribute(attribute) || !AttributeOffset(attribute.FullAuthoredSemantic, byteOffset)) {
            Fail("reflection", "unsupported active vertex semantic or input shape: " + attribute.FullAuthoredSemantic);
        }
        (void)byteOffset;
        const char* names[6] = { "POSITION0", "TEXCOORD0", "COLOR0", "TEXCOORD1", "TEXCOORD2", "COLOR1" };
        std::size_t semanticIndex = 0u;
        while (semanticIndex < 6u && attribute.FullAuthoredSemantic != names[semanticIndex]) ++semanticIndex;
        if (semanticIndex == 6u || semantics[semanticIndex] || attribute.HardwareSlot >= 16u || slots[attribute.HardwareSlot]) {
            Fail("reflection", "duplicate semantic/slot or out-of-range hardware slot");
        }
        semantics[semanticIndex] = true;
        slots[attribute.HardwareSlot] = true;
    }
}

/// <summary>Checks all six float4 payloads before any vertex can be copied to RSX memory.</summary>
bool IsFiniteVector(const ::float4& value) {
    return RealEvaluation::IsFinite(value.X) && RealEvaluation::IsFinite(value.Y)
        && RealEvaluation::IsFinite(value.Z) && RealEvaluation::IsFinite(value.W);
}

/// <summary>Checks each named shared field without assuming pointer arithmetic across float4 subobjects.</summary>
bool IsFiniteVertex(const ::Batch2DVertex& vertex) {
    return IsFiniteVector(vertex.Position) && IsFiniteVector(vertex.TexLocal)
        && IsFiniteVector(vertex.Color) && IsFiniteVector(vertex.Shape)
        && IsFiniteVector(vertex.Corners) && IsFiniteVector(vertex.BorderColor);
}

/// <summary>Validates the exact shader variant, sampler shape, and active pixel uniforms before command submission.</summary>
void ValidateProgram(const Ps3CookedShaderProgram& artifact, bool textured) {
    ValidateAttributes(artifact);
    if (!artifact.Pixel.Uniforms.empty()) {
        Fail("reflection", "active pixel uniforms are unsupported by the batch contract");
    }
    if ((textured && artifact.PixelSamplers.size() != 1u)
        || (!textured && !artifact.PixelSamplers.empty())) {
        Fail("reflection", textured ? "textured variant must expose exactly one sampler" : "rounded variant must not expose a sampler");
    }
}

/// <summary>Returns the exact float pointer for one shared float4 vertex member.</summary>
} // namespace

Ps3Batch2DRenderer::Ps3Batch2DRenderer(Ps3Video& video, Ps3Batch2DResources& resources)
    : Video(video), Resources(resources), VertexBuffer(nullptr), IndexBuffer(nullptr), VertexOffset(0u),
      IndexOffset(0u), OwnedGpuBytesValue(0u), CameraColumns{}, Viewport{}, Initialized(false),
      CameraConfigured(false), Failed(false), ShutdownComplete(false), ShadowDiagnosticLogged(false),
      DrawCountValue(0u) {
}

Ps3Batch2DRenderer::~Ps3Batch2DRenderer() {
    if (OwnedGpuBytesValue != 0u) {
        std::printf("PS3_BATCH2D_RENDERER_RETAINED bytes=%lu shutdown=%d\n",
            static_cast<unsigned long>(OwnedGpuBytesValue), ShutdownComplete ? 1 : 0);
    }
}

void Ps3Batch2DRenderer::Initialize() {
    if (Initialized || ShutdownComplete || Failed) Fail("initialize", "renderer lifecycle does not allow initialization");
    if (Video.Context() == nullptr || Video.HasDrained()) Fail("initialize", "video context is unavailable");

    void* candidateVertices = rsxMemalign(128u, VertexBytes);
    if (candidateVertices == nullptr) Fail("initialize", "RSX vertex allocation failed");
    void* candidateIndices = rsxMemalign(128u, IndexBytes);
    if (candidateIndices == nullptr) {
        rsxFree(candidateVertices);
        Fail("initialize", "RSX index allocation failed");
    }
    u32 candidateVertexOffset = 0u;
    u32 candidateIndexOffset = 0u;
    if (rsxAddressToOffset(candidateVertices, &candidateVertexOffset) != 0
        || rsxAddressToOffset(candidateIndices, &candidateIndexOffset) != 0) {
        rsxFree(candidateIndices);
        rsxFree(candidateVertices);
        Fail("initialize", "RSX geometry address-to-offset conversion failed");
    }

    VertexBuffer = candidateVertices;
    IndexBuffer = candidateIndices;
    VertexOffset = candidateVertexOffset;
    IndexOffset = candidateIndexOffset;
    OwnedGpuBytesValue = VertexBytes + IndexBytes;
    Initialized = true;
    std::printf("PS3_BATCH2D_RENDERER_READY vertexBytes=%lu indexBytes=%lu vertexOffset=%u indexOffset=%u\n",
        static_cast<unsigned long>(VertexBytes), static_cast<unsigned long>(IndexBytes), VertexOffset, IndexOffset);
}

void Ps3Batch2DRenderer::SetCamera(const ::float4x4& projection, const ::float4& pixelViewport) {
    if (!Initialized || Failed || ShutdownComplete || Video.Context() == nullptr || Video.HasDrained()) {
        Fail("camera", "renderer or video is unavailable");
    }
    const float matrix[16] = { static_cast<float>(projection.M11), static_cast<float>(projection.M21), static_cast<float>(projection.M31), static_cast<float>(projection.M41),
        static_cast<float>(projection.M12), static_cast<float>(projection.M22), static_cast<float>(projection.M32), static_cast<float>(projection.M42),
        static_cast<float>(projection.M13), static_cast<float>(projection.M23), static_cast<float>(projection.M33), static_cast<float>(projection.M43),
        static_cast<float>(projection.M14), static_cast<float>(projection.M24), static_cast<float>(projection.M34), static_cast<float>(projection.M44) };
    for (float value : matrix) if (!std::isfinite(value)) Fail("camera", "projection contains a non-finite element");
    const float viewport[4] = { static_cast<float>(pixelViewport.X), static_cast<float>(pixelViewport.Y), static_cast<float>(pixelViewport.Z), static_cast<float>(pixelViewport.W) };
    for (float value : viewport) {
        if (!std::isfinite(value) || value < 0.0f || value > 65535.0f
            || static_cast<float>(static_cast<std::uint32_t>(value)) != value) {
            Fail("camera", "pixel viewport must contain finite integral values within RSX coordinates");
        }
    }
    if (viewport[0] < 0.0f || viewport[1] < 0.0f || viewport[2] <= 0.0f || viewport[3] <= 0.0f
        || viewport[0] + viewport[2] > static_cast<float>(Video.Width())
        || viewport[1] + viewport[3] > static_cast<float>(Video.Height())
        || viewport[0] + viewport[2] > 65535.0f || viewport[1] + viewport[3] > 65535.0f) {
        Fail("camera", "pixel viewport must have positive size and fit the backbuffer and RSX coordinates");
    }
    std::memcpy(CameraColumns, matrix, sizeof(matrix));
    std::memcpy(Viewport, viewport, sizeof(viewport));
    CameraConfigured = true;
}

void Ps3Batch2DRenderer::Submit(Ps3Batch2DPrograms& programs, ::Batch2DBuffer& buffer, const ::Batch2DRun& run) {
    if (!Initialized || ShutdownComplete || Failed || Video.Context() == nullptr || Video.HasDrained()) {
        Fail("submit", "renderer or video is unavailable");
    }
    if (!CameraConfigured) Fail("submit", "SetCamera must succeed before a run can be submitted");
    if (buffer.Vertices == nullptr || buffer.Indices == nullptr || buffer.Vertices->Data == nullptr || buffer.Indices->Data == nullptr) {
        Fail("submit", "shared buffer arrays or data are null");
    }
    const std::int32_t vertexCount = buffer.VertexCount;
    const std::int32_t indexCount = buffer.IndexCount;
    if (vertexCount <= 0 || indexCount <= 0 || vertexCount % 4 != 0 || indexCount % 6 != 0
        || static_cast<std::uint64_t>(vertexCount) > MaximumVertices
        || static_cast<std::uint64_t>(indexCount) > MaximumIndices
        || static_cast<std::uint64_t>(indexCount) != static_cast<std::uint64_t>(vertexCount / 4) * 6u
        || vertexCount > buffer.Vertices->Length || indexCount > buffer.Indices->Length) {
        Fail("submit", "shared counts exceed capacities or do not describe complete quad triangles");
    }
    const bool textured = run.Variant == ::Batch2DVariant::Textured;
    const bool rounded = run.Variant == ::Batch2DVariant::RoundedShape;
    if (!textured && !rounded) Fail("submit", "unknown shared batch variant");
    if (run.ScissorWidth < 0 || run.ScissorHeight < 0) Fail("submit", "negative run scissor dimensions are invalid");

    const std::int64_t scissorRight = static_cast<std::int64_t>(run.ScissorX) + run.ScissorWidth;
    const std::int64_t scissorBottom = static_cast<std::int64_t>(run.ScissorY) + run.ScissorHeight;
    const std::int64_t left = std::max<std::int64_t>({ 0, static_cast<std::int64_t>(Viewport[0]), run.ScissorX });
    const std::int64_t top = std::max<std::int64_t>({ 0, static_cast<std::int64_t>(Viewport[1]), run.ScissorY });
    const std::int64_t right = std::min<std::int64_t>({ static_cast<std::int64_t>(Video.Width()),
        static_cast<std::int64_t>(Viewport[0] + Viewport[2]), scissorRight });
    const std::int64_t bottom = std::min<std::int64_t>({ static_cast<std::int64_t>(Video.Height()),
        static_cast<std::int64_t>(Viewport[1] + Viewport[3]), scissorBottom });
    if (run.ScissorWidth == 0 || run.ScissorHeight == 0 || right <= left || bottom <= top) return;
    if (right > 65535 || bottom > 65535) Fail("submit", "intersected scissor exceeds RSX coordinates");

    for (std::int32_t index = 0; index < indexCount; ++index) {
        if (buffer.Indices->Data[index] >= static_cast<std::uint32_t>(vertexCount)) {
            Fail("submit", "index references a vertex outside this run");
        }
    }
    for (std::int32_t index = 0; index < vertexCount; ++index) {
        if (!IsFiniteVertex(buffer.Vertices->Data[index])) Fail("submit", "vertex contains a non-finite field");
    }

    Ps3AuthoredProgram& program = textured ? programs.Textured() : programs.RoundedShape();
    const Ps3CookedShaderProgram& artifact = program.Program();
    ValidateProgram(artifact, textured);
    std::vector<std::array<float, 16u>> resolvedUniforms(artifact.Vertex.Uniforms.size());
    std::vector<std::size_t> resolvedUniformCounts(artifact.Vertex.Uniforms.size(), 0u);
    std::string error;
    for (std::size_t uniformIndex = 0u; uniformIndex < artifact.Vertex.Uniforms.size(); ++uniformIndex) {
        if (!Ps3Batch2DUniformProvider::Resolve(artifact.Vertex.Uniforms[uniformIndex], CameraColumns, 16u,
            resolvedUniforms[uniformIndex].data(), resolvedUniforms[uniformIndex].size(),
            resolvedUniformCounts[uniformIndex], error)
            || resolvedUniformCounts[uniformIndex] != 16u) {
            Fail("vertex-uniform-preflight", error.empty() ? "projection provider returned an invalid float count" : error);
        }
    }
    Ps3AuthoredTexture* texture = nullptr;
    if (textured) {
        if (run.Texture == nullptr) Fail("submit", "textured run has no runtime texture");
        Ps3RuntimeTexture* nativeTexture = dynamic_cast<Ps3RuntimeTexture*>(run.Texture);
        if (nativeTexture == nullptr) Fail("submit", "textured run references a non-PS3 runtime texture");
        texture = &Resources.Resolve(*nativeTexture);
    } else if (run.Texture != nullptr) {
        Fail("submit", "rounded run must not carry a texture");
    }

    // All shared input is now preflighted. The idle wait protects both geometry reuse and program constants.
    if (!Video.WaitForSubmittedWork()) {
        Failed = true;
        Fail("submit", "video could not complete prior RSX work; renderer is terminal");
    }
    std::memcpy(VertexBuffer, buffer.Vertices->Data, static_cast<std::size_t>(vertexCount) * VertexStride);
    std::memcpy(IndexBuffer, buffer.Indices->Data, static_cast<std::size_t>(indexCount) * sizeof(std::uint16_t));

    // A bounded late quad sample proves that the rotating authored backdrop retains its pivot.
    static const s64 firstSubmissionMicroseconds = sysGetSystemTime();
    static bool lateBackdropReported = false;
    if (!lateBackdropReported && textured && run.Texture->get_Width() == 512 && run.Texture->get_Height() == 512
        && sysGetSystemTime() - firstSubmissionMicroseconds >= 90000000) {
        lateBackdropReported = true;
        const ::float4& first = buffer.Vertices->Data[0].Position;
        const ::float4& opposite = buffer.Vertices->Data[2].Position;
        std::printf("PS3_BATCH2D_LATE_BACKGROUND elapsedSeconds=%lld center=%.3f,%.3f first=%.3f,%.3f opposite=%.3f,%.3f\n",
            static_cast<long long>((sysGetSystemTime() - firstSubmissionMicroseconds) / 1000000),
            (static_cast<double>(first.X) + static_cast<double>(opposite.X)) * 0.5f, (static_cast<double>(first.Y) + static_cast<double>(opposite.Y)) * 0.5f,
            static_cast<double>(first.X), static_cast<double>(first.Y), static_cast<double>(opposite.X), static_cast<double>(opposite.Y));
    }

    // Report the actual first submitted quad once, after shared geometry expansion.
    if (DrawCountValue == 0u) {
        std::printf("PS3_BATCH2D_FIRST_QUAD texture=%ux%u vertices=%d indices=%d\n",
            run.Texture == nullptr ? 0u : static_cast<unsigned>(run.Texture->get_Width()),
            run.Texture == nullptr ? 0u : static_cast<unsigned>(run.Texture->get_Height()), vertexCount, indexCount);
        for (std::int32_t corner = 0; corner < 4; ++corner) {
            const ::Batch2DVertex& vertex = buffer.Vertices->Data[corner];
            std::printf("PS3_BATCH2D_FIRST_VERTEX corner=%d position=%.3f,%.3f,%.3f,%.3f uv=%.3f,%.3f rgba=%.3f,%.3f,%.3f,%.3f\n",
                corner, static_cast<double>(vertex.Position.X), static_cast<double>(vertex.Position.Y), static_cast<double>(vertex.Position.Z), static_cast<double>(vertex.Position.W),
                static_cast<double>(vertex.TexLocal.X), static_cast<double>(vertex.TexLocal.Y), static_cast<double>(vertex.Color.X), static_cast<double>(vertex.Color.Y), static_cast<double>(vertex.Color.Z), static_cast<double>(vertex.Color.W));
        }
    }

    gcmContextData* context = Video.Context();
    if (context == nullptr) {
        Failed = true;
        Fail("submit", "video context disappeared after the idle wait");
    }
    const u16 viewportX = static_cast<u16>(Viewport[0]);
    const u16 viewportY = static_cast<u16>(Viewport[1]);
    const u16 viewportWidth = static_cast<u16>(Viewport[2]);
    const u16 viewportHeight = static_cast<u16>(Viewport[3]);
    const f32 viewportScale[4] = { Viewport[2] * 0.5f, Viewport[3] * -0.5f, 0.5f, 0.0f };
    const f32 viewportOffset[4] = { Viewport[0] + Viewport[2] * 0.5f,
        Viewport[1] + Viewport[3] * 0.5f, 0.5f, 0.0f };
    rsxSetViewport(context, viewportX, viewportY, viewportWidth, viewportHeight, 0.0f, 1.0f, viewportScale, viewportOffset);
    for (u8 clipIndex = 0u; clipIndex < 8u; ++clipIndex) {
        rsxSetViewportClip(context, clipIndex, static_cast<u16>(Video.Width()), static_cast<u16>(Video.Height()));
    }
    rsxSetScissor(context, static_cast<u16>(left), static_cast<u16>(top),
        static_cast<u16>(right - left), static_cast<u16>(bottom - top));
    rsxSetColorMask(context, GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B | GCM_COLOR_MASK_A);
    rsxSetBlendEnable(context, GCM_TRUE);
    rsxSetBlendEquation(context, GCM_FUNC_ADD, GCM_FUNC_ADD);
    rsxSetBlendFunc(context, GCM_SRC_ALPHA, GCM_ONE_MINUS_SRC_ALPHA, GCM_ONE, GCM_ONE_MINUS_SRC_ALPHA);
    rsxSetDepthTestEnable(context, GCM_FALSE);
    rsxSetDepthWriteEnable(context, GCM_FALSE);
    rsxSetCullFaceEnable(context, GCM_FALSE);
    rsxSetStencilTestEnable(context, GCM_FALSE);
    rsxSetAlphaTestEnable(context, GCM_FALSE);
    rsxSetLogicOpEnable(context, GCM_FALSE);
    rsxSetUserClipPlaneControl(context, GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE,
        GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE,
        GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE);

    // BeginDraw loads the reflected vertex program and its input mask before the active slots are rebound.
    if (!program.BeginDraw(error)) {
        Failed = true;
        Fail("begin-draw", error);
    }
    for (const Ps3CookedShaderAttribute& attribute : artifact.Attributes) {
        std::size_t memberOffset = 0u;
        AttributeOffset(attribute.FullAuthoredSemantic, memberOffset);
        if (DrawCountValue == 0u) {
            std::printf("PS3_BATCH2D_ATTRIBUTE semantic=%s generated=%s slot=%u sdkType=%u storageType=%u offset=%lu\n",
                attribute.FullAuthoredSemantic.c_str(), attribute.GeneratedName.c_str(), attribute.HardwareSlot,
                attribute.SdkDataType, static_cast<unsigned>(GCM_VERTEX_DATA_TYPE_F32),
                static_cast<unsigned long>(memberOffset));
        }
        rsxBindVertexArrayAttrib(context, static_cast<u8>(attribute.HardwareSlot), 0u,
            VertexOffset + static_cast<u32>(memberOffset), static_cast<u8>(VertexStride), 4u,
            GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    }

    for (std::size_t uniformIndex = 0u; uniformIndex < artifact.Vertex.Uniforms.size(); ++uniformIndex) {
        if (!program.UploadUniform(artifact.Vertex.Stage, uniformIndex, resolvedUniforms[uniformIndex].data(),
            resolvedUniformCounts[uniformIndex], error)) {
            Failed = true;
            Fail("vertex-uniform", error);
        }
    }
    if (textured && !texture->BindLinearClamp(artifact.PixelSamplers[0].TextureUnit, error)) {
        Failed = true;
        Fail("texture-bind", error);
    }
    if (!program.FinishUniformUpdates(error)) {
        Failed = true;
        Fail("finish-uniforms", error);
    }

    // Capture actual submitted inputs, bounded to one black quad and its following quad.
    if (textured && !ShadowDiagnosticLogged) {
        for (std::int32_t quad = 0; quad < vertexCount / 4; ++quad) {
            const std::int32_t firstVertex = quad * 4;
            bool opaqueBlack = true;
            for (std::int32_t corner = 0; corner < 4; ++corner) {
                const ::float4& color = buffer.Vertices->Data[firstVertex + corner].Color;
                if (color.X != 0.0f || color.Y != 0.0f || color.Z != 0.0f || color.W != 1.0f) {
                    opaqueBlack = false;
                    break;
                }
            }
            if (!opaqueBlack) continue;

            const std::int32_t traceVertexCount = std::min(8, vertexCount - firstVertex);
            const auto* nativeTexture = static_cast<void*>(run.Texture);
            std::printf("PS3_BATCH2D_SHADOW_TRACE texture=%p dimensions=%ux%u vertices=%d indices=%d firstQuad=%d firstIndex=%d tracedVertices=%d\n",
                nativeTexture, static_cast<unsigned>(run.Texture->get_Width()),
                static_cast<unsigned>(run.Texture->get_Height()), vertexCount, indexCount,
                quad, quad * 6, traceVertexCount);
            for (std::int32_t index = 0; index < traceVertexCount; ++index) {
                const std::int32_t sourceIndex = firstVertex + index;
                const ::Batch2DVertex& vertex = buffer.Vertices->Data[sourceIndex];
                std::printf("PS3_BATCH2D_SHADOW_VERTEX ordinal=%d source=%d position=(%.3f,%.3f,%.3f,%.3f) uv=(%.3f,%.3f,%.3f,%.3f) rgba=(%.3f,%.3f,%.3f,%.3f)\n",
                    index, sourceIndex, static_cast<double>(vertex.Position.X), static_cast<double>(vertex.Position.Y), static_cast<double>(vertex.Position.Z),
                    static_cast<double>(vertex.Position.W), static_cast<double>(vertex.TexLocal.X), static_cast<double>(vertex.TexLocal.Y), static_cast<double>(vertex.TexLocal.Z),
                    static_cast<double>(vertex.TexLocal.W), static_cast<double>(vertex.Color.X), static_cast<double>(vertex.Color.Y), static_cast<double>(vertex.Color.Z), static_cast<double>(vertex.Color.W));
            }
            ShadowDiagnosticLogged = true;
            break;
        }
    }

    rsxDrawIndexArray(context, GCM_TYPE_TRIANGLES, IndexOffset, static_cast<u32>(indexCount),
        GCM_INDEX_TYPE_16B, GCM_LOCATION_RSX);
    ++DrawCountValue;
}

bool Ps3Batch2DRenderer::ShutdownAfterDrain(std::string& error) {
    error.clear();
    if (ShutdownComplete) return true;
    if (!Video.HasDrained()) {
        error = "video has not completed its successful terminal drain";
        return false;
    }
    if (IndexBuffer != nullptr) rsxFree(IndexBuffer);
    if (VertexBuffer != nullptr) rsxFree(VertexBuffer);
    IndexBuffer = nullptr;
    VertexBuffer = nullptr;
    IndexOffset = 0u;
    VertexOffset = 0u;
    OwnedGpuBytesValue = 0u;
    Initialized = false;
    ShutdownComplete = true;
    return true;
}

std::size_t Ps3Batch2DRenderer::OwnedGpuBytes() const {
    return OwnedGpuBytesValue;
}

std::uint64_t Ps3Batch2DRenderer::DrawCount() const {
    return DrawCountValue;
}

} // namespace helengine::ps3

#endif
