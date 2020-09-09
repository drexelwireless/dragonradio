#!/bin/sh
. /root/dragonradio/env/bin/activate
exec /root/dragonradio/dragonradio /root/dragonradio/scripts/sc2-radio.py "$@"
