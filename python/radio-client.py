#!/usr/bin/env python3
import argparse
import asyncio
import json
from pprint import pprint
import sys

from dragon.protobuf import *
import dragon.dragonradio_pb2 as internal
import dragon.radio_api as radio_api

def main():
    parser = argparse.ArgumentParser(description='Interact with dragonradio.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-d', '--debug', action='store_const', const=logging.DEBUG,
                        dest='loglevel',
                        default=logging.WARNING,
                        help='print debugging information')
    parser.add_argument('-v', '--verbose', action='store_const', const=logging.INFO,
                        dest='loglevel',
                        help='be verbose')
    parser.add_argument('action', choices=['start', 'stop', 'status', 'update-outcomes', 'update-environment'])
    parser.add_argument('paths', type=str, nargs='*',
                        help='path to JSON file')

    try:
        args = parser.parse_args()
    except SystemExit as ex:
        return ex.code

    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=args.loglevel)

    loop=asyncio.get_event_loop()

    client = radio_api.RadioAPIClient(loop=loop)

    if args.action == 'start':
        with client:
            client.start()
    elif args.action == 'stop':
        with client:
            client.stop()
    elif args.action == 'status':
        data = {}

        try:
            with client:
                resp = client.status()

            data['STATE'] = radio_api.stateToString(resp.status.state)
            data['INFO'] = resp.status.info
        except:
            data['STATE'] = 'OFF'
            data['INFO'] = ''

        print(json.dumps(data))
    elif args.action == 'update-outcomes':
        if len(args.paths) == 0:
            path = "/root/radio_api/mandated_outcomes.json"
        else:
            path = args.paths[0]

        with open(path) as f:
            goals = f.read()

        with client:
            client.updateMandatedOutcomes(goals)
    elif args.action == 'update-environment':
        if len(args.paths) == 0:
            path = "/root/radio_api/environment.json"
        else:
            path = args.paths[0]

        with open(path) as f:
            goals = f.read()

        with client:
            client.updateEnvironment(goals)

    loop.close()
    return 0

if __name__ == '__main__':
    main()
