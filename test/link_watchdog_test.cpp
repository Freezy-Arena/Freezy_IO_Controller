// Compile-time behavioral tests: run with any C++14 compiler, including the
// installed ESP32 toolchain (-std=c++14 -fsyntax-only). No hardware required.
#include "../src/network/link_watchdog.h"
using namespace ethwatch;

constexpr bool steadyTrafficNeverRestarts() {
    Watchdog w;
    for (uint32_t t = 1000; t < 120000; t += 200) {
        if (w.due(true, t, t + 150)) return false; // Something every 200 ms.
    }
    return w.restarts() == 0;
}
constexpr bool stalledReceiveRestartsOnce() {
    // 11-21: traffic until 543 s, then nothing while the link stays up.
    Watchdog w;
    if (w.due(true, 0, 1000)) return false;
    const uint32_t lastRx = 543000;
    if (w.due(true, lastRx, lastRx + RxSilenceMs - 1)) return false;
    if (!w.due(true, lastRx, lastRx + RxSilenceMs)) return false;
    w.restarted(lastRx + RxSilenceMs);
    return w.restarts() == 1;
}
constexpr bool silenceCountsFromLinkUp() {
    // A board booting with no arena traffic yet waits a full interval after the link comes up.
    Watchdog w;
    if (w.due(false, 0, 0) || w.due(false, 0, 50000)) return false;
    if (w.due(true, 0, 60000)) return false;               // Link just came up.
    if (w.due(true, 0, 60000 + RxSilenceMs - 1)) return false;
    return w.due(true, 0, 60000 + RxSilenceMs);
}
constexpr bool restartsAreSpacedOut() {
    // An arena that is down sends nothing: restart, then no more than once per backoff.
    Watchdog w;
    if (w.due(true, 0, 0)) return false;                     // Link up at boot.
    if (!w.due(true, 0, RxSilenceMs)) return false;
    w.restarted(RxSilenceMs);
    if (w.due(false, 0, RxSilenceMs + 1000)) return false;  // Link drops during the restart,
    const uint32_t back = RxSilenceMs + 3000;                // and comes back.
    if (w.due(true, 0, back)) return false;
    if (w.due(true, 0, back + RxSilenceMs)) return false;    // Quiet again, but inside the backoff.
    return w.due(true, 0, RxSilenceMs + RestartBackoffMs);
}
constexpr bool receivingAfterRestartClearsIt() {
    Watchdog w;
    w.restarted(10000);
    w.due(true, 0, 13000);
    return !w.due(true, 13500, 13500 + RxSilenceMs - 1);
}
constexpr bool millisWrapIsHandled() {
    Watchdog w;
    const uint32_t lastRx = 0xFFFFF000u;
    if (w.due(true, lastRx, lastRx)) return false;
    return !w.due(true, lastRx, lastRx + 5000) && w.due(true, lastRx, lastRx + RxSilenceMs);
}

static_assert(steadyTrafficNeverRestarts(), "Steady traffic must never restart Ethernet");
static_assert(stalledReceiveRestartsOnce(), "A stalled receive path must restart Ethernet after RxSilenceMs");
static_assert(silenceCountsFromLinkUp(), "Silence must count from when the link comes up");
static_assert(restartsAreSpacedOut(), "Restarts must be at least RestartBackoffMs apart");
static_assert(receivingAfterRestartClearsIt(), "Receiving again must clear the watchdog");
static_assert(millisWrapIsHandled(), "millis() wrap must not trigger or block a restart");
