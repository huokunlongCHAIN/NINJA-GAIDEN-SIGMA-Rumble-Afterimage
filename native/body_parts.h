#pragma once
#include <cstdint>

enum class BodyPart : unsigned { Head = 1, Torso, UpperArm, Forearm, Hand, Thigh, Calf, Foot };

// Draw signatures derived from the default Ryu model nodes.
inline BodyPart ClassifyBodyPart(uint64_t key) {
    switch (key) {
        case 0xb68e68aeb5f7aa68ull:
        case 0x5af0e89eae3b0c9aull:
        case 0x8079ff4d0a964f5aull:
        case 0xd4e50eb876bcf794ull:
        case 0xe6d5fbb16c08b658ull:
        case 0x08da90ff77b08930ull:
        case 0xe1d8f88af22dfcd0ull:
        case 0x8640d3ea946aec60ull:
        case 0x8f9a9ac38c445b38ull:
        case 0xbf6308f3030a221eull:
            return BodyPart::Head;
        case 0x693166a35403de50ull:
        case 0x64ec58757d1e9b86ull:
        case 0x0ce06b21e3d8be38ull:
        case 0x8c1afe7b0c81b8e7ull:
        case 0x40f39189b29e7bdfull:
        case 0x09b07928a7bd41faull:
        case 0x12fd5b1b84dc6666ull:
        case 0xb7cd7fb10aac6538ull:
        case 0x88dc63b0c2626142ull:
        case 0xf8f14e5516e07250ull:
        case 0xe32b2677b61be82eull:
        case 0x2bab4f69b1b7c600ull:
            return BodyPart::Torso;
        case 0x4dc0ca0f283250d1ull:
        case 0xbc2a70154c89a2b1ull:
        case 0x01c6d2d73b9167b1ull:
        case 0x57dc83e7e116cf71ull:
            return BodyPart::UpperArm;
        case 0x078d2aca10c8caaaull:
        case 0xd453dc07e6a77402ull:
        case 0x1baa5b23ef841284ull:
        case 0x106f7c9de2b05822ull:
        case 0xd74ab47c059b920cull:
        case 0x24801c6e12177f58ull:
        case 0xfcc97d165c2a53b4ull:
        case 0x6bf2d2f96b2fbfeeull:
            return BodyPart::Forearm;
        case 0x24be92003f7e3cc6ull:
        case 0x1714166d02abe3baull:
        case 0x7947aa1b50efa20cull:
        case 0x320eb4ae25b6b736ull:
        case 0xba717c44489ea74aull:
        case 0xb2bd905053ab823cull:
        case 0x64f315455133b266ull:
            return BodyPart::Hand;
        case 0xfc1de2dfda7632e8ull:
        case 0x074f25386d737218ull:
        case 0x69faf1778b730c8cull:
        case 0xbce46f77c6f9c444ull:
        case 0x803dcd5384549aa8ull:
        case 0xd8decb09e2d5b93cull:
        case 0x4e140762e04d8aa8ull:
        case 0xfcd2e7de97c5f6c7ull:
            return BodyPart::Thigh;
        case 0x48a744e8d2908b89ull:
        case 0xb092b75d9d4ef758ull:
        case 0x663687d9e35d6010ull:
        case 0xbca9e66a77162cd0ull:
        case 0x6cc370f1f7e534c0ull:
        case 0xc7b8f5e66cbe7000ull:
        case 0xfb36607866079e80ull:
            return BodyPart::Calf;
        case 0x937987937e842fd0ull:
        case 0xe6bce44b4adb76c8ull:
            return BodyPart::Foot;
        default: return BodyPart::Torso;
    }
}

// Material groups from the same model, using its submesh material indices.
enum class GhostMaterial : unsigned { Fabric, Armor, Skin, Glove, GripAtlas };

inline GhostMaterial ClassifyGhostMaterial(uint64_t key) {
    switch (key) {
        // Albedo 33 is the exposed arm and shoulder skin; albedo 31 is the eye opening.
        case 0x4dc0ca0f283250d1ull:
        case 0xbc2a70154c89a2b1ull:
        case 0x01c6d2d73b9167b1ull:
        case 0x57dc83e7e116cf71ull:
        case 0x078d2aca10c8caaaull:
        case 0x6bf2d2f96b2fbfeeull:
        case 0xd4e50eb876bcf794ull:
            return GhostMaterial::Skin;
        // The glove atlas contains both exposed fingers and black fabric.
        case 0x24be92003f7e3cc6ull:
        case 0x1714166d02abe3baull:
        case 0x320eb4ae25b6b736ull:
        case 0xba717c44489ea74aull:
            return GhostMaterial::Glove;
        case 0xd453dc07e6a77402ull:
        case 0x106f7c9de2b05822ull:
        case 0x24801c6e12177f58ull:
        case 0xd74ab47c059b920cull:
        case 0x7947aa1b50efa20cull:
        case 0xb2bd905053ab823cull:
        case 0x663687d9e35d6010ull:
        case 0x6cc370f1f7e534c0ull:
        case 0x64f315455133b266ull:
        case 0x40f39189b29e7bdfull:
        case 0x937987937e842fd0ull:
        case 0x074f25386d737218ull:
        case 0x8f9a9ac38c445b38ull:
        case 0xe6bce44b4adb76c8ull:
            return GhostMaterial::Armor;
        default: return GhostMaterial::Fabric;
    }
}
