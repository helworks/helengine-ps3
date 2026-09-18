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
#include "PlatformMaterialAsset.hpp"
#include "runtime/native_list.hpp"
#include "system/io/stream.hpp"

#include "platform/ps3/Ps3RuntimeAssets.hpp"
#include "platform/ps3/Ps3Video.hpp"

#include "runtime/native_exceptions.hpp"
#include "runtime/native_runtime.hpp"

namespace helengine::ps3 {
    Ps3RenderManager3D::Ps3RenderManager3D(Ps3Video& video)
        : DrawCounter(0)
        , LoggedFirstDraw(false)
        , RaiseOnDraw(false)
        , LoggedFirstMaterial(false)
        , LoggedFirstModel(false)
        , MaterialCounter(0)
        , ModelCounter(0)
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

        // Geometry upload is the next step. The matrix is computed and reported above so the
        // camera path can be checked against a running frame before any RSX buffers exist.
        (void)modelViewProjection;
        NoteSkip("model-without-geometry");
    }

    ::RuntimeModel* Ps3RenderManager3D::BuildModelFromRaw(::ModelAsset* data) {
        (void)data;
        std::printf("PS3_CORE_ERROR stage=BuildModelFromRaw reason=not-supported-in-v02-boot-gate\n");
        he_cpp_raise(new ::NotSupportedException(
            "The PlayStation 3 v0.2 boot gate builds no models. Authored geometry arrives with the scene milestone."));
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
        ReadCookedAsset(cookedAssetPath, contentStreamSource, "model");
        if (!LoggedFirstModel) {
            LoggedFirstModel = true;
            std::printf("PS3_CORE_MODEL_FIRST path=%s note=no-gpu-geometry-yet\n", cookedAssetPath.c_str());
        }
        ++ModelCounter;
        return new Ps3RuntimeModel();
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
