#pragma once
#include <stdint.h>

// Detects an Ethernet receive path that has stopped while the link still
// looks up. On 11-21 a hub's W5500 stopped receiving for about a minute while
// it kept sending: the arena saw its frames, the board saw nothing back, not
// even ARP. Restarting the Ethernet driver hard-resets the W5500 through
// ETH_PHY_RST, without rebooting the board, so counters and stop state
// survive.
namespace ethwatch {
// Nothing received for this long while the link is up means receiving has
// stopped. The arena sends a hub plcIoChange every 200 ms, acknowledges every
// stop input within 100 ms, sends "ping" every few seconds, and answers the
// websocket heartbeat; 10 s is far past all of them.
constexpr uint32_t RxSilenceMs = 10000;
// At most one driver restart per this interval, so an arena that is simply
// down does not keep the board's link bouncing.
constexpr uint32_t RestartBackoffMs = 30000;

// Wrap-safe elapsed time.
constexpr uint32_t since(uint32_t now, uint32_t then) { return uint32_t(now - then); }

class Watchdog {
public:
    // lastRx: millis() of the last byte received from the arena (0 if none yet).
    // Returns true when the Ethernet driver should be restarted now.
    constexpr bool due(bool linkUp, uint32_t lastRx, uint32_t now) {
        if (!linkUp) { _wasUp = false; return false; }
        if (!_wasUp) { _wasUp = true; _upSince = now; }
        // Silence counts from the later of the last receive and the link coming up.
        uint32_t quiet = since(now, _upSince);
        if (lastRx != 0 && since(now, lastRx) < quiet) quiet = since(now, lastRx);
        if (quiet < RxSilenceMs) return false;
        if (_restarts && since(now, _lastRestart) < RestartBackoffMs) return false;
        return true;
    }
    constexpr void restarted(uint32_t now) {
        _lastRestart = now;
        ++_restarts;
        _wasUp = false; // The link drops during the restart; count again from when it is back.
    }
    constexpr uint32_t restarts() const { return _restarts; }

private:
    bool _wasUp = false;
    uint32_t _upSince = 0, _lastRestart = 0, _restarts = 0;
};
} // namespace ethwatch
