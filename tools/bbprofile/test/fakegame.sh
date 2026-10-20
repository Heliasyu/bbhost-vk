#!/usr/bin/env bash
# bbprofile.sh's test launcher (BBPROFILE_LAUNCH): runs the stand-in game from the game folder.
exec python3 "$(dirname "$(readlink -f "$0")")/fakegame.py" "$@"
