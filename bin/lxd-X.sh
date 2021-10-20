#!/bin/sh
# Copyright 2018-2021 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
CONTAINER=$1; shift

UID=$(id -u)
GID=$(id -g)

X=$(echo $DISPLAY | sed -e 's/://')

PROFILE="$USER-X${X}"

#
# Add a profile to allow X from container
#

lxc profile create "$PROFILE" || true

lxc profile edit "$PROFILE" <<EOF
config:
  environment.DISPLAY: :0
  environment.PULSE_SERVER: unix:/tmp/.pulse-native
  nvidia.driver.capabilities: all
  raw.idmap: |
    uid $UID $UID
    gid $GID $GID
  user.user-data: |
    #cloud-config
    runcmd:
      - 'sed -i "s/; enable-shm = yes/enable-shm = no/g" /etc/pulse/client.conf'
    packages:
      - x11-apps
      - mesa-utils
      - pulseaudio
description: X${X} access for $USER
devices:
  PASocket:
    path: /tmp/.pulse-native
    source: /run/user/$UID/pulse/native
    type: disk
  X0:
    path: /tmp/.X11-unix/X0
    source: /tmp/.X11-unix/X${X}
    type: disk
  mygpu:
    type: gpu
name: $USER-X${X}
EOF

#
# Apply the new profile to the container
#

PROFILES=$(lxc info "$CONTAINER" | grep Profile | sed -e 's/Profiles: //' -e 's/ //g')
lxc profile apply "$CONTAINER" "$PROFILES,$PROFILE"

#
# Restart the container to make sure the profile is applied
#

lxc restart "$CONTAINER"
