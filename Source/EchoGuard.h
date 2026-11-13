#pragma once
// Notes the plugin sends out (REC -> piano roll via a loopback MIDI port) come straight back in
// as host MIDI. EchoGuard remembers what went out so those copies are recognised and dropped,
// instead of being played twice or bouncing around forever.
#include <array>
#include <cstdint>

struct EchoGuard
{
    struct E { int note = -1; bool on = false; int64_t t = -1; };
    std::array<E, 512> ring {};
    int head = 0;

    void sent (int note, bool on, int64_t t) { ring[(size_t) head] = { note, on, t }; head = (head + 1) % (int) ring.size(); }

    /** true (and forgets it) when this incoming note matches one sent within `window` samples. */
    bool isEcho (int note, bool on, int64_t t, int64_t window)
    {
        for (auto& e : ring)
            if (e.note == note && e.on == on && e.t >= 0 && t - e.t >= -64 && t - e.t <= window)
            { e.t = -1; return true; }
        return false;
    }
};
