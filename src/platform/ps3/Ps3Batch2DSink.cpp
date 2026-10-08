#include "platform/ps3/Ps3Batch2DSink.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include "platform/ps3/Ps3Batch2DPrograms.hpp"
#include "platform/ps3/Ps3Batch2DRenderer.hpp"

#include "Batch2DBuffer.hpp"
#include "Batch2DRun.hpp"
#include "float4.hpp"
#include "float4x4.hpp"

#include <stdexcept>

namespace helengine::ps3 {

Ps3Batch2DSink::Ps3Batch2DSink(Ps3Batch2DRenderer& renderer, Ps3Batch2DPrograms& programs)
    : Renderer(renderer), Programs(programs) {
}

Ps3Batch2DSink::~Ps3Batch2DSink() = default;

void Ps3Batch2DSink::SetCamera(::float4x4 projection, ::float4 pixelViewport) {
    Renderer.SetCamera(projection, pixelViewport);
}

void Ps3Batch2DSink::Submit(::Batch2DBuffer* buffer, ::Batch2DRun run) {
    if (buffer == nullptr) throw std::invalid_argument("PS3 2D sink received a null batch buffer");
    Renderer.Submit(Programs, *buffer, run);
}

} // namespace helengine::ps3

#endif
