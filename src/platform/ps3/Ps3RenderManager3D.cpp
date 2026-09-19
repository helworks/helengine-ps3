#include "platform/ps3/Ps3RenderManager3D.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdio>

#include "AssetSerializer.hpp"
#include "Core.hpp"
#include "Entity.hpp"
#include "ICamera.hpp"
#include "IContentStreamSource.hpp"
#include "IDrawable3D.hpp"
#include "IRenderQueue3D.hpp"
#include "ObjectManager.hpp"
#include "ModelAsset.hpp"
#include "PlatformMaterialAsset.hpp"
#include "float3.hpp"
#include "runtime/native_list.hpp"
#include "system/io/stream.hpp"

#include "platform/ps3/Ps3RuntimeAssets.hpp"
#include "platform/ps3/Ps3RasterRenderer.hpp"
#include "platform/ps3/Ps3Vertex.hpp"
#include "platform/ps3/Ps3Video.hpp"

#include "runtime/native_exceptions.hpp"
#include "runtime/native_runtime.hpp"

namespace helengine::ps3 {
    namespace {
        /// Converts one engine matrix into the renderer's row-major storage.
        ///
        /// The vertex program multiplies column vectors, so the matrix reaching it must be
        /// row-major with translation in the fourth row's counterpart. The engine stores
        /// M11..M44 row by row with translation in M41..M43, which is the transpose of what
        /// the program wants, so it is transposed here rather than in the shader.
        Ps3Matrix4 ToRowMajor(const ::float4x4& matrix) {
            Ps3Matrix4 result;
            result.Values[0] = matrix.M11;
            result.Values[1] = matrix.M21;
            result.Values[2] = matrix.M31;
            result.Values[3] = matrix.M41;
            result.Values[4] = matrix.M12;
            result.Values[5] = matrix.M22;
            result.Values[6] = matrix.M32;
            result.Values[7] = matrix.M42;
            result.Values[8] = matrix.M13;
            result.Values[9] = matrix.M23;
            result.Values[10] = matrix.M33;
            result.Values[11] = matrix.M43;
            result.Values[12] = matrix.M14;
            result.Values[13] = matrix.M24;
            result.Values[14] = matrix.M34;
            result.Values[15] = matrix.M44;
            return result;
        }
    }

    Ps3RenderManager3D::Ps3RenderManager3D(Ps3Video& video, Ps3RasterRenderer& renderer)
        : DrawCounter(0)
        , LoggedFirstDraw(false)
        , RaiseOnDraw(false)
        , LoggedFirstMaterial(false)
        , LoggedFirstModel(false)
        , MaterialCounter(0)
        , ModelCounter(0)
        , Renderer(renderer)
        , Video(video)
        , ViewProjection(::float4x4::get_Identity())
        , VisitedDrawables(0)
        , SubmittedDrawables(0)
        , LoggedCamera(false)
        , LoggedFirstVisit(false)
        , LastSkipMarker(nullptr) {
    }

    void Ps3RenderManager3D::NoteSkip(const char* marker) {
        // The same reason repeats every frame once it occurs, so only a change is reported.
        if (LastSkipMarker == marker) {
            return;
        }
        LastSkipMarker = marker;
        std::printf("PS3_CORE_RENDER3D_SKIP reason=%s frame=%u\n", marker, DrawCounter);
    }

    void Ps3RenderManager3D::ArmDeliberateRaise() {
        RaiseOnDraw = true;
        std::printf("PS3_CORE_UNWIND_ARMED target=RenderManager3D.Draw\n");
    }

    std::uint32_t Ps3RenderManager3D::DrawCount() const {
        return DrawCounter;
    }

