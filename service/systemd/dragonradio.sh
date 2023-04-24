#!/bin/sh
. /usr/local/dragonradio/venv/bin/activate
exec /usr/local/dragonradio/dragonradio /usr/local/dragonradio/scripts/radiowars.py "$@"
