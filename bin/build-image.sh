#!/bin/sh
# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
set -e

usage() { echo "Usage: $0 [-s] REV" 1>&2; exit 1; }

while getopts "s" opt; do
    case "${opt}" in
        s)
            SERVICE=1
            ;;
        *)
            usage
            ;;
    esac
done

shift $((OPTIND-1))

if [ $# -ne 1 ]; then
    REV="HEAD"
else
    REV="$1"; shift
fi

#
# Clone the repo
#
echo "Cloning dragonradio repository"

DIR=$(mktemp -d)
BASEDIR=$(basename "$DIR")

git clone . "$DIR"

#
# Chekout specified revision
#
echo "Checking out $REV"

(cd "$DIR" && git checkout "$REV")

HASH=$(cd "$DIR" && git rev-parse "$REV"^{} | cut -c1-8)

REVNAME=$(cd "$DIR" && (git name-rev --name-only "$REV" --exclude=tags/ | sed -e 's/^.*\///' -e 's/[^a-zA-Z0-9-]//g'))
REVNAME=$(basename $REVNAME)

DATE=$(date +%Y%m%d)

if [ -z "$REVNAME" ]; then
    REVNAME="dragonradio"
fi

CONTAINER="$REVNAME-$DATE-$HASH"

(cd "$DIR" && git submodule update --init --recursive)

#
# Create the container
#
echo "Creating container $CONTAINER"

lxc launch dragonradio-2004-base "$CONTAINER"

# XXX Wait for the container to come up
sleep 10

#
# Copy the repo to the container
#
echo "Copying dragonradio to $CONTAINER"

tar cf - -C $(dirname "$DIR") "$BASEDIR" | lxc exec "$CONTAINER" -- tar xvf - -C /root

lxc exec "$CONTAINER" -- mv "/root/$BASEDIR" /root/dragonradio

#
# Build the radio
#
echo "Building dragonradio"

lxc exec "$CONTAINER" bash <<EOF
cd /root/dragonradio
./build.sh
cd /root/dragonradio/tools
./install.sh
EOF

if [ ! -z "$SERVICE" ]; then
    echo "Installing dragonradio service"

    lxc exec "$CONTAINER" bash <<EOF
cd /root/dragonradio
if [ -f install.sh ]; then
    ./install.sh
fi
EOF
fi

#
# Export the container
#
echo "Exporting $CONTAINER"

lxc stop -f "$CONTAINER"
lxc publish "$CONTAINER" --alias "$CONTAINER"
mkdir -p images
lxc image export "$CONTAINER" "images/$CONTAINER"
