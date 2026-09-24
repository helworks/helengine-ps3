#include "platform/ps3/Ps3RenderManager3D.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdio>
#include <cmath>
#include <memory>
#include <vector>
#include <algorithm>

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
#include "ModelSubmeshResolver.hpp"
#include "RuntimeSubmesh.hpp"
#include "PlatformMaterialAsset.hpp"
#include "float2.hpp"
#include "float3.hpp"
#include "float4.hpp"
#include "runtime/native_list.hpp"
#include "system/io/stream.hpp"

#include "platform/ps3/Ps3RuntimeAssets.hpp"
#include "platform/ps3/Ps3RasterRenderer.hpp"
#include "platform/ps3/Ps3Vertex.hpp"
#include "platform/ps3/Ps3StandardDrawData.hpp"
#include "platform/ps3/Ps3Video.hpp"

#include "runtime/native_exceptions.hpp"
#include "runtime/native_runtime.hpp"

namespace helengine::ps3 {
    namespace {
        /// Suppresses only secondary disposal errors, retaining the primary exception for the caller.
        /// Generated exception pointers are owned here and deleted after their detail is logged.
        template <typename TResource>
        void DisposeFailedResource(TResource* resource, const char* context) noexcept {
            if (resource == nullptr) return;
            try { resource->Dispose(); }
            catch (::Exception* exception) {
                std::printf("PS3_CORE_ERROR stage=dispose-during-failure context=%s reason=%s\n",
                    context, exception == nullptr ? "null generated exception" : exception->what());
                delete exception;
            } catch (const std::exception& exception) {
                std::printf("PS3_CORE_ERROR stage=dispose-during-failure context=%s reason=%s\n",
                    context, exception.what());
            } catch (...) {
                std::printf("PS3_CORE_ERROR stage=dispose-during-failure context=%s reason=unknown-secondary-exception\n",
                    context);
            }
        }

