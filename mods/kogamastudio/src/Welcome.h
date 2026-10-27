#pragma once

namespace Welcome {
    void Init();       // Call once at startup to check first-run state
    void Render();     // Call every frame (renders welcome window if needed)
    bool IsOpen();     // True while welcome window is showing
}
