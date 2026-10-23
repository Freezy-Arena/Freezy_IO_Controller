#pragma once
#include <stdint.h>

// Called only from the main loop / its WebSocket callbacks. Timings are latched
// at boot; each new rising edge captures the result and restarts the sequence.
class WinnerDisplay {
public:
    void configure(uint32_t delayMs, uint32_t durationMs) {
        _delayMs = delayMs;
        _durationMs = durationMs;
    }

    void observe(bool red, bool blue, uint32_t now) {
        const bool redRising = red && !_previousRed;
        const bool blueRising = blue && !_previousBlue;
        const bool rising = redRising || blueRising;
        _previousRed = red;
        _previousBlue = blue;
        if (!rising || !_durationMs) return;
        _red = red;
        _blue = blue;
        // The strobe belongs to the coil that most recently asserted. If both
        // assert in the same snapshot there is no unambiguous alliance winner.
        _allianceWinnerRed = redRising != blueRising && redRising;
        _allianceWinnerBlue = redRising != blueRising && blueRising;
        _edgeAt = now;
        _phase = Phase::Waiting;
    }

    bool update(uint32_t now) {
        if (_phase == Phase::Waiting && uint32_t(now - _edgeAt) >= _delayMs) {
            _phase = Phase::Showing;
            _shownAt = now; // Give the display its full duration after it starts.
        }
        if (_phase == Phase::Showing && uint32_t(now - _shownAt) >= _durationMs)
            _phase = Phase::Idle;
        return _phase == Phase::Showing;
    }

    bool showRed(bool redHub) const { return _red && _blue ? redHub : _red; }
    bool activeForAlliance(bool redAlliance) const {
        return _phase == Phase::Showing &&
               (redAlliance ? _allianceWinnerRed : _allianceWinnerBlue);
    }

private:
    enum class Phase { Idle, Waiting, Showing };
    Phase _phase = Phase::Idle;
    bool _previousRed = false, _previousBlue = false;
    bool _red = false, _blue = false;
    bool _allianceWinnerRed = false, _allianceWinnerBlue = false;
    uint32_t _delayMs = 0, _durationMs = 0, _edgeAt = 0, _shownAt = 0;
};
