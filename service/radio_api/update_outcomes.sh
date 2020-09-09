#!/bin/sh
. /root/dragonradio/env/bin/activate
exec dragonradio-client update-outcomes "$@"
