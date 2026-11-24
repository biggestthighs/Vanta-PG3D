#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace frame_sample {
    constexpr unsigned PatchWidth = 32;
    constexpr unsigned PatchHeight = 16;
    constexpr unsigned PatchCount = 5;
    constexpr unsigned Width = PatchWidth * PatchCount;
    constexpr unsigned Height = PatchHeight;
    constexpr size_t Bytes = Width * Height * 4;
    struct Patch { unsigned x, y; };

    inline Patch SourcePatch(unsigned width, unsigned height, unsigned index) {
        if (width < PatchWidth || height < PatchHeight) return {};
        constexpr std::array<unsigned,PatchCount> xPercent{20,80,50,20,80};
        constexpr std::array<unsigned,PatchCount> yPercent{25,25,50,85,85};
        const auto x = static_cast<int64_t>(width) * xPercent.at(index) / 100 - PatchWidth / 2;
        const auto y = static_cast<int64_t>(height) * yPercent.at(index) / 100 - PatchHeight / 2;
        return {static_cast<unsigned>(std::clamp<int64_t>(x,0,width-PatchWidth)),
                static_cast<unsigned>(std::clamp<int64_t>(y,0,height-PatchHeight))};
    }
    inline uint64_t Hash(const uint8_t* pixels, size_t pitch) {
        if (!pixels || pitch < Width * 4) return 0;
        uint64_t hash = 1469598103934665603ull;
        for (unsigned y=0; y<Height; y+=2) {
            for (unsigned x=0; x<Width; x+=4) {
                const auto* pixel = pixels + y*pitch + x*4;
                for (unsigned channel=0; channel<3; ++channel)
                    hash = (hash ^ (pixel[channel]>>3)) * 1099511628211ull;
            }
        }
        return hash;
    }
}
