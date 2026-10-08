#include "native/Ps3Batch2DCameraRuntime.hpp"

#include "CameraComponent.hpp"
#include "CameraClearSettings.hpp"
#include "AmbientLightComponent.hpp"
#include "ClipRectComponent.hpp"
#include "Entity.hpp"
#include "FontAsset.hpp"
#include "FontChar.hpp"
#include "FontInfo.hpp"
#include "IRenderQueue3D.hpp"
#include "MeshComponent.hpp"
#include "ModelAsset.hpp"
#include "ModelSubmeshAsset.hpp"
#include "IRenderQueue2D.hpp"
#include "ObjectManager.hpp"
#include "RenderTarget.hpp"
#include "RuntimeMaterial.hpp"
#include "RuntimeModel.hpp"
#include "RuntimeTexture.hpp"
#include "SpriteComponent.hpp"
#include "TextComponent.hpp"
#include "TextureAsset.hpp"
#include "TextureAssetAlphaPrecision.hpp"
#include "TextureAssetColorFormat.hpp"
#include "ViewportComponent.hpp"
#include "byte4.hpp"
#include "float3.hpp"
#include "float4.hpp"
#include "int2.hpp"
#include "runtime/array.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <cstring>
#include <limits>
#include <vector>

namespace {
    /// Number of camera-fixture expectations that passed in this process.
    std::uint32_t Passed = 0u;

    /// Number of camera-fixture expectations that failed in this process.
    std::uint32_t Failed = 0u;

    /// Records one independently named assertion for the runner's fresh TTY gate.
    void Expect(bool condition, const char* name) {
        if (condition) {
            ++Passed;
            std::printf("PASS %s\n", name);
        } else {
            ++Failed;
            std::printf("FAIL %s\n", name);
        }
    }

    /// Aborts fixture setup while preserving a concrete stage explanation in the log.
    void Require(bool condition, const char* reason) {
        if (!condition) throw std::runtime_error(reason);
    }

    /// Catches the expected native or generated failure and verifies its diagnostic identifies the rejected context.
    void ExpectContextFailure(const std::function<void()>& action, const char* context,
        const char* name) {
        bool matched = false;
        try {
            action();
        } catch (::Exception* exception) {
            const char* reason = exception == nullptr ? "" : exception->what();
            matched = std::strstr(reason, context) != nullptr;
            std::printf("PS3_BATCH2D_CAMERA_REJECTION expected=%s actual=%s\n", context, reason);
            delete exception;
        } catch (const std::exception& exception) {
            matched = std::strstr(exception.what(), context) != nullptr;
            std::printf("PS3_BATCH2D_CAMERA_REJECTION expected=%s actual=%s\n", context, exception.what());
        }
        Expect(matched, name);
    }

    /// Creates one Core-registered entity with initialized child/component collections.
    ::Entity* CreateEntity(::Core& core, ::Entity* parent, std::uint16_t layer,
        float x = 0.0f, float y = 0.0f) {
        ::Entity* entity = new ::Entity(&core);
        entity->InitChildren();
        entity->InitComponents();
        entity->set_LayerMask(layer);
        entity->set_Position(::float3(x, y, 0.0f));
        if (parent != nullptr) parent->AddChild(entity);
        return entity;
    }

    /// Creates one actual registered camera component on the supplied Core entity.
    ::CameraComponent* AttachCamera(::Entity& entity, std::uint8_t order,
        std::uint16_t layer, const ::CameraClearSettings& clear) {
        ::CameraComponent* camera = new ::CameraComponent();
        camera->set_CameraDrawOrder(order);
        camera->set_LayerMask(layer);
        camera->set_ClearSettings(clear);
        entity.AddComponent(camera);
        return camera;
    }

    /// Adds a generated clip component whose rectangle follows its owning entity's world position.
    ::ClipRectComponent* AttachClipRect(::Entity& entity, std::int32_t width, std::int32_t height) {
        ::ClipRectComponent* clip = new ::ClipRectComponent();
        clip->set_Size(::int2(width, height));
        entity.AddComponent(clip);
        return clip;
    }

