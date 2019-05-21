#!/bin/sh
EVENT=pe2

for FILE in batch.json "$EVENT.conf"; do
    scp "$FILE" "sc2-lz:/share/nas/dragon-radio/$EVENT/config"
done

scp batch.json sc2-lz:/share/nas/dragon-radio/batch
scp "$EVENT.conf" sc2-lz:/share/nas/dragon-radio/config