    void Ps3RenderManager3D::Draw() {
        if (RaiseOnDraw) {
            // Disarm first, so a caller that catches and continues does not loop here.
            RaiseOnDraw = false;
            std::printf("PS3_CORE_UNWIND_RAISE stage=RenderManager3D.Draw draws=%u\n", DrawCounter);
            he_cpp_raise(new ::NotSupportedException(
                "Deliberate raise from the PlayStation 3 3D adapter, requested by the build to prove exception unwinding through generated frames."));
        }
        ++DrawCounter;
        if (!LoggedFirstDraw) {
            LoggedFirstDraw = true;
            std::printf("PS3_CORE_RENDER3D_FIRST_DRAW\n");
        }

        VisitedDrawables = 0;
        SubmittedDrawables = 0;

        // Every stage below names itself when it cannot proceed, because a camera or queue
        // that is merely absent looks identical on screen to one that is broken.
        ::Core* core = get_OwnerCore();
        if (core == nullptr || core->get_ObjectManager() == nullptr) {
            NoteSkip("no-object-manager");
            return;
        }
        ::List<::ICamera*>* cameras = core->get_ObjectManager()->get_Cameras();
        if (cameras == nullptr || cameras->get_Count() == 0) {
            NoteSkip("no-camera");
            return;
        }
        ::ICamera* camera = cameras->get_Item(0);
        if (camera == nullptr || camera->get_Parent() == nullptr || camera->get_RenderQueue3D() == nullptr) {
            NoteSkip("camera-incomplete");
            return;
        }

        // The view is the inverse of the camera's world transform, and the projection uses the
        // authored field of view and planes, so one scene frames the same way on every platform.
        ::float4x4 cameraWorld = camera->get_Parent()->get_WorldTransformMatrix();
        // The engine keeps plain inversion private but exposes the inverse transpose, so the
        // view is recovered by transposing that back. Same result, public surface only.
        ::float4x4 inverseTranspose = ::float4x4::get_Identity();
        if (!::float4x4::TryInverseTranspose__ref0_out1(cameraWorld, inverseTranspose)) {
            NoteSkip("camera-transform-not-invertible");
            return;
        }
        ::float4x4 view = ::float4x4::get_Identity();
        ::float4x4::Transpose__ref0_out1(inverseTranspose, view);
        ::float4x4 projection = ::float4x4::get_Identity();
        ::float4x4::CreatePerspectiveFieldOfView__out4(
            camera->get_FieldOfView(),
            Video.Aspect(),
            camera->get_NearPlaneDistance(),
            camera->get_FarPlaneDistance(),
            projection);
        ::float4x4::Multiply__ref0_ref1_out2(view, projection, ViewProjection);

        if (!LoggedCamera) {
            LoggedCamera = true;
            std::printf("PS3_CORE_CAMERA fov=%.4f near=%.3f far=%.1f aspect=%.4f position=%.3f,%.3f,%.3f\n",
                        static_cast<double>(camera->get_FieldOfView()),
                        static_cast<double>(camera->get_NearPlaneDistance()),
                        static_cast<double>(camera->get_FarPlaneDistance()),
                        static_cast<double>(Video.Aspect()),
                        static_cast<double>(cameraWorld.M41),
                        static_cast<double>(cameraWorld.M42),
                        static_cast<double>(cameraWorld.M43));
        }

        camera->get_RenderQueue3D()->VisitOrdered(this);

        if (DrawCounter == 1) {
            std::printf("PS3_CORE_RENDER3D_QUEUE visited=%u submitted=%u\n", VisitedDrawables, SubmittedDrawables);
        }
    }

    void Ps3RenderManager3D::Visit(::IDrawable3D* drawable) {
        ++VisitedDrawables;
        if (drawable == nullptr || drawable->get_Parent() == nullptr) {
            NoteSkip("drawable-without-entity");
            return;
        }

        Ps3RuntimeModel* model = dynamic_cast<Ps3RuntimeModel*>(drawable->get_Model());
        if (model == nullptr) {
            NoteSkip("drawable-without-ps3-model");
            return;
        }

        ::float4x4 world = drawable->get_Parent()->get_WorldTransformMatrix();
        ::float4x4 modelViewProjection = ::float4x4::get_Identity();
        ::float4x4::Multiply__ref0_ref1_out2(world, ViewProjection, modelViewProjection);

        if (!LoggedFirstVisit) {
            LoggedFirstVisit = true;
            std::printf("PS3_CORE_DRAWABLE_FIRST indices=%u position=%.3f,%.3f,%.3f\n",
                        model->IndexCount(),
                        static_cast<double>(world.M41),
                        static_cast<double>(world.M42),
                        static_cast<double>(world.M43));
        }

        if (!model->HasGeometry()) {
            NoteSkip("model-without-geometry");
            return;
        }
        if (!Renderer.DrawIndexed(model->Buffers(), ToRowMajor(modelViewProjection))) {
            NoteSkip("draw-submit-failed");
            return;
        }
        ++SubmittedDrawables;
    }