    /// Adds one real generated sprite component bound to a previously realized runtime texture.
    ::SpriteComponent* AttachSprite(::Entity& entity, ::RuntimeTexture* texture,
        ::int2 size, ::byte4 color = ::byte4(255u, 255u, 255u, 255u)) {
        ::SpriteComponent* sprite = new ::SpriteComponent();
        sprite->set_Texture(texture);
        sprite->set_Size(size);
        sprite->set_Color(color);
        entity.AddComponent(sprite);
        return sprite;
    }

    /// Creates one actual generated texture asset with a solid RGBA texel repeated across its image.
    ::TextureAsset* CreateSolidTexture(std::uint8_t red, std::uint8_t green,
        std::uint8_t blue, std::uint8_t alpha) {
        constexpr std::int32_t width = 4;
        constexpr std::int32_t height = 4;
        ::TextureAsset* asset = new ::TextureAsset();
        asset->Width = static_cast<std::uint16_t>(width);
        asset->Height = static_cast<std::uint16_t>(height);
        asset->ColorFormat = ::TextureAssetColorFormat::Rgba32;
        asset->AlphaPrecision = ::TextureAssetAlphaPrecision::A8;
        asset->Colors = new ::Array<std::uint8_t>(width * height * 4);
        for (std::int32_t pixel = 0; pixel < width * height; ++pixel) {
            (*asset->Colors)[pixel * 4] = red;
            (*asset->Colors)[pixel * 4 + 1] = green;
            (*asset->Colors)[pixel * 4 + 2] = blue;
            (*asset->Colors)[pixel * 4 + 3] = alpha;
        }
        return asset;
    }

    /// Checks a predetermined displayed pixel and reports the observed RGB channels alongside its label.
    void ExpectPixel(helengine::ps3::Ps3Video& video, unsigned x, unsigned y,
        unsigned red, unsigned green, unsigned blue, const char* name) {
        unsigned packed = 0u;
        const bool readable = video.ReadDisplayedPixel(x, y, &packed);
        const unsigned actualRed = (packed >> 16u) & 0xFFu;
        const unsigned actualGreen = (packed >> 8u) & 0xFFu;
        const unsigned actualBlue = packed & 0xFFu;
        const bool matched = readable &&
            std::abs(static_cast<int>(actualRed) - static_cast<int>(red)) <= 4 &&
            std::abs(static_cast<int>(actualGreen) - static_cast<int>(green)) <= 4 &&
            std::abs(static_cast<int>(actualBlue) - static_cast<int>(blue)) <= 4;
        std::printf("PS3_BATCH2D_CAMERA_PIXEL name=%s sample=(%u,%u) expected=(%u,%u,%u) actual=%s(%u,%u,%u)\n",
            name, x, y, red, green, blue, readable ? "" : "unavailable/",
            actualRed, actualGreen, actualBlue);
        Expect(matched, name);
    }

    /// Exercises generated argument guards and the native wrapper/byte-view validations without issuing RSX work.
    void TestTextureUpdateRejections(Ps3Batch2DCameraRuntime& runtime,
        ::RuntimeTexture* nativeTexture) {
        ::RuntimeTexture foreignTexture;
        foreignTexture.set_Width(1);
        foreignTexture.set_Height(1);
        ::Array<std::uint8_t>* fourBytes = new ::Array<std::uint8_t>(4);
        const unsigned foreignOffset = runtime.VideoService().CommandWriteOffset();
        ExpectContextFailure([&runtime, &foreignTexture, fourBytes]() {
            runtime.Manager2DService().UpdateTextureRegion(&foreignTexture, 0, 0, 1, 1, fourBytes, 4);
        }, "foreign RuntimeTexture", "foreign-texture-update-rejected-by-value");
        Expect(runtime.VideoService().CommandWriteOffset() == foreignOffset,
            "foreign-texture-update-emits-no-rsx-commands");

        ::Array<std::uint8_t>* malformedBytes = new ::Array<std::uint8_t>();
        malformedBytes->Length = 4;
        const unsigned malformedOffset = runtime.VideoService().CommandWriteOffset();
        ExpectContextFailure([&runtime, nativeTexture, malformedBytes]() {
            runtime.Manager2DService().UpdateTextureRegion(nativeTexture, 0, 0, 1, 1,
                malformedBytes, 4);
        }, "invalid managed byte array", "texture-update-rejects-nonempty-array-with-null-data");
        Expect(runtime.VideoService().CommandWriteOffset() == malformedOffset,
            "malformed-texture-update-emits-no-rsx-commands");

        ::Array<std::uint8_t>* shortBytes = new ::Array<std::uint8_t>(3);
        ExpectContextFailure([&runtime, nativeTexture, fourBytes]() {
            runtime.Manager2DService().UpdateTextureRegion(nativeTexture, 0, 0, 1, 1,
                fourBytes, 3);
        }, "row pitch", "native-boundary-rejects-row-pitch-smaller-than-rgba-pixel");
        ExpectContextFailure([&runtime, nativeTexture, shortBytes]() {
            runtime.Manager2DService().UpdateTextureRegion(nativeTexture, 0, 0, 1, 1,
                shortBytes, 4);
        }, "Source buffer", "generated-boundary-rejects-short-texture-update-buffer");
        delete shortBytes;
        delete malformedBytes;
        delete fourBytes;
    }

