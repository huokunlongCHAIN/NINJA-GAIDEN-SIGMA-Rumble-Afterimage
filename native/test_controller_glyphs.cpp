#include "controller_glyphs.h"
#include <array>
#include <cassert>
#include <cstdio>

int main() {
    using namespace ControllerGlyphs;
    assert(IsXboxAtlas(512, 32, 0x02248876ea9adceaull));
    assert(IsXboxAtlas(512, 32, 0x4ba8889a287e6c4eull));
    assert(IsXboxAtlas(512, 512, 0xfe7a5c69e24c78ceull));
    assert(IsXboxAtlas(512, 512, 0xc4973fe4f885a6b6ull));
    assert(!IsXboxAtlas(512, 32, 0xfe7a5c69e24c78ceull));
    assert(!IsXboxAtlas(512, 32, 0));

    std::vector<uint8_t> atlas(512 * 32 * 4, 73);
    auto pixel = [](const std::vector<uint8_t>& data, unsigned x, unsigned y) {
        return &data[(y * 512 + x) * 4];
    };
    assert(Render(atlas, 512, 32));
    assert(pixel(atlas, 16, 16)[0] < 100);  // Square center is hollow.
    assert(pixel(atlas, 111, 15)[0] > 210); // Cross intersects at the center.
    assert(pixel(atlas, 79, 15)[0] < 100);  // Circle center is hollow.
    assert(pixel(atlas, 48, 9)[0] > 140);   // Triangle apex.
    for (unsigned tile = 0; tile < 8; ++tile) {
        unsigned lit = 0, soft = 0;
        for (unsigned y = 0; y < 32; ++y)
            for (unsigned x = tile * 32; x < (tile + 1) * 32; ++x) {
                const auto* p = pixel(atlas, x, y);
                lit += p[0] > 220 && p[3] == 255;
                soft += p[3] > 0 && p[3] < 255;
            }
        assert(lit > 8 && soft > 0);
    }
    for (unsigned y = 0; y < 32; ++y)
        assert(std::all_of(pixel(atlas, 256, y), pixel(atlas, 256, y) + 256 * 4,
            [](uint8_t value) { return value == 73; }));

    std::vector<uint8_t> diagram(512 * 512 * 4, 73);
    assert(Render(diagram, 512, 512));
    assert(pixel(diagram, 251, 214)[0] > 210);
    assert(pixel(diagram, 271, 194)[0] < 100);
    assert(pixel(diagram, 131, 242)[0] == 73);
    for (size_t i = 3; i < diagram.size(); i += 4) assert(diagram[i] == 73);
    puts("Built-in face/shoulder prompts and controller guide glyphs verified.");
}
