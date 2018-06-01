#!/bin/sh
install -d -m 0755 /root/radio_api

for CONFIG in radio.conf colosseum_config.ini; do
    install -m 0644 "service/radio_api/$CONFIG" /root/radio_api
done

for SCRIPT in scenario_discontinuity.sh start.sh statistics.sh  status.sh  stop.sh  update_environment.sh  update_outcomes.sh; do
    install -m 0755 "service/radio_api/$SCRIPT" /root/radio_api
done

install -m 0644 service/systemd/dragonradio.service /etc/systemd/system
systemctl enable /etc/systemd/system/dragonradio.service
systemctl daemon-reload
