#include "platform/ps3/Ps3GtfTexture.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

/// <summary>Checks managed GTF fixtures against the actual native parser and rejects mutated upload descriptors.</summary>
int main(int argumentCount, char** arguments) {
    assert(argumentCount == 2);
    std::size_t count = 0u;
    for (const auto& entry : std::filesystem::directory_iterator(arguments[1])) {
        if (entry.path().extension() != ".gtf") continue;
        std::ifstream stream(entry.path(), std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        helengine::ps3::Ps3GtfTexture texture;
        std::string error;
        assert(helengine::ps3::Ps3GtfTexture::TryParse(bytes.data(), bytes.size(), texture, error));
        assert(texture.Width == 8u && texture.Height == 4u);
        assert(texture.Pixels == bytes.data() + 128u && texture.PixelBytes == bytes.size() - 128u);
        const auto original = texture;
        assert(!helengine::ps3::Ps3GtfTexture::TryParse(bytes.data(), bytes.size() - 1u, texture, error));
        assert(texture.Pixels == original.Pixels && texture.PixelBytes == original.PixelBytes);
        for (const std::size_t offset : {0u, 8u, 16u, 20u, 25u, 26u, 27u, 28u, 32u, 36u}) {
            auto corrupt = bytes;
            corrupt[offset] = 0xffu;
            assert(!helengine::ps3::Ps3GtfTexture::TryParse(corrupt.data(), corrupt.size(), texture, error));
        }
        auto corrupt = bytes; corrupt[24u] |= 0x40u;
        assert(!helengine::ps3::Ps3GtfTexture::TryParse(corrupt.data(), corrupt.size(), texture, error));
        corrupt = bytes; corrupt[24u] &= 0x7fu;
        assert(!helengine::ps3::Ps3GtfTexture::TryParse(corrupt.data(), corrupt.size(), texture, error));
        ++count;
    }
    assert(count == 94u);
    std::cout << "PS3_GTF_NATIVE_TESTS_PASSED fixtures=" << count << " malformed-descriptors=13-per-fixture\n";
}