    ::RuntimeModel* Ps3RenderManager3D::BuildModelFromRaw(::ModelAsset* data) {
        if (data == nullptr) {
            he_cpp_raise(new ::ArgumentNullException("data"));
        }

        Ps3RuntimeModel* model = new Ps3RuntimeModel();
        Array<::float3>* positions = data->Positions;
        const std::uint32_t vertexCount = positions == nullptr ? 0u : static_cast<std::uint32_t>(positions->get_Length());

        // Indices arrive as 16 or 32 bit. The RSX draw below is 16 bit, so a 32-bit array is
        // accepted only while every value fits; a larger mesh is refused by name rather than
        // silently truncated into scrambled triangles.
        Array<uint16_t>* indices16 = data->Indices16;
        Array<uint32_t>* indices32 = data->Indices32;
        const std::uint32_t indexCount = indices16 != nullptr
            ? static_cast<std::uint32_t>(indices16->get_Length())
            : (indices32 == nullptr ? 0u : static_cast<std::uint32_t>(indices32->get_Length()));

        if (vertexCount == 0 || indexCount == 0) {
            std::printf("PS3_CORE_MODEL_EMPTY vertices=%u indices=%u\n", vertexCount, indexCount);
            return model;
        }
        if (vertexCount > 0xFFFFu) {
            std::printf("PS3_CORE_MODEL_TOO_LARGE vertices=%u limit=65535\n", vertexCount);
            return model;
        }

        Ps3Vertex* vertices = new Ps3Vertex[vertexCount];
        for (std::uint32_t index = 0; index < vertexCount; ++index) {
            const ::float3 position = (*positions)[static_cast<int32_t>(index)];
            vertices[index].Position[0] = position.X;
            vertices[index].Position[1] = position.Y;
            vertices[index].Position[2] = position.Z;
            // The vertex program has no material constant yet, so geometry is uploaded white
            // and the cooked material colour is not applied. This is visible in the log.
            vertices[index].Color[0] = 1.0f;
            vertices[index].Color[1] = 1.0f;
            vertices[index].Color[2] = 1.0f;
            vertices[index].Color[3] = 1.0f;
        }

        u16* meshIndices = new u16[indexCount];
        bool indicesFit = true;
        for (std::uint32_t index = 0; index < indexCount && indicesFit; ++index) {
            const std::uint32_t value = indices16 != nullptr
                ? static_cast<std::uint32_t>((*indices16)[static_cast<int32_t>(index)])
                : (*indices32)[static_cast<int32_t>(index)];
            if (value > 0xFFFFu) {
                indicesFit = false;
                break;
            }
            meshIndices[index] = static_cast<u16>(value);
        }

        Ps3MeshBuffers buffers{ nullptr, 0, nullptr, 0, 0, 0 };
        if (!indicesFit) {
            std::printf("PS3_CORE_MODEL_INDEX_RANGE indices=%u reason=value-exceeds-16-bit\n", indexCount);
        } else if (Renderer.UploadMesh(vertices, vertexCount, meshIndices, indexCount, buffers)) {
            model->AdoptBuffers(buffers);
        }

        delete[] meshIndices;
        delete[] vertices;
        return model;
    }

