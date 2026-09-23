#include "platform/ps3/Ps3RenderManager3D.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdio>
#include <cmath>

#include "AssetSerializer.hpp"
#include "Core.hpp"
#include "AmbientLightComponent.hpp"
#include "CameraClearSettings.hpp"
#include "DirectionalLightComponent.hpp"
#include "Entity.hpp"
#include "ICamera.hpp"
#include "IContentStreamSource.hpp"
#include "IDrawable3D.hpp"
#include "IRenderQueue3D.hpp"
#include "LightDirectionUtility.hpp"
#include "ObjectManager.hpp"
#include "ModelAsset.hpp"
#include "ModelAssetIndexData.hpp"
#include "PlatformMaterialAsset.hpp"
#include "float3.hpp"
#include "float4.hpp"
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

        /// Converts one normalized channel into an RSX color byte, mapping non-finite values to zero.
        u32 ToColorByte(float value) {
            if (!std::isfinite(value) || value <= 0.0f) {
                return 0;
            }
            if (value >= 1.0f) {
                return 255;
            }
            return static_cast<u32>(value * 255.0f + 0.5f);
        }

        /// Packs the authored RGBA camera clear color into RSX A8R8G8B8 order.
        u32 PackCameraColor(const ::float4& color) {
            return (ToColorByte(color.W) << 24) |
                   (ToColorByte(color.X) << 16) |
                   (ToColorByte(color.Y) << 8) |
                   ToColorByte(color.Z);
        }

        /// Packs normalized depth and stencil into the RSX Z24S8 clear register.
        u32 PackCameraDepthStencil(float depth, u8 stencil) {
            if (!std::isfinite(depth) || depth <= 0.0f) {
                depth = 0.0f;
            } else if (depth >= 1.0f) {
                depth = 1.0f;
            }
            const u32 depth24 = static_cast<u32>(static_cast<double>(depth) * 16777215.0 + 0.5);
            return (depth24 << 8) | static_cast<u32>(stencil);
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
        AmbientLight[0] = 0.2f;
        AmbientLight[1] = 0.2f;
        AmbientLight[2] = 0.2f;
        AmbientLight[3] = 1.0f;
        DirectionalLight[0] = 0.0f;
        DirectionalLight[1] = 0.0f;
        DirectionalLight[2] = 0.0f;
        DirectionalLight[3] = 1.0f;
        DirectionalDirection[0] = 0.0f;
        DirectionalDirection[1] = -1.0f;
        DirectionalDirection[2] = 0.0f;
        DirectionalDirection[3] = 0.0f;
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
            Video.ClearDefaultSurface();
            NoteSkip("no-object-manager");
            return;
        }
        ::List<::ICamera*>* cameras = core->get_ObjectManager()->get_Cameras();
        if (cameras == nullptr || cameras->get_Count() == 0) {
            Video.ClearDefaultSurface();
            NoteSkip("no-camera");
            return;
        }
        ::ICamera* camera = cameras->get_Item(0);
        if (camera == nullptr) {
            Video.ClearDefaultSurface();
            NoteSkip("camera-incomplete");
            return;
        }

        ::CameraClearSettings clearSettings = camera->get_ClearSettings();
        const ::float4 clearColor = clearSettings.get_ClearColor();
        if (!Video.ClearSurface(clearSettings.get_ClearColorEnabled(),
                                clearSettings.get_ClearDepthEnabled(),
                                clearSettings.get_ClearStencilEnabled(),
                                PackCameraColor(clearColor),
                                PackCameraDepthStencil(clearSettings.get_ClearDepth(), clearSettings.get_ClearStencil()))) {
            NoteSkip("camera-clear-failed");
            return;
        }
        if (camera->get_Parent() == nullptr || camera->get_RenderQueue3D() == nullptr) {
            NoteSkip("camera-incomplete");
            return;
        }

        AmbientLight[0] = 0.2f;
        AmbientLight[1] = 0.2f;
        AmbientLight[2] = 0.2f;
        AmbientLight[3] = 1.0f;
        DirectionalLight[0] = 0.0f;
        DirectionalLight[1] = 0.0f;
        DirectionalLight[2] = 0.0f;
        DirectionalLight[3] = 1.0f;
        DirectionalDirection[0] = 0.0f;
        DirectionalDirection[1] = -1.0f;
        DirectionalDirection[2] = 0.0f;
        DirectionalDirection[3] = 0.0f;

        ::List<::AmbientLightComponent*>* ambientLights = core->get_ObjectManager()->get_AmbientLights();
        if (ambientLights != nullptr && ambientLights->get_Count() > 0) {
            ::AmbientLightComponent* light = ambientLights->get_Item(0);
            if (light != nullptr) {
                const ::float4 color = light->get_Color();
                const float intensity = light->get_Intensity();
                AmbientLight[0] = color.X * intensity;
                AmbientLight[1] = color.Y * intensity;
                AmbientLight[2] = color.Z * intensity;
                AmbientLight[3] = 1.0f;
            }
        }
        ::List<::DirectionalLightComponent*>* directionalLights = core->get_ObjectManager()->get_DirectionalLights();
        if (directionalLights != nullptr && directionalLights->get_Count() > 0) {
            ::DirectionalLightComponent* light = directionalLights->get_Item(0);
            if (light != nullptr) {
                const ::float4 color = light->get_Color();
                const float intensity = light->get_Intensity();
                const ::float3 direction = ::LightDirectionUtility::GetLightDirection(light);
                DirectionalLight[0] = color.X * intensity;
                DirectionalLight[1] = color.Y * intensity;
                DirectionalLight[2] = color.Z * intensity;
                DirectionalLight[3] = 1.0f;
                DirectionalDirection[0] = direction.X;
                DirectionalDirection[1] = direction.Y;
                DirectionalDirection[2] = direction.Z;
                DirectionalDirection[3] = 0.0f;
            }
        }
        if (DrawCounter == 1) {
            std::printf("PS3_CORE_LIGHTING ambient=%.3f,%.3f,%.3f directional=%.3f,%.3f,%.3f direction=%.3f,%.3f,%.3f\n",
                        static_cast<double>(AmbientLight[0]), static_cast<double>(AmbientLight[1]), static_cast<double>(AmbientLight[2]),
                        static_cast<double>(DirectionalLight[0]), static_cast<double>(DirectionalLight[1]), static_cast<double>(DirectionalLight[2]),
                        static_cast<double>(DirectionalDirection[0]), static_cast<double>(DirectionalDirection[1]), static_cast<double>(DirectionalDirection[2]));
            std::printf("PS3_CORE_CAMERA_CLEAR enabled=%d rgba=%.3f,%.3f,%.3f,%.3f depthEnabled=%d depth=%.3f stencilEnabled=%d stencil=%u\n",
                        clearSettings.get_ClearColorEnabled() ? 1 : 0,
                        static_cast<double>(clearColor.X), static_cast<double>(clearColor.Y),
                        static_cast<double>(clearColor.Z), static_cast<double>(clearColor.W),
                        clearSettings.get_ClearDepthEnabled() ? 1 : 0,
                        static_cast<double>(clearSettings.get_ClearDepth()),
                        clearSettings.get_ClearStencilEnabled() ? 1 : 0,
                        static_cast<unsigned>(clearSettings.get_ClearStencil()));
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
        ::Array<::RuntimeMaterial*>* materials = drawable->get_Materials();
        Ps3RuntimeMaterial* material = nullptr;
        if (materials != nullptr && materials->get_Length() > 0) {
            material = dynamic_cast<Ps3RuntimeMaterial*>((*materials)[0]);
            if (material == nullptr) {
                NoteSkip("drawable-without-ps3-material");
                return;
            }
        }
        const bool lit = material != nullptr && material->Lit;
        if (lit && !model->HasUsableNormals()) {
            NoteSkip("lit-model-without-usable-normals");
            return;
        }
        const float materialColor[4] = {
            material == nullptr ? 1.0f : static_cast<float>(material->BaseColorR) / 255.0f,
            material == nullptr ? 1.0f : static_cast<float>(material->BaseColorG) / 255.0f,
            material == nullptr ? 1.0f : static_cast<float>(material->BaseColorB) / 255.0f,
            material == nullptr ? 1.0f : static_cast<float>(material->BaseColorA) / 255.0f
        };
        ::float4x4 normalTransform = ::float4x4::get_Identity();
        if (lit && !::float4x4::TryInverseTranspose__ref0_out1(world, normalTransform)) {
            NoteSkip("lit-draw-transform-not-invertible");
            return;
        }
        const Ps3Matrix4 packedModelViewProjection = ToRowMajor(modelViewProjection);
        if (!Renderer.DrawIndexed(model->Buffers(), packedModelViewProjection, ToRowMajor(normalTransform),
                                  materialColor, lit, AmbientLight, DirectionalLight, DirectionalDirection)) {
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

        // The engine already resolves which index width a model uses and validates that only
        // one is populated, so that decision is not repeated here. RSX draws either width.
        ::ModelAssetIndexData* indexData = ::ModelAssetIndexData::Resolve(data);
        const std::uint32_t indexCount = indexData == nullptr ? 0u : static_cast<std::uint32_t>(indexData->get_IndexCount());
        const bool uses32BitIndices = indexData != nullptr && indexData->get_Uses32BitIndices();

        if (vertexCount == 0 || indexCount == 0) {
            std::printf("PS3_CORE_MODEL_EMPTY vertices=%u indices=%u\n", vertexCount, indexCount);
            return model;
        }

        Ps3Vertex* vertices = new Ps3Vertex[vertexCount];
        Array<::float3>* normals = data->Normals;
        bool usableNormals = normals != nullptr && static_cast<std::uint32_t>(normals->get_Length()) == vertexCount;
        for (std::uint32_t index = 0; index < vertexCount; ++index) {
            const ::float3 position = (*positions)[static_cast<int32_t>(index)];
            vertices[index].Position[0] = position.X;
            vertices[index].Position[1] = position.Y;
            vertices[index].Position[2] = position.Z;
            if (usableNormals) {
                const ::float3 normal = (*normals)[static_cast<int32_t>(index)];
                const float normalLengthSquared = normal.X * normal.X + normal.Y * normal.Y + normal.Z * normal.Z;
                if (!std::isfinite(normal.X) || !std::isfinite(normal.Y) || !std::isfinite(normal.Z) ||
                    !std::isfinite(normalLengthSquared) || normalLengthSquared <= 1.0e-12f) {
                    usableNormals = false;
                    vertices[index].Normal[0] = 0.0f;
                    vertices[index].Normal[1] = 0.0f;
                    vertices[index].Normal[2] = 1.0f;
                } else {
                    vertices[index].Normal[0] = normal.X;
                    vertices[index].Normal[1] = normal.Y;
                    vertices[index].Normal[2] = normal.Z;
                }
            } else {
                vertices[index].Normal[0] = 0.0f;
                vertices[index].Normal[1] = 0.0f;
                vertices[index].Normal[2] = 1.0f;
            }
            // The lit path multiplies this vertex tint by the cooked material color.
            vertices[index].Color[0] = 1.0f;
            vertices[index].Color[1] = 1.0f;
            vertices[index].Color[2] = 1.0f;
            vertices[index].Color[3] = 1.0f;
        }
        model->SetHasUsableNormals(usableNormals);

        // Copied into a flat array because the generated arrays are not guaranteed contiguous
        // in a form RSX can read directly.
        Ps3MeshBuffers buffers{ nullptr, 0, nullptr, 0, 0, false, 0 };
        if (uses32BitIndices) {
            Array<uint32_t>* source = indexData->get_Indices32();
            uint32_t* meshIndices = new uint32_t[indexCount];
            for (std::uint32_t index = 0; index < indexCount; ++index) {
                meshIndices[index] = (*source)[static_cast<int32_t>(index)];
            }
            if (Renderer.UploadMesh(vertices, vertexCount, meshIndices, indexCount, true, buffers)) {
                model->AdoptBuffers(buffers);
            }
            delete[] meshIndices;
        } else {
            Array<uint16_t>* source = indexData->get_Indices16();
            uint16_t* meshIndices = new uint16_t[indexCount];
            for (std::uint32_t index = 0; index < indexCount; ++index) {
                meshIndices[index] = (*source)[static_cast<int32_t>(index)];
            }
            if (Renderer.UploadMesh(vertices, vertexCount, meshIndices, indexCount, false, buffers)) {
                model->AdoptBuffers(buffers);
            }
            delete[] meshIndices;
        }

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
