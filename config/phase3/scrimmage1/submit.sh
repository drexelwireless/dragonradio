#!/bin/sh
EVENT=scrimmage1
VALIDATE=1
IMAGE=polyphase-20190123-9f5a4266
QUAL=73203

ssh sc2-lz "rm -rf /share/nas/dragon-radio/$EVENT"
ssh sc2-lz "mkdir -p /share/nas/dragon-radio/$EVENT/config"

scp batch.json "sc2-lz:/share/nas/dragon-radio/$EVENT/config"
ssh sc2-lz "chmod 0750 /share/nas/dragon-radio/$EVENT/config/batch.json"

scp "$EVENT.conf" "sc2-lz:/share/nas/dragon-radio/$EVENT/config"
ssh sc2-lz "chmod 0750 /share/nas/dragon-radio/$EVENT/config/$EVENT.conf"

ssh sc2-lz "cp /share/nas/dragon-radio/images/$IMAGE.tar.gz /share/nas/dragon-radio/$EVENT/$EVENT.tar.gz"
ssh sc2-lz "chmod 0750 /share/nas/dragon-radio/$EVENT/$EVENT.tar.gz"

ssh sc2-lz "md5sum /share/nas/dragon-radio/images/$IMAGE.tar.gz >/share/nas/dragon-radio/$EVENT/$EVENT.md5"

ssh sc2-lz "cp /share/nas/dragon-radio/RESERVATION-$QUAL/traffic_logs/*.drc /share/nas/dragon-radio/$EVENT/"

ssh sc2-lz "/share/nas/common/other/scrimmage-utils/validate_competitor_submission.sh $VALIDATE"
