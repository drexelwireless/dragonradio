#!/bin/sh
. /root/dragonradio/venv/bin/activate
exec dragonradio-client start "$@"
