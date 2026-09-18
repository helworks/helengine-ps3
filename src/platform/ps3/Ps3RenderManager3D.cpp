#include "platform/ps3/Ps3RenderManager3D.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdio>

#include "AssetSerializer.hpp"
#include "IContentStreamSource.hpp"
#include "PlatformMaterialAsset.hpp"
#include "system/io/stream.hpp"

#include "platform/ps3/Ps3RuntimeAssets.hpp"

#include "runtime/native_exceptions.hpp"
#include "runtime/native_runtime.hpp"

namespace helengine::ps3 {
    Ps3RenderManager3D::Ps3RenderManager3D()
        : DrawCounter(0)
        , LoggedFirstDraw(false)
        , RaiseOnDraw(false)
        , LoggedFirstMaterial(false)
        , LoggedFirstModel(false)
        , MaterialCounter(0)
        , ModelCounter(0) {
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
