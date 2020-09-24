#!/bin/sh
. /root/dragonradio/env/bin/activate
exec dragonradio-client stop "$@"