        /// Packs Core columns into SDK DP4 vectors. The actual StandardShader dump uses
        /// c[4..7] for clip XYZW; the measured asymmetric GPU fixture confirms translation
        /// in each vector's fourth element. No SDK output or shared shader is rewritten.
        Ps3Matrix4 ToSdkVectors(const ::float4x4& matrix) {
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

        /// Emits one named finite SDK-vector sequence for the first submitted draw only.
        void LogSubmittedValues(unsigned frame, const char* name, const float* values, std::size_t count) {
            std::printf("PS3_CORE_AUTHORED_VALUES Frame=%u field=%s values=", frame, name);
            for (std::size_t index = 0; index < count; ++index) {
                std::printf(index == 0 ? "%.9g" : ",%.9g", static_cast<double>(values[index]));
            }
            std::printf("\n");
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
        , LastSkipMarker(nullptr)
        , HasDirectionalLight(false) {
        CameraPosition[0] = CameraPosition[1] = CameraPosition[2] = 0.0f;
        CameraPosition[3] = 1.0f;
        AmbientLight[0] = 0.0f;
        AmbientLight[1] = 0.0f;
        AmbientLight[2] = 0.0f;
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

        HasDirectionalLight = false;
        AmbientLight[0] = 0.0f;
        AmbientLight[1] = 0.0f;
        AmbientLight[2] = 0.0f;
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
                HasDirectionalLight = true;
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
        CameraPosition[0] = cameraWorld.M41;
        CameraPosition[1] = cameraWorld.M42;
        CameraPosition[2] = cameraWorld.M43;
        CameraPosition[3] = 1.0f;
        const float fov = camera->get_FieldOfView();
        const float nearPlane = camera->get_NearPlaneDistance();
        const float farPlane = camera->get_FarPlaneDistance();
        if (!std::isfinite(fov) || fov <= 0.0f || fov >= 3.14159265358979323846f ||
            !std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
            nearPlane <= 0.0f || farPlane <= nearPlane || !std::isfinite(Video.Aspect()) || Video.Aspect() <= 0.0f) {
            he_cpp_raise(new ::NotSupportedException("PS3 authored camera has invalid perspective parameters."));
        }
        // The engine keeps plain inversion private but exposes the inverse transpose, so the
        // view is recovered by transposing that back. Same result, public surface only.
        ::float4x4 inverseTranspose = ::float4x4::get_Identity();
        if (!::float4x4::TryInverseTranspose__ref0_out1(cameraWorld, inverseTranspose)) {
            he_cpp_raise(new ::NotSupportedException("PS3 authored camera transform is not invertible."));
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

    /// Supplies complete current-draw state; authored failures never select the diagnostic shader.
    void Ps3RenderManager3D::Visit(::IDrawable3D* drawable) {
        ++VisitedDrawables;
        if (drawable == nullptr || drawable->get_Parent() == nullptr) {
            he_cpp_raise(new ::NotSupportedException("PS3 authored drawable requires an entity."));
        }
        auto* model = dynamic_cast<Ps3RuntimeModel*>(drawable->get_Model());
        if (model == nullptr || !model->HasGeometry()) {
            he_cpp_raise(new ::NotSupportedException("PS3 authored drawable requires uploaded PS3 geometry."));
        }
        if (!model->HasUsableNormals() || !model->HasUsableTexCoords()) {
            he_cpp_raise(new ::NotSupportedException("ForwardStandard requires finite nonzero normals and finite TEXCOORD0 for every position."));
        }
        auto* materials = drawable->get_Materials();
        if (materials == nullptr || materials->get_Length() != 1) {
            he_cpp_raise(new ::NotSupportedException("PS3 standard drawable requires exactly one authored P3MT material."));
        }
        auto* material = dynamic_cast<Ps3RuntimeMaterial*>((*materials)[0]);
        if (material == nullptr) {
            he_cpp_raise(new ::NotSupportedException("PS3 standard drawable has no realized P3MT material."));
        }
        ::float4x4 world = drawable->get_Parent()->get_WorldTransformMatrix();
        ::float4x4 modelViewProjection;
        ::float4x4::Multiply__ref0_ref1_out2(world, ViewProjection, modelViewProjection);
        ::float4x4 normalTransform;
        if (!::float4x4::TryInverseTranspose__ref0_out1(world, normalTransform)) {
            he_cpp_raise(new ::NotSupportedException("PS3 authored model transform is not invertible."));
        }
        Ps3StandardDrawData draw;
        draw.World = ToSdkVectors(world);
        draw.WorldViewProjection = ToSdkVectors(modelViewProjection);
        draw.NormalMatrix = ToSdkVectors(normalTransform);
        std::copy(CameraPosition, CameraPosition + 4, draw.CameraPosition);
        std::copy(AmbientLight, AmbientLight + 4, draw.AmbientLight);
        std::copy(DirectionalLight, DirectionalLight + 4, draw.DirectionalColor);
        std::copy(DirectionalDirection, DirectionalDirection + 4, draw.DirectionalDirection);
        draw.BaseColor[0] = static_cast<float>(material->Material.BaseColorR) / 255.0f;
        draw.BaseColor[1] = static_cast<float>(material->Material.BaseColorG) / 255.0f;
        draw.BaseColor[2] = static_cast<float>(material->Material.BaseColorB) / 255.0f;
        draw.BaseColor[3] = static_cast<float>(material->Material.BaseColorA) / 255.0f;
        draw.Lit = material->IsLit();
        draw.HasDirectionalLight = HasDirectionalLight;
        draw.DoubleSided = material->IsDoubleSided();
        // Validate even inactive scene fields rather than silently accepting corrupt authored inputs.
        const float* groups[] = {draw.World.Values, draw.WorldViewProjection.Values, draw.NormalMatrix.Values,
            draw.CameraPosition, draw.BaseColor, draw.AmbientLight, draw.DirectionalColor, draw.DirectionalDirection};
        for (std::size_t group = 0; group < 8; ++group) {
            for (std::size_t index = 0; index < (group < 3 ? 16u : 4u); ++index) {
                if (!std::isfinite(groups[group][index])) {
                    he_cpp_raise(new ::NotSupportedException("PS3 authored draw contains a nonfinite transform, material, camera or light input."));
                }
            }
        }
        std::string error;
        if (!Renderer.DrawAuthored(model->Buffers(), *material, draw, error)) {
            std::printf("PS3_CORE_ERROR stage=DrawAuthored material=%s shader=%s reason=%s\n",
                material->Material.MaterialAssetId.c_str(), material->Material.ShaderAssetId.c_str(), error.c_str());
            he_cpp_raise(new ::NotSupportedException("PS3 authored draw: " + error));
        }
        ++SubmittedDrawables;
        if (!LoggedFirstVisit) {
            LoggedFirstVisit = true;
            const unsigned frame = Video.PresentedFrameCount();
            std::printf("PS3_CORE_AUTHORED_DRAW Frame=%u material=%s indices=%u Lit=%d HasDir=%d Double=%d\n",
                frame, material->Material.MaterialAssetId.c_str(), model->IndexCount(),
                draw.Lit ? 1 : 0, draw.HasDirectionalLight ? 1 : 0, draw.DoubleSided ? 1 : 0);
            LogSubmittedValues(frame, "World", draw.World.Values, 16);
            LogSubmittedValues(frame, "WVP", draw.WorldViewProjection.Values, 16);
            LogSubmittedValues(frame, "Normal", draw.NormalMatrix.Values, 16);
            LogSubmittedValues(frame, "Camera", draw.CameraPosition, 4);
            LogSubmittedValues(frame, "Base", draw.BaseColor, 4);
            LogSubmittedValues(frame, "Ambient", draw.AmbientLight, 4);
            LogSubmittedValues(frame, "DirColor", draw.DirectionalColor, 4);
            LogSubmittedValues(frame, "DirDirection", draw.DirectionalDirection, 4);
        }
    }

    /// Copies source geometry without synthesizing missing standard-shader inputs.
    ::RuntimeModel* Ps3RenderManager3D::BuildModelFromRaw(::ModelAsset* data) {
        if (data == nullptr) he_cpp_raise(new ::ArgumentNullException("data"));
        /// Releases generated CPU containers on failure without touching renderer-owned GPU storage.
        const auto disposeModel = [](Ps3RuntimeModel* value) {
            if (value != nullptr) { DisposeFailedResource(value, "partial-runtime-model"); delete value; }
        };
        std::unique_ptr<Ps3RuntimeModel, decltype(disposeModel)> model(new Ps3RuntimeModel(), disposeModel);
        auto* positions = data->Positions;
        const std::uint32_t vertexCount = positions == nullptr ? 0u : static_cast<std::uint32_t>(positions->get_Length());
        std::unique_ptr<::ModelAssetIndexData> indexData(::ModelAssetIndexData::Resolve(data));
        const std::uint32_t indexCount = indexData == nullptr ? 0u : static_cast<std::uint32_t>(indexData->get_IndexCount());
        const bool uses32BitIndices = indexData != nullptr && indexData->get_Uses32BitIndices();
        if (vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0) {
            he_cpp_raise(new ::NotSupportedException("PS3 model requires nonempty triangle-list geometry."));
        }
        // Core resolves authored ranges (or its documented whole-model range), not a PS3-invented default.
        std::unique_ptr<::Array<::RuntimeSubmesh*>> submeshes(::ModelSubmeshResolver::BuildRuntimeSubmeshes(data));
        if (!submeshes) he_cpp_raise(new ::NotSupportedException("Core returned no runtime submesh collection."));
        std::vector<std::unique_ptr<::RuntimeSubmesh>> pendingSubmeshes;
        pendingSubmeshes.reserve(static_cast<std::size_t>(submeshes->get_Length()));
        for (std::int32_t index = 0; index < submeshes->get_Length(); ++index) {
            pendingSubmeshes.emplace_back((*submeshes)[index]);
        }
        if (submeshes->get_Length() != 1 || (*submeshes)[0] == nullptr ||
            (*submeshes)[0]->get_IndexStart() != 0 ||
            (*submeshes)[0]->get_IndexCount() != static_cast<std::int32_t>(indexCount)) {
            he_cpp_raise(new ::NotSupportedException("PS3 standard profile requires one whole-model triangle submesh."));
        }
        model->SetSubmeshes(submeshes.get());
        for (auto& submesh : pendingSubmeshes) submesh.release();
        if (model->get_Submeshes() == nullptr || model->get_Submeshes()->get_Length() != 1) {
            he_cpp_raise(new ::NotSupportedException("PS3 runtime model did not retain its Core submesh collection."));
        }
        std::vector<Ps3Vertex> vertices(vertexCount);
        auto* normals = data->Normals;
        auto* texCoords = data->TexCoords;
        bool usableNormals = normals != nullptr && static_cast<std::uint32_t>(normals->get_Length()) == vertexCount;
        bool usableTexCoords = texCoords != nullptr && static_cast<std::uint32_t>(texCoords->get_Length()) == vertexCount;
        ::float3 minimum = (*positions)[0];
        ::float3 maximum = minimum;
        for (std::uint32_t index = 0; index < vertexCount; ++index) {
            const auto position = (*positions)[static_cast<std::int32_t>(index)];
            if (!std::isfinite(position.X) || !std::isfinite(position.Y) || !std::isfinite(position.Z)) {
                he_cpp_raise(new ::NotSupportedException("PS3 model contains a nonfinite position."));
            }
            minimum.X = std::min(minimum.X, position.X); maximum.X = std::max(maximum.X, position.X);
            minimum.Y = std::min(minimum.Y, position.Y); maximum.Y = std::max(maximum.Y, position.Y);
            minimum.Z = std::min(minimum.Z, position.Z); maximum.Z = std::max(maximum.Z, position.Z);
            vertices[index].Position[0] = position.X;
            vertices[index].Position[1] = position.Y;
            vertices[index].Position[2] = position.Z;
            if (usableNormals) {
                const auto normal = (*normals)[static_cast<std::int32_t>(index)];
                const float lengthSquared = normal.X * normal.X + normal.Y * normal.Y + normal.Z * normal.Z;
                if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12f) usableNormals = false;
                else {
                    vertices[index].Normal[0] = normal.X;
                    vertices[index].Normal[1] = normal.Y;
                    vertices[index].Normal[2] = normal.Z;
                }
            }
            if (usableTexCoords) {
                const auto uv = (*texCoords)[static_cast<std::int32_t>(index)];
                if (!std::isfinite(uv.X) || !std::isfinite(uv.Y)) usableTexCoords = false;
                else {
                    vertices[index].TexCoord[0] = uv.X;
                    vertices[index].TexCoord[1] = uv.Y;
                }
            }
            // Preserved diagnostic layout only: authored standard VS has no color input.
            std::fill(vertices[index].Color, vertices[index].Color + 4, 1.0f);
        }
        model->SetHasUsableNormals(usableNormals);
        model->SetHasUsableTexCoords(usableTexCoords);
        model->SetBounds(minimum, maximum);
        Ps3MeshBuffers buffers{};
        bool uploaded = false;
        if (uses32BitIndices) {
            auto* source = indexData->get_Indices32();
            std::vector<std::uint32_t> indices(indexCount);
            for (std::uint32_t index = 0; index < indexCount; ++index) indices[index] = (*source)[static_cast<std::int32_t>(index)];
            uploaded = Renderer.UploadMesh(vertices.data(), vertexCount, indices.data(), indexCount, true, buffers);
        } else {
            auto* source = indexData->get_Indices16();
            std::vector<std::uint16_t> indices(indexCount);
            for (std::uint32_t index = 0; index < indexCount; ++index) indices[index] = (*source)[static_cast<std::int32_t>(index)];
            uploaded = Renderer.UploadMesh(vertices.data(), vertexCount, indices.data(), indexCount, false, buffers);
        }
        if (!uploaded) he_cpp_raise(new ::NotSupportedException("PS3 authored mesh upload failed; see PS3_MESH_ERROR."));
        model->AdoptBuffers(buffers);
        std::printf("PS3_CORE_MODEL_REALIZED vertices=%u indices=%u indexBits=%u normals=%d texCoords=%d stride=%lu\n",
            vertexCount, indexCount, uses32BitIndices ? 32u : 16u, usableNormals ? 1 : 0, usableTexCoords ? 1 : 0,
            static_cast<unsigned long>(sizeof(Ps3Vertex)));
        return model.release();
    }

    /// Legacy platform materials cannot identify the required authored shader pair.
    ::RuntimeMaterial* Ps3RenderManager3D::BuildMaterialFromCooked(::PlatformMaterialAsset*) {
        he_cpp_raise(new ::NotSupportedException("PS3 authored materials require a P3MT content path and source; PlatformMaterialAsset has no shader identity."));
        return nullptr;
    }

    /// Opens P3MT via the renderer-owned source cache and translates native errors at the Core boundary.
    ::RuntimeMaterial* Ps3RenderManager3D::BuildMaterialFromCooked(std::string cookedAssetPath, ::IContentStreamSource* contentStreamSource) {
        if (contentStreamSource == nullptr) he_cpp_raise(new ::ArgumentNullException("contentStreamSource"));
        std::string error;
        auto* material = Renderer.BuildAuthoredMaterial(cookedAssetPath, *contentStreamSource, error);
        if (material == nullptr) {
            std::printf("PS3_CORE_ERROR stage=BuildMaterialFromCooked path=%s reason=%s\n", cookedAssetPath.c_str(), error.c_str());
            he_cpp_raise(new ::NotSupportedException(error));
        }
        ++MaterialCounter;
        return material;
    }

    /// Realizes serialized model geometry and disposes the temporary source arrays after copying.
    ::RuntimeModel* Ps3RenderManager3D::BuildModelFromCooked(std::string cookedAssetPath, ::IContentStreamSource* contentStreamSource) {
        std::unique_ptr<::Asset> asset(ReadCookedAsset(cookedAssetPath, contentStreamSource, "model"));
        auto* modelAsset = dynamic_cast<::ModelAsset*>(asset.get());
        if (modelAsset == nullptr) he_cpp_raise(new ::NotSupportedException("PS3 cooked model is not a ModelAsset: " + cookedAssetPath));
        std::unique_ptr<::RuntimeModel> model;
        try { model.reset(BuildModelFromRaw(modelAsset)); }
        catch (...) { DisposeFailedResource(modelAsset, cookedAssetPath.c_str()); throw; }
        modelAsset->Dispose();
        if (!LoggedFirstModel) {
            LoggedFirstModel = true;
            std::printf("PS3_CORE_MODEL_FIRST path=%s\n", cookedAssetPath.c_str());
        }
        ++ModelCounter;
        return model.release();
    }

    /// Closes the owned content stream on both deserialization success and exception.
    ::Asset* Ps3RenderManager3D::ReadCookedAsset(const std::string& cookedAssetPath,
        ::IContentStreamSource* contentStreamSource, const char* kind) {
        if (contentStreamSource == nullptr) he_cpp_raise(new ::ArgumentNullException("contentStreamSource"));
        std::unique_ptr<::Stream> stream(contentStreamSource->OpenRead(cookedAssetPath));
        if (!stream) he_cpp_raise(new ::NotSupportedException("PS3 could not open " + std::string(kind) + ": " + cookedAssetPath));
        std::unique_ptr<::Asset> asset;
        try {
            asset.reset(::AssetSerializer::Deserialize(stream.get()));
            auto closing = std::move(stream);
            closing->Dispose();
        } catch (...) {
            if (stream) {
                DisposeFailedResource(stream.get(), cookedAssetPath.c_str());
            }
            // Deserialize may have completed before stream disposal failed; release its owned arrays.
            DisposeFailedResource(dynamic_cast<::ModelAsset*>(asset.get()), cookedAssetPath.c_str());
            throw;
        }
        if (!asset) he_cpp_raise(new ::NotSupportedException("PS3 deserializer returned no " + std::string(kind) + ": " + cookedAssetPath));
        return asset.release();
    }

    void Ps3RenderManager3D::Dispose() {
        std::printf("PS3_CORE_RENDER3D_DISPOSED draws=%u materials=%u models=%u\n", DrawCounter, MaterialCounter, ModelCounter);
    }
}

#endif
