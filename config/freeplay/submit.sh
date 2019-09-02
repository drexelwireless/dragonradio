#!/bin/sh
IMAGE=testing-20190902-ef5e158c

scp freeplay.conf sc2-lz:/share/nas/freeplay/dragon-radio
ssh sc2-lz "chmod 0750 /share/nas/freeplay/dragon-radio/freeplay.conf"

ssh sc2-lz "cp /share/nas/dragon-radio/images/$IMAGE.tar.gz /share/nas/freeplay/dragon-radio/freeplay.tar.gz"
ssh sc2-lz "chmod 0750 /share/nas/freeplay/dragon-radio/freeplay.tar.gz"