    ::RuntimeMaterial* Ps3RenderManager3D::BuildMaterialFromCooked(::PlatformMaterialAsset* materialAsset) {
        if (materialAsset == nullptr) {
            he_cpp_raise(new ::ArgumentNullException("materialAsset"));
        }
        if (!LoggedFirstMaterial) {
            LoggedFirstMaterial = true;
            std::printf("PS3_CORE_MATERIAL_FIRST family=%s color=%02X%02X%02X%02X lit=%d\n",
                        materialAsset->RendererFamilyId.c_str(),
                        materialAsset->BaseColorR, materialAsset->BaseColorG,
                        materialAsset->BaseColorB, materialAsset->BaseColorA,
                        materialAsset->Lit ? 1 : 0);
        }
        ++MaterialCounter;
        return new Ps3RuntimeMaterial(
            materialAsset->BaseColorR,
            materialAsset->BaseColorG,
            materialAsset->BaseColorB,
            materialAsset->BaseColorA,
            materialAsset->Lit);
    }

    ::RuntimeMaterial* Ps3RenderManager3D::BuildMaterialFromCooked(std::string cookedAssetPath, ::IContentStreamSource* contentStreamSource) {
        ::Asset* asset = ReadCookedAsset(cookedAssetPath, contentStreamSource, "material");
        ::PlatformMaterialAsset* materialAsset = dynamic_cast<::PlatformMaterialAsset*>(asset);
        if (materialAsset == nullptr) {
            std::printf("PS3_CORE_ERROR stage=BuildMaterialFromCooked path=%s reason=not-a-platform-material\n", cookedAssetPath.c_str());
            he_cpp_raise(new ::NotSupportedException(
                "The PlayStation 3 renderer expects a platform-owned cooked material payload."));
        }
        return BuildMaterialFromCooked(materialAsset);
    }

    ::RuntimeModel* Ps3RenderManager3D::BuildModelFromCooked(std::string cookedAssetPath, ::IContentStreamSource* contentStreamSource) {
        // The asset is read so a missing or malformed model is reported here rather than as
        // an empty draw later. Turning it into RSX buffers is the next milestone, so the
        // model returned carries no geometry yet and the log says so once.
        ::Asset* asset = ReadCookedAsset(cookedAssetPath, contentStreamSource, "model");
        ::ModelAsset* modelAsset = dynamic_cast<::ModelAsset*>(asset);
        if (modelAsset == nullptr) {
            std::printf("PS3_CORE_ERROR stage=BuildModelFromCooked path=%s reason=not-a-model-asset\n", cookedAssetPath.c_str());
            he_cpp_raise(new ::NotSupportedException("The PlayStation 3 renderer expects a cooked model payload."));
        }
        if (!LoggedFirstModel) {
            LoggedFirstModel = true;
            std::printf("PS3_CORE_MODEL_FIRST path=%s\n", cookedAssetPath.c_str());
        }
        ++ModelCounter;
        return BuildModelFromRaw(modelAsset);
    }

    ::Asset* Ps3RenderManager3D::ReadCookedAsset(const std::string& cookedAssetPath, ::IContentStreamSource* contentStreamSource, const char* kind) {
        if (contentStreamSource == nullptr) {
            he_cpp_raise(new ::ArgumentNullException("contentStreamSource"));
        }
        ::Stream* stream = contentStreamSource->OpenRead(cookedAssetPath);
        if (stream == nullptr) {
            std::printf("PS3_CORE_ERROR stage=ReadCookedAsset kind=%s path=%s reason=content-missing\n", kind, cookedAssetPath.c_str());
            he_cpp_raise(new ::NotSupportedException("The PlayStation 3 renderer could not open a cooked asset."));
        }
        ::Asset* asset = ::AssetSerializer::Deserialize(stream);
        stream->Dispose();
        if (asset == nullptr) {
            std::printf("PS3_CORE_ERROR stage=ReadCookedAsset kind=%s path=%s reason=deserialize-returned-null\n", kind, cookedAssetPath.c_str());
            he_cpp_raise(new ::NotSupportedException("The PlayStation 3 renderer could not read a cooked asset."));
        }
        return asset;
    }

    void Ps3RenderManager3D::Dispose() {
        std::printf("PS3_CORE_RENDER3D_DISPOSED draws=%u materials=%u models=%u\n", DrawCounter, MaterialCounter, ModelCounter);
    }
}

#endif
