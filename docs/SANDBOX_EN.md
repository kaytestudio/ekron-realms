**Ekron — Urban Sandbox**

**Agreed Direction**
A first-person Android game featuring free exploration of a fictional wartime city and building mechanics. There are no quests, story campaign, objective markers, or mandatory sequence of actions.
Visual reference: `concepts/ekron-war-city-ru.png`. This is concept art, not an engine screenshot. The quests and distance marker shown on it are NOT part of the game.

**World**

* Partially destroyed city: intact blocks, damaged buildings, rubble, and abandoned cars.
* Accessible buildings have real entrances and interiors, not just facades.
* Furniture and building elements can be placed inside rooms.
* An open world is the target direction; the first technical prototype is limited to a single small city block.

**Sandbox**

* Free movement with no quests, timers, or mandatory objectives.
* Building catalog: wall, floor, window, door, table, chair, wardrobe/cabinet, sofa.
* Item selection, translucent placement preview, rotation, placement confirmation, and cancellation.
* Selection and deletion of player-placed items.
* Saving and loading placed objects.
* In the first prototype, building items are available without resource gathering: this is a starting technical decision, not the finalized in-game economy.

**Controls and Language**

* UI, item names, and tooltips in four languages: English, Russian, European Portuguese, and Spanish (Spain). The language follows the system setting (on desktop also `--lang`, `EKRON_LANG`, the `L` key, and the pause menu).
* Phone in landscape orientation.
* Left side — movement joystick; right side — look area and buttons for interact, jump, and build.
* In build mode — item selection, rotation, placement, and cancellation.
* Do not display a quest log or mission markers.

**First Verifiable Implementation Milestone**

1. Runnable Android build with movement and camera controls.
2. Small city block with collisions and one accessible interior.
3. Placement, rotation, and deletion of a single type of building object.
4. State saving and restoration after a restart.
5. Expanding the catalog and populating the city block.

**Scope of Current Changes**
The browser prototype has been removed: the user confirmed that changes are needed directly in the Android game. The existing native project `sys/android` was modified: GameActivity → JNI → C++/OpenGL ES 3. Added a small city block with accessible interiors, movement with collisions, touch-based camera look, eight types of placeable items, rotation, deletion, and saving to a private app file. WebView, JavaScript, and npm are not used in the Android build. These changes apply to the Android codebase branch; the desktop Pascal engine is unaffected. Instructions: `sys/android/README.md`.
Graphics currently consist of primitives and do not match the level of detail in the concept art. The generated image does not replace 3D models, textures, and mobile optimization. The Android build has not been tested yet. Multiplayer, full building destructibility, and combat NPCs are not agreed upon at this stage.