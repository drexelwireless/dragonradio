#!/bin/sh
. /root/dragonradio/venv/bin/activate
exec dragonradio-client update-environment "$@"
