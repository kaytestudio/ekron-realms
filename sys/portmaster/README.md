## Notes

Ekron Realms on PortMaster handhelds (built for the Anbernic RG40XXH:
640x480, Mali-G31, OpenGL ES 3.0). Source: https://github.com/kaytestudio/ekron-realms

- **Ekron Realms** starts the sandbox city (ready to run).
- **Ekron Realms - Quake 2** starts Quake 2's first map. Copy `pak0.pak` (and
  `pak1.pak`, `pak2.pak`) from your Quake 2 `baseq2` folder into
  `ports/ekronrealms/baseq2` first.

Settings (`config.cfg`, `settings.json`), the city, the store and Quake 2
saves are kept in `ports/ekronrealms/conf/KayteStudio/EkronRealms`. The
Quake 2 data can also go in its `baseq2` folder.

## Controls

| Button | City | Quake 2 map |
|--|--|--|
| Left stick | Move | Move |
| Right stick | Look | Look |
| A | Jump | Jump (held) |
| B | Crouch | Crouch (held) |
| X | Interact | Use the inventory item |
| Y | Rotate | Inventory |
| R2 / L2 | Grab, place / freeze, cancel | Fire / jump |
| R1 / L1 | Next / previous tool | Next / previous weapon |
| D-pad | Move | Up: help, down: scores, left / right: inventory |
| Select + L1 / R1 | | Quick save / quick load |
| Start | Pause menu (d-pad, A, B) | Pause menu |
| Select + Start | Quit (PortMaster's gptokeyb hotkey) | Quit |

The build catalog and the store need a mouse; B closes them.
