#pragma once
#include <cstdint>

namespace input {
    // Sample once per rendered frame so every input API sees identical edges.
    struct ButtonEdges {
        uint32_t frame = 0;
        bool sampled = false, held = false, pressed = false, released = false;
        void Sample(uint32_t now, bool requested) {
            if (sampled && frame == now) return;
            sampled = true;
            frame = now;
            pressed = requested && !held;
            released = !requested && held;
            held = requested;
        }
        bool Value(int edge) const {
            return edge == 1 ? pressed : edge == 2 ? released : held;
        }
    };

    // UI and weapon code share Unity's mouse APIs. UI must see physical input
    // only, including the frame in which an automated press is released.
    struct UiInputScope {
        static inline thread_local unsigned depth = 0;
        UiInputScope() { ++depth; }
        ~UiInputScope() { --depth; }
        UiInputScope(const UiInputScope&) = delete;
        static bool Filter(bool physical, bool automated) {
            return physical || (!depth && automated);
        }
    };
}
