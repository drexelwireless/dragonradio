#!/bin/sh
IMAGE=queuing-roun-20190828-d5995197

scp freeplay.conf sc2-lz:/share/nas/freeplay/dragon-radio
ssh sc2-lz "chmod 0750 /share/nas/freeplay/dragon-radio/freeplay.conf"

ssh sc2-lz "cp /share/nas/dragon-radio/images/$IMAGE.tar.gz /share/nas/freeplay/dragon-radio/freeplay.tar.gz"
ssh sc2-lz "chmod 0750 /share/nas/freeplay/dragon-radio/freeplay.tar.gz"
