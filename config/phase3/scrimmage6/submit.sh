#!/bin/sh
EVENT=scrimmage6
VALIDATE=6
IMAGE=testing-20190902-ef5e158c
QUAL=104777

copy()
{
  FILE=$1; shift
  DIR=$1; shift
  PERMISSIONS=$1; shift

  scp "$FILE" "sc2-lz:/share/nas/dragon-radio/$EVENT/$DIR"
  ssh sc2-lz "chmod 0750 /share/nas/dragon-radio/$EVENT/$DIR$FILE"
}

ssh sc2-lz "rm -rf /share/nas/dragon-radio/$EVENT"
ssh sc2-lz "mkdir -p /share/nas/dragon-radio/$EVENT/config"

copy "batch.json" "config/" "0750"
copy "$EVENT.conf" "config/" "0750"
copy "rf_threshold_baseline.txt" "" "0750"

ssh sc2-lz "cp /share/nas/dragon-radio/images/$IMAGE.tar.gz /share/nas/dragon-radio/$EVENT/$EVENT.tar.gz"
ssh sc2-lz "chmod 0750 /share/nas/dragon-radio/$EVENT/$EVENT.tar.gz"

ssh sc2-lz "md5sum /share/nas/dragon-radio/images/$IMAGE.tar.gz >/share/nas/dragon-radio/$EVENT/$EVENT.md5"

ssh sc2-lz "cp /share/nas/dragon-radio/RESERVATION-$QUAL/traffic_logs/*.drc /share/nas/dragon-radio/$EVENT/"

ssh sc2-lz "/share/nas/common/other/scrimmage-utils/validate_competitor_submission.sh $VALIDATE"