    /// Exercises real Core queues, native 2D/3D ordering, clipping, dynamic glyphs, and no-clear preservation.
    void TestCameraRoutingAndNoClear(Ps3Batch2DCameraRuntime& runtime) {
        ::Core& core = runtime.Core();
        auto* cameras = core.get_ObjectManager()->get_Cameras();
        ::Entity* root = CreateEntity(core, nullptr, 0xFFFFu);
        ::Entity* firstCameraOwner = CreateEntity(core, root, 0xFFFFu);
        ::Entity* secondCameraOwner = CreateEntity(core, root, 0xFFFFu);
        ::Entity* thirdCameraOwner = CreateEntity(core, root, 0xFFFFu);
        ::Entity* disabledCameraOwner = CreateEntity(core, root, 0xFFFFu);
        firstCameraOwner->set_Position(::float3(0.0f, 0.0f, 5.0f));
        thirdCameraOwner->set_Position(::float3(0.0f, 0.0f, 5.0f));
        disabledCameraOwner->set_Enabled(false);

        const ::CameraClearSettings firstClear(true, ::float4(0.0f, 0.0f, 0.2f, 1.0f),
            true, 1.0f, false, 0u);
        const ::CameraClearSettings noClear(false, ::float4(0.0f), false, 1.0f, false, 0u);
        const ::CameraClearSettings thirdClear(false, ::float4(0.0f), true, 1.0f, false, 0u);
        ::CameraComponent* firstCamera = AttachCamera(*firstCameraOwner, 0u, 0x0001u, firstClear);
        ::CameraComponent* secondCamera = AttachCamera(*secondCameraOwner, 1u, 0x0002u, noClear);
        ::CameraComponent* thirdCamera = AttachCamera(*thirdCameraOwner, 2u, 0x0008u, thirdClear);
        ::CameraComponent* disabledCamera = AttachCamera(*disabledCameraOwner, 3u, 0xFFFFu, noClear);

        ::AmbientLightComponent* ambient = new ::AmbientLightComponent();
        ambient->set_Color(::float4(1.0f));
        ambient->set_Intensity(1.0f);
        root->AddComponent(ambient);

        Expect(cameras->get_Count() == 3 && cameras->get_Item(0) == firstCamera &&
            cameras->get_Item(1) == secondCamera && cameras->get_Item(2) == thirdCamera,
            "generated-camera-registration-order-and-disabled-exclusion");
        disabledCameraOwner->set_Enabled(true);
        Expect(cameras->get_Count() == 4 && cameras->get_Item(3) == disabledCamera,
            "generated-camera-enabled-transition-registers-camera");
        disabledCameraOwner->set_Enabled(false);
        Expect(cameras->get_Count() == 3, "generated-camera-disabled-transition-unregisters-camera");

        ::TextureAsset* asset = CreateSolidTexture(255u, 0u, 0u, 255u);
        ::RuntimeTexture* redTexture = runtime.Manager2DService().BuildTextureFromRaw(asset);
        asset->Dispose();
        delete asset;
        TestTextureUpdateRejections(runtime, redTexture);
        asset = CreateSolidTexture(0u, 255u, 0u, 255u);
        ::RuntimeTexture* greenTexture = runtime.Manager2DService().BuildTextureFromRaw(asset);
        asset->Dispose();
        delete asset;
        ::RuntimeTexture* cookedTexture = runtime.Manager2DService().BuildTextureFromCooked(
            "cooked/imported/01199c4588eaad6253c3c88a9ce1182f48d94477c36225fe4124b060c21fc679",
            &runtime.ContentSourceService());
        Expect(cookedTexture != nullptr && cookedTexture->get_Width() > 0 && cookedTexture->get_Height() > 0,
            "manager-loads-real-cooked-texture-through-standard-content-source");

        ::RuntimeModel* cubeModel = runtime.Manager3DService().BuildModelFromCooked(
            "cooked/engine/models/cube.hasset", &runtime.ContentSourceService());
        ::RuntimeMaterial* cubeMaterial = runtime.Manager3DService().BuildMaterialFromCooked(
            "cooked/materials/rendering/cube_test/cubetestsolid.hasset",
            &runtime.ContentSourceService());
        ::Entity* meshOwner = CreateEntity(core, root, 0x0009u, 0.0f, 0.0f);
        ::MeshComponent* cube = new ::MeshComponent();
        cube->set_Model(cubeModel);
        ::Array<::RuntimeMaterial*>* materialArray = new ::Array<::RuntimeMaterial*>(1);
        (*materialArray)[0] = cubeMaterial;
        cube->set_Materials(materialArray);
        delete materialArray;
        meshOwner->AddComponent(cube);

        ::Entity* viewportExcludedOwner = CreateEntity(core, root, 0x0001u, 160.0f, 20.0f);
        ::ViewportComponent* viewportBinding = new ::ViewportComponent();
        viewportBinding->set_BindingMode(::ViewportComponent::ExplicitCameraBindingMode);
        viewportBinding->set_BoundCameraComponent(disabledCamera);
        viewportExcludedOwner->AddComponent(viewportBinding);
        AttachSprite(*viewportExcludedOwner, redTexture, ::int2(36, 36));

        ::Entity* disabledSpriteOwner = CreateEntity(core, root, 0x0001u, 200.0f, 20.0f);
        disabledSpriteOwner->set_Enabled(false);
        AttachSprite(*disabledSpriteOwner, redTexture, ::int2(36, 36));

        ::Entity* outerClipOwner = CreateEntity(core, root, 0x0001u, 20.0f, 120.0f);
        AttachClipRect(*outerClipOwner, 90, 90);
        ::Entity* innerClipOwner = CreateEntity(core, outerClipOwner, 0x0001u, 30.0f, 30.0f);
        AttachClipRect(*innerClipOwner, 35, 35);
        ::Entity* clippedSpriteOwner = CreateEntity(core, innerClipOwner, 0x0001u);
        AttachSprite(*clippedSpriteOwner, redTexture, ::int2(100, 100));

        ::Entity* emptyClipOwner = CreateEntity(core, root, 0x0001u, 200.0f, 120.0f);
        AttachClipRect(*emptyClipOwner, 0, 50);
        ::Entity* emptyClipSpriteOwner = CreateEntity(core, emptyClipOwner, 0x0001u);
        AttachSprite(*emptyClipSpriteOwner, greenTexture, ::int2(50, 50));

        ::TextureAsset* atlasAsset = CreateSolidTexture(0u, 0u, 0u, 0u);
        ::RuntimeTexture* atlasTexture = runtime.Manager2DService().BuildTextureFromRaw(atlasAsset);
        atlasAsset->Dispose();
        delete atlasAsset;
        auto* glyphs = new ::Dictionary<char, ::FontChar>();
        glyphs->Add('A', ::FontChar(::float4(0.0f, 0.0f, 1.0f, 1.0f), 0.0f, 32.0f, 0.0f, 0.0f));
        const std::int32_t disposedFontsBefore = ::FontAsset::get_DisposedInstanceCount();
        ::FontAsset* font = new ::FontAsset(new ::FontInfo("camera-fixture", 32, 8.0f),
            atlasTexture, glyphs, 32.0f, 4, 4);
        ::Entity* textOwner = CreateEntity(core, root, 0x0001u, 220.0f, 300.0f);
        ::TextComponent* text = new ::TextComponent();
        text->set_Font(font);
        text->set_FontScale(1.0f);
        text->set_Size(::int2(32, 32));
        text->set_Color(::byte4(255u, 255u, 255u, 255u));
        text->set_ShadowOffset(::float2(-1.0f, -1.0f));
        text->set_ShadowColor(::byte4(0u, 0u, 0u, 255u));
        text->set_Text("A");
        textOwner->AddComponent(text);

        ::Entity* cookedTextureOwner = CreateEntity(core, root, 0x0001u, 150.0f, 20.0f);
        AttachSprite(*cookedTextureOwner, cookedTexture, ::int2(36, 36));
        const float centerX = static_cast<float>(runtime.VideoService().Width()) * 0.5f;
        const float centerY = static_cast<float>(runtime.VideoService().Height()) * 0.5f;
        ::Entity* secondCameraCenterOwner = CreateEntity(core, root, 0x0002u,
            centerX - 96.0f, centerY - 64.0f);
        AttachSprite(*secondCameraCenterOwner, greenTexture, ::int2(192, 128));

        // Force both generated manager defaults through the same Raster-owned retirement path.
        Require(runtime.Manager2DService().get_PixelTexture() != nullptr &&
            runtime.Manager2DService().get_BlackPixelTexture() != nullptr,
            "generated-manager-default-textures-could-not-be-created");

        ::Entity* redOwner = CreateEntity(core, root, 0x0001u, 12.0f, 12.0f);
        ::Entity* greenOwner = CreateEntity(core, root, 0x0002u, 72.0f, 12.0f);
        ::Entity* excludedOwner = CreateEntity(core, root, 0x0004u, 132.0f, 12.0f);
        AttachSprite(*redOwner, redTexture, ::int2(36, 36));
        AttachSprite(*greenOwner, greenTexture, ::int2(36, 36));
        AttachSprite(*excludedOwner, redTexture, ::int2(36, 36));

        Expect(firstCamera->get_RenderQueue2D()->get_Count() == 5 &&
            secondCamera->get_RenderQueue2D()->get_Count() == 2 &&
            thirdCamera->get_RenderQueue2D()->get_Count() == 0 &&
            firstCamera->get_RenderQueue3D()->get_Count() == 1 &&
            secondCamera->get_RenderQueue3D()->get_Count() == 0 &&
            thirdCamera->get_RenderQueue3D()->get_Count() == 1,
            "authoritative-queues-route-layer-bound-disabled-and-real-3d-content");
        auto* updateBytes = new ::Array<std::uint8_t>(4 * 4 * 4);
        for (std::int32_t index = 0; index < updateBytes->get_Length(); ++index) (*updateBytes)[index] = 255u;
        runtime.Manager2DService().UpdateTextureRegion(atlasTexture, 0, 0, 4, 4, updateBytes, 16);
        delete updateBytes;
        Expect(!atlasTexture->get_IsDisposed(), "dynamic-font-atlas-updated-before-shared-text-emission");

        const unsigned invalidViewportOffset = runtime.VideoService().CommandWriteOffset();
        const ::float4 validSecondViewport = secondCamera->get_Viewport();
        secondCamera->set_Viewport(::float4(std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f, 1.0f));
        ExpectContextFailure([&runtime, secondCamera]() {
            runtime.Manager2DService().RenderCamera(secondCamera);
        }, "viewport", "nonfinite-viewport-rejected-with-context");
        Expect(runtime.VideoService().CommandWriteOffset() == invalidViewportOffset,
            "invalid-viewport-does-not-submit-rsx-commands");
        secondCamera->set_Viewport(validSecondViewport);

        Require(runtime.VideoService().BeginFrame(true), "fixture could not begin its camera-routing frame");
        const unsigned rasterDrawsBefore = runtime.RasterService().DrawCount();
        core.Draw();
        Require(runtime.VideoService().Present(), "camera-routing frame present failed");
        Require(runtime.VideoService().BeginFrame(true), "camera-routing frame completion wait failed");
        Expect(runtime.Manager2DService().DrawCount() == 3u,
            "native-manager-submits-each-generated-camera-once-in-camera-order");
        Expect(runtime.RasterService().DrawCount() == rasterDrawsBefore + 2u,
            "real-authored-3d-draw-runs-after-intervening-ui-only-camera");
        ExpectPixel(runtime.VideoService(), 20u, 20u, 255u, 0u, 0u,
            "first-camera-red-sprite-survives-following-no-clear-camera");
        ExpectPixel(runtime.VideoService(), 80u, 20u, 0u, 255u, 0u,
            "second-ui-only-camera-adds-green-sprite-without-clearing");
        ExpectPixel(runtime.VideoService(), 400u, 300u, 0u, 0u, 51u,
            "second-camera-disabled-clear-preserves-first-camera-background");
        ExpectPixel(runtime.VideoService(), 60u, 160u, 255u, 0u, 0u,
            "nested-clip-chain-renders-inside-pixel-center-intersection");
        ExpectPixel(runtime.VideoService(), 90u, 160u, 0u, 0u, 51u,
            "nested-clip-chain-excludes-pixels-outside-inner-clip");
        ExpectPixel(runtime.VideoService(), 210u, 130u, 0u, 0u, 51u,
            "empty-clip-region-preserves-camera-clear-pixel");
        ExpectPixel(runtime.VideoService(), 221u, 301u, 255u, 255u, 255u,
            "shared-text-emitter-samples-dynamically-updated-font-atlas");
        ExpectPixel(runtime.VideoService(), 219u, 299u, 0u, 0u, 0u,
            "shared-text-emitter-renders-opaque-black-shadow-quad");
        ExpectPixel(runtime.VideoService(), 218u, 298u, 0u, 0u, 51u,
            "shadow-offset-preserves-pixel-outside-actual-quad");
        ExpectPixel(runtime.VideoService(), runtime.VideoService().Width() / 2u,
            runtime.VideoService().Height() / 2u, 255u, 255u, 255u,
            "third-camera-cube-overwrites-intervening-ui-green-pixel-after-depth-clear");

        auto* alphaMask = new ::Array<std::uint8_t>(4 * 4 * 4);
        for (std::int32_t y = 0; y < 4; ++y) {
            for (std::int32_t x = 0; x < 4; ++x) {
                const std::int32_t pixel = (y * 4 + x) * 4;
                (*alphaMask)[pixel] = 255u;
                (*alphaMask)[pixel + 1] = 255u;
                (*alphaMask)[pixel + 2] = 255u;
                (*alphaMask)[pixel + 3] = (x == 1 && y == 1) ? 255u : 0u;
            }
        }
        runtime.Manager2DService().UpdateTextureRegion(atlasTexture, 0, 0, 4, 4, alphaMask, 16);
        delete alphaMask;

        // The previous Present/BeginFrame pair left this second diagnostic frame open.
        core.Draw();
        Require(runtime.VideoService().Present(), "transparent-glyph shadow diagnostic frame present failed");
        Require(runtime.VideoService().BeginFrame(true), "transparent-glyph shadow diagnostic frame did not drain");
        ExpectPixel(runtime.VideoService(), 220u, 300u, 0u, 0u, 0u,
            "transparent-main-glyph-preserves-opaque-shadow-pixel");
        ExpectPixel(runtime.VideoService(), 221u, 301u, 255u, 255u, 255u,
            "single-opaque-atlas-texel-renders-white-main-glyph");
        ExpectPixel(runtime.VideoService(), 219u, 299u, 0u, 0u, 51u,
            "transparent-shadow-and-main-preserve-exterior-clear-pixel");

        text->set_ShadowOffset(::float2(-8.0f, -8.0f));
        core.Draw();
        Require(runtime.VideoService().Present(), "separated transparent-shadow diagnostic frame present failed");
        Require(runtime.VideoService().BeginFrame(true), "separated transparent-shadow diagnostic frame did not drain");
        ExpectPixel(runtime.VideoService(), 213u, 293u, 0u, 0u, 0u,
            "separated-transparent-glyph-black-shadow-pixel");
        ExpectPixel(runtime.VideoService(), 221u, 301u, 255u, 255u, 255u,
            "separated-transparent-glyph-white-main-pixel");
        ExpectPixel(runtime.VideoService(), 212u, 292u, 0u, 0u, 51u,
            "separated-transparent-shadow-preserves-exterior-clear-pixel");

        text->set_ShadowColor(::byte4(255u, 0u, 0u, 255u));
        core.Draw();
        Require(runtime.VideoService().Present(), "separated red-shadow diagnostic frame present failed");
        Require(runtime.VideoService().BeginFrame(true), "separated red-shadow diagnostic frame did not drain");
        ExpectPixel(runtime.VideoService(), 213u, 293u, 255u, 0u, 0u,
            "separated-transparent-glyph-red-shadow-pixel");
        ExpectPixel(runtime.VideoService(), 221u, 301u, 255u, 255u, 255u,
            "separated-red-shadow-white-main-pixel");

        ::RenderTarget offscreenTarget;
        secondCamera->set_RenderTarget(&offscreenTarget);
        const unsigned commandOffset = runtime.VideoService().CommandWriteOffset();
        ExpectContextFailure([&runtime, secondCamera]() {
            runtime.Manager2DService().RenderCamera(secondCamera);
        }, "RenderTarget", "offscreen-camera-target-rejected-with-context");
        Expect(runtime.VideoService().CommandWriteOffset() == commandOffset,
            "offscreen-camera-target-rejected-before-submission");
        secondCamera->set_RenderTarget(nullptr);

        firstCameraOwner->set_Position(::float3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 5.0f));
        Require(runtime.VideoService().BeginFrame(true), "fixture could not begin invalid-matrix diagnostic frame");
        ExpectContextFailure([&core]() { core.Draw(); }, "nonfinite",
            "nonfinite-camera-matrix-fails-with-camera-context");
        Require(runtime.VideoService().Present(), "invalid-matrix diagnostic frame could not be presented");
        Require(runtime.VideoService().BeginFrame(true), "invalid-matrix diagnostic frame did not drain");
        firstCameraOwner->set_Position(::float3(0.0f, 0.0f, 5.0f));

