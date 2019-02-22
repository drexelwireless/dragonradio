#!/bin/sh
IMAGE=mainland-20190110-3fb355a8

scp freeplay.conf sc2-lz:/share/nas/freeplay/dragon-radio
ssh sc2-lz "chmod 0750 /share/nas/freeplay/dragon-radio/freeplay.conf"

ssh sc2-lz "cp /share/nas/dragon-radio/images/$IMAGE.tar.gz /share/nas/freeplay/dragon-radio/freeplay.tar.gz"
ssh sc2-lz "chmod 0750 /share/nas/freeplay/dragon-radio/freeplay.tar.gz"
