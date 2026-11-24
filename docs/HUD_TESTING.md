# Sandbox HUD checks

The maintained sandbox clients are `sys/android`, `sys/ios` (the iOS target),
and `sys/desktop` (Linux/macOS/Windows). The older `game-source` Android tree is a
standalone archived snapshot, not a synchronized client. The Xcode project
has a single target, `realms iOS`.

## On Android and iOS

- Wait for the intro/loading screen to finish. Move with the left stick while
  looking with another finger; verify releasing either finger does not stop the other.
- Tap the tool switch to open the picker, select a tool, and verify its controls
  appear. Hold and drag the switch in each direction to select a tool; drag out
  and back to the center to cancel. Canceling or dismissing the picker must not
  leave movement running.
- Open the catalog, select an item, rotate and place it; test grab, freeze,
  contextual interaction, drone placement and the FPV exit controls.
- Toggle crouch and verify the highlighted button reflects the player state.
  Open and close the pause menu and background/resume the app while a stick is
  held; check that movement does not remain stuck.
- Tap the coordinate display to copy `spawn.json`. Verify its position, yaw in
  degrees, and `startFlying` reflect the current player; elevated walking alone
  must not enable flight.

## On desktop

- Open the catalog with Q and the pause menu with Esc; click tabs, items and
  buttons. Resize the window to a small size and repeat: the drawn buttons and
  their hit targets should still align.
- Verify the controls bar fits, and C descends in noclip mode. Copy `spawn.json`
  with P or from the pause menu and check it uses `enabled`, `position`,
  `yawDegrees`, and `startFlying`.

The native regression tests cover desktop HUD coordinate scaling and spawn
configuration serialization. Platform SDK builds and on-device interaction
checks are still required to validate the actual touch and graphics stacks.