        root->Dispose();
        delete root;
        runtime.Manager2DService().ReleaseTexture(redTexture);
        runtime.Manager2DService().ReleaseTexture(greenTexture);
        runtime.Manager2DService().ReleaseTexture(cookedTexture);
        runtime.Manager2DService().ReleaseFont(font);
        Expect(::FontAsset::get_DisposedInstanceCount() == disposedFontsBefore + 1,
            "generated-releasefont-disposes-font-and-atlas-exactly-once");
        cubeModel->Dispose();
        delete cubeModel;
        cubeMaterial->Dispose();
        delete cubeMaterial;
    }
}

/// Initializes the actual PS3 stack, runs each camera assertion, then checks explicit drain cleanup.
int main() {
    Ps3Batch2DCameraRuntime runtime;
    try {
        Require(runtime.Start(), "camera fixture could not initialize video, raster, or generated Core");
        TestCameraRoutingAndNoClear(runtime);
    } catch (::Exception* exception) {
        ++Failed;
        std::printf("FAIL generated-exception=%s\n", exception == nullptr ? "(null)" : exception->what());
        delete exception;
    } catch (const std::exception& exception) {
        ++Failed;
        std::printf("FAIL exception=%s\n", exception.what());
    } catch (...) {
        ++Failed;
        std::printf("FAIL unknown-exception\n");
    }
    const bool shutdown = runtime.Shutdown();
    Expect(shutdown, "manager-callback-detached-and-raster-gpu-owners-retired-after-drain");
    Expect(runtime.Owned2DBytesAfterDrain() == 0u,
        "2d-textures-including-defaults-retire-once-after-final-drain");
    std::printf("PS3_BATCH2D_CAMERA_TESTS passed=%u failed=%u\n", Passed, Failed);
    return Failed == 0u ? 0 : 1;
}
