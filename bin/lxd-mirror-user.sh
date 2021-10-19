#!/bin/sh
# Copyright 2018-2021 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
CONTAINER=$1; shift

UID=$(id -u)
GID=$(id -g)

#
# Create user in the container
#

lxc exec "$CONTAINER" -- groupadd -g "$GID" "$USER"
lxc exec "$CONTAINER" -- useradd -u "$UID" -g "$GID" -G sudo -d "$HOME" -s "$SHELL" -M "$USER"
lxc exec "$CONTAINER" -- sh -c "cat >/etc/sudoers.d/$USER" <<EOF
$USER ALL=(ALL) NOPASSWD: ALL
EOF

#
# Add a profile to mount user's home directory in the container
#

lxc profile create "$USER-home" || true

lxc profile edit "$USER-home" <<EOF
config:
  raw.idmap: |
    uid $UID $UID
    gid $GID $GID
description: Mount mirrored home directory for $USER
devices:
  $USER-home:
    path: /home/$USER
    source: /home/$USER
    type: disk
name: $USER-home
EOF

#
# Apply the new profile to the container
#

PROFILES=$(lxc info "$CONTAINER" | grep Profile | sed -e 's/Profiles: //' -e 's/ //g')
lxc profile apply "$CONTAINER" "$PROFILES,$USER-home"

#
# Restart the container to make sure the profile is applied
#

lxc restart "$CONTAINER"
