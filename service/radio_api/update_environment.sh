#!/usr/bin/env bash
# update_environment.sh - This script is called by the Colosseum to tell the radio to update the environment to the container. 
# No input is accepted.
# STDOUT and STDERR may be logged, but the exit status is always checked.
# The script should return 0 to signify successful execution.

#this means it is running
echo "[`date`] Ran update_environment.sh" >> /logs/run.log
echo "[`date`] New environment:" >> /logs/env.log
cat /root/radio_api/environment.json >> /logs/env.log
exit 0
