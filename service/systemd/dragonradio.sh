#!/bin/sh
. /root/dragonradio/venv/bin/activate
exec /root/dragonradio/dragonradio /root/dragonradio/scripts/sc2-radio.py "$@"
