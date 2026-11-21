#include "atmosphere.h"
#include <algorithm>

namespace atmosphere {
namespace {
    constexpr float Pi = 3.14159265f;
    constexpr Palette palettes[] = {
        {"Aurora", "Violet dusk, mint aurora and drifting sparks",
            {.045f,.012f,.16f}, {.48f,.18f,.72f}, {.12f,1.f,.72f}, {.36f,.16f,.58f}, {.55f,1.f,.85f}},
        {"Ember", "Copper light, a warm horizon and rising embers",
            {.16f,.018f,.035f}, {1.f,.30f,.085f}, {1.f,.64f,.12f}, {.70f,.19f,.09f}, {1.f,.78f,.30f}},
        {"Glacier", "Blue hour, ice crystals and silver haze",
            {.012f,.055f,.21f}, {.18f,.65f,.92f}, {.30f,.94f,1.f}, {.19f,.48f,.72f}, {.68f,.94f,1.f}},
    };
    float Unit(float v) { return std::clamp(v, 0.f, 1.f); }
    float Wave(float x) { return .5f + .5f * std::sin(x); }
    float Fraction(float x) { return x - std::floor(x); }
    float Sample(unsigned i) {
        i ^= i >> 16; i *= 0x7feb352du;
        i ^= i >> 15; i *= 0x846ca68bu; i ^= i >> 16;
        return static_cast<float>(i & 0xffffffu) / 16777216.f;
    }
    float Noise(float u, float v, int frequency) {
        const float x = u * frequency, y = v * frequency;
        const int col = static_cast<int>(std::floor(x)), row = static_cast<int>(std::floor(y));
        const float dx = x - col, dy = y - row;
        const auto sample = [frequency](int c, int r) {
            int wrap = c % frequency;
            if (wrap < 0) wrap += frequency;
            return Sample(unsigned(wrap)*733u + unsigned(r)*12211u + unsigned(frequency)*37u);
        };
        const float tx = dx*dx*(3.f-2.f*dx), ty = dy*dy*(3.f-2.f*dy);
        return std::lerp(std::lerp(sample(col,row),sample(col+1,row),tx),
                         std::lerp(sample(col,row+1),sample(col+1,row+1),tx),ty);
    }
    float Clouds(float u, float elevation, int palette) {
        const float v = elevation * .64f;
        const float flow = Noise(u,v,5) * .065f;
        const float n = .55f * Noise(u+flow,v,13) + .31f * Noise(u,v+flow,29) + .14f * Noise(u,v,61);
        const float coverage = palette == 2 ? .38f : .48f;
        return Unit((n-coverage)*4.3f) * Unit(elevation*6.f) * Unit((1.f-elevation)*8.f);
    }
    Col Mix(Col a, Col b, float f) {
        f = Unit(f);
        return {std::lerp(a.r,b.r,f), std::lerp(a.g,b.g,f), std::lerp(a.b,b.b,f)};
    }
    ImU32 Ink(Col c, float alpha = 1.f) {
        return ImGui::ColorConvertFloat4ToU32({c.r, c.g, c.b, Unit(alpha)});
    }
    void Gradient(ImDrawList& draw, ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom) {
        draw.AddRectFilledMultiColor(a, b, top, top, bottom, bottom);
    }
    float Ribbon(float angle, int layer) {
        return .40f + layer * .116f + .10f * std::sin(angle * 2.f + layer)
            + .038f * std::sin(angle * 5.f + layer * 1.4f);
    }
}

const Palette& Colors(int index) { return palettes[std::clamp(index, 0, 2)]; }

void Panorama(std::vector<uint8_t>& pixels, int width, int height, int index, bool stars) {
    const auto& palette = Colors(index);
    pixels.resize(static_cast<size_t>(width) * height * 4);
    for (int x = 0; x < width; ++x) {
        const float u = float(x) / width;
        const float angle = u * 2.f * Pi;
        const float folds = .32f + .68f * Wave(angle * 22.f + 2.f * std::sin(angle * 8.f));
        const float low = Ribbon(angle, 0), high = Ribbon(angle, 1);
        const float moonX = (u - .72f) * 3.04f;
        for (int y = 0; y < height; ++y) {
            // Texture rows start at the nadir. The equator is y = height/2.
            const float elevation = (float(y) / (height - 1) - .5f) * 2.f;
            const float horizon = std::pow(1.f - std::abs(elevation), 5.f);
            Col color = Mix(palette.zenith, palette.horizon, horizon * (.76f + .20f * Wave(angle * 3.f)));
            if (elevation > .03f) {
                const float a = (elevation - low) * 15.f;
                const float b = (elevation - high) * 11.f;
                const float glow = std::exp(-a*a) * .78f + std::exp(-b*b) * .48f;
                color = Mix(color, palette.aurora, glow * folds);
                const float cloud = Clouds(u,elevation,index);
                color = Mix(color,Mix(palette.zenith,palette.horizon,.36f+.32f*cloud),
                            cloud*(index == 2 ? .98f : .82f));
            }
            const float moonY = elevation - .45f;
            const float moonDistance = std::sqrt(moonX * moonX + moonY * moonY);
            color = Mix(color, palette.sparks, Unit(1.f - moonDistance / .17f) * .25f);
            if (moonDistance < .041f) color = Mix(color, palette.sparks, Unit((.041f - moonDistance) * 410.f) * .84f);
            auto* px = &pixels[(static_cast<size_t>(y) * width + x) * 4];
            px[0] = static_cast<uint8_t>(Unit(color.r) * 255.f);
            px[1] = static_cast<uint8_t>(Unit(color.g) * 255.f);
            px[2] = static_cast<uint8_t>(Unit(color.b) * 255.f);
            px[3] = 255;
        }
    }
    if (!stars) return;
    for (unsigned i = 1; i <= 1800; ++i) {
        const int x = int(Sample(i * 13) * width);
        const int y = int((.55f + Sample(i * 17) * .43f) * height);
        const float cloud = Clouds(float(x)/width,(float(y)/(height-1)-.5f)*2.f,index);
        const float light = (.24f + .70f * Sample(i * 23)) * (1.f-cloud*.92f);
        auto* px = &pixels[(static_cast<size_t>(y) * width + x) * 4];
        px[0] = static_cast<uint8_t>(std::lerp(float(px[0]), 224.f, light));
        px[1] = static_cast<uint8_t>(std::lerp(float(px[1]), 236.f, light));
        px[2] = static_cast<uint8_t>(std::lerp(float(px[2]), 255.f, light));
    }
}

void Preview(ImDrawList& draw, ImVec2 origin, ImVec2 size, const WorldCfg& settings, double time) {
    const auto& p = Colors(settings.palette);
    const ImVec2 end{origin.x + size.x, origin.y + size.y};
    auto point = [&](float x, float y) { return ImVec2{origin.x+x*size.x, origin.y+y*size.y}; };
    draw.PushClipRect(origin, end, true);
    Gradient(draw, origin, end, Ink(p.zenith), Ink(Mix(p.zenith, p.horizon, .78f)));
    if (settings.stars) {
        for (unsigned i = 1; i <= 106; ++i) {
            const auto pt = point(Sample(i * 3), Sample(i * 7) * .70f);
            const float twinkle = .24f + .62f * Wave(static_cast<float>(time) * .65f + i * 2.1f);
            draw.AddCircleFilled(pt, .6f + 1.1f * Sample(i * 17), Ink(p.sparks, twinkle), 8);
        }
    }
    if (settings.sky) {
        const auto moon = point(.79f, .28f);
        const float radius = std::clamp(size.y * .097f, 10.f, 34.f);
        for (int i = 10; i > 0; --i)
            draw.AddCircleFilled(moon, radius + i * 3.1f, Ink(p.sparks, .016f), 36);
        draw.AddCircleFilled(moon, radius, Ink(Mix(p.sparks, {1,1,1}, .6f)), 36);
        for (int ribbon = 0; ribbon < 2; ++ribbon) {
            for (int i = 0; i < 64; ++i) {
                const float x = i / 64.f, next = (i + 1) / 64.f;
                auto height = [&](float u) { return .89f - Ribbon(u * 4.2f + .06f * float(time), ribbon); };
                const auto a = point(x, height(x)), b = point(next, height(next));
                const float strength = .036f + .068f * Wave(x * 47.f + ribbon);
                const auto tip = Ink(p.aurora, strength);
                draw.PrimReserve(6, 4);
                const ImVec2 uv = ImGui::GetIO().Fonts->TexUvWhitePixel;
                const auto index = static_cast<ImDrawIdx>(draw._VtxCurrentIdx);
                draw.PrimWriteIdx(index); draw.PrimWriteIdx(index+1); draw.PrimWriteIdx(index+2);
                draw.PrimWriteIdx(index); draw.PrimWriteIdx(index+2); draw.PrimWriteIdx(index+3);
                draw.PrimWriteVtx(a,uv,tip); draw.PrimWriteVtx(b,uv,tip);
                draw.PrimWriteVtx({b.x,b.y-size.y*.21f},uv,Ink(p.aurora,0));
                draw.PrimWriteVtx({a.x,a.y-size.y*.21f},uv,Ink(p.aurora,0));
            }
        }
    }
    const auto flags = draw.Flags;
    draw.Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (int ridge = 0; ridge < 3; ++ridge) {
        const float depth = .63f + .089f * ridge;
        const auto color = Ink(Mix(p.horizon, p.zenith, .50f + .18f * ridge));
        auto height = [&](float x) {
            return depth + .071f * std::sin(x * (13.f + ridge * 6.f) + ridge)
                         + .028f * std::sin(x * 43.f + ridge * 2.f);
        };
        for (int i = 0; i < 58; ++i) {
            const float x = i / 58.f, next = (i + 1) / 58.f;
            draw.AddQuadFilled(point(x,height(x)),point(next,height(next)),point(next,1),point(x,1),color);
        }
    }
    draw.Flags = flags;
    if (settings.fog)
        Gradient(draw, point(0,.71f), end, Ink(p.mist,0), Ink(p.mist, .22f));
    if (settings.particles) {
        for (unsigned i = 1; i <= 26; ++i) {
            const float x = Fraction(Sample(i * 57) + float(time) * .018f);
            const float y = .55f + .44f * Fraction(Sample(i * 13) - float(time) * .041f);
            draw.AddCircleFilled(point(x,y), 1.f + Sample(i * 11), Ink(p.sparks, .32f));
        }
    }
    draw.PopClipRect();
}

}
