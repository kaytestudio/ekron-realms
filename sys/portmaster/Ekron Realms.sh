#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/ekronrealms"

cd "$GAMEDIR"
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

$ESUDO chmod +x "$GAMEDIR/bin/ekron-realms"
mkdir -p "$GAMEDIR/conf" "$GAMEDIR/baseq2"

# Settings, the city and Quake 2 saves stay in the port folder.
export XDG_DATA_HOME="$GAMEDIR/conf"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
# OpenGL ES 3.0 (Mali, PowerVR, Adreno: no desktop OpenGL on the handhelds)
export EKRON_GLES=1

# The game reads the controller itself; gptokeyb only provides the exit hotkey.
$GPTOKEYB "ekron-realms" &
pm_platform_helper "$GAMEDIR/bin/ekron-realms"
./bin/ekron-realms --quake2 "$GAMEDIR" +set vid_fullscreen 1

pm_finish
