#!/bin/sh
for FILE in batch.json scrimmage7.conf; do
    scp "$FILE" sc2-lz:/share/nas/dragon-radio/scrimmage7/config
done

scp batch.json sc2-lz:/share/nas/dragon-radio/batch
scp scrimmage7.conf sc2-lz:/share/nas/dragon-radio/config
