#pragma once

namespace ailo {

class App;

// Asteroids in 3D: the ship flies freely inside a wrap-around cube of space, shoots the rocks, and the rocks split
// into smaller ones until the smallest are destroyed. Clearing every rock starts the next, larger wave.
// Requires: PlatformPlugin, AssetPlugin, RenderPlugin, ImGuiPlugin.
// Resources: AsteroidsGame (assets, game state), AsteroidsInput.
// Input:   mouse turns the ship (the cursor is captured while playing), LMB fires, W or Space thrusts. Esc releases
//          the cursor and pauses; Esc while paused quits. Click to play, and to restart after game over (or Enter).
// Startup: loads the ship, rocks, materials and sky; spawns the camera, the ship and the first wave.
// Update:  ship, rocks, lasers and particles; collisions, score, lives and waves; the follow camera and the HUD.
struct AsteroidsPlugin {
    void build(App& app);
};

}
