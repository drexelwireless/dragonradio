# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import argparse
import importlib_resources
import jinja2
import json
import logging
from pathlib import Path
import sys

JSON_DUMPS_KWARGS = { 'sort_keys': True, 'indent': 2, 'separators': (',', ': ')}

SCENARIOS = { 'sce-qual': '9988'
            , 'sce-cil-qual': '9991'
            }

def sc2_batch():
    # Determine available scenario batch files
    scenarios = sorted([f.stem for f in importlib_resources.files('dragonradio.tools.colosseum.data.batch').iterdir()]
                       + list(SCENARIOS.keys()))

    parser = argparse.ArgumentParser(description="Generate a batch configuration.")

    parser.add_argument('-d', '--debug', action='store_const',
                        const=logging.DEBUG,
                        dest='loglevel',
                        default=logging.WARNING,
                        help='print debugging information')
    parser.add_argument('-v', '--verbose', action='store_const',
                        const=logging.INFO,
                        dest='loglevel',
                        help='be verbose')
    parser.add_argument('-o', '--output', type=Path,
                        help='output destination')

    # Template options
    template_parser = parser.add_argument_group('Batch template options options')
    template_parser.add_argument('--no-template', action='store_const',
                                 const=False,
                                 default=True,
                                 dest='template',
                                 help='do not fill out template')
    template_parser.add_argument('--scenario', type=str,
                                 choices=scenarios,
                                 default='sce-qual',
                                 metavar='SCENARIO',
                                 help='scenario')
    template_parser.add_argument('--name', type=str,
                                 default='',
                                 metavar='NAME',
                                 help='job name')
    template_parser.add_argument('--image', type=str,
                                 metavar='IMAGE',
                                 help='image name')
    template_parser.add_argument('--config', type=str,
                                 default="dragonradio.conf",
                                 metavar='CONFIG',
                                 help='radio configuration')

    # Parse arguments
    args = parser.parse_args()

    # Set up logging
    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=args.loglevel)

    # Determine scenario
    scenario = SCENARIOS.get(args.scenario, args.scenario)
    path = importlib_resources.files('dragonradio.tools.colosseum.data.batch').joinpath(scenario + '.json')

    if not path.exists():
        print(f"Cannot find batch configuration for scenario {scenario:}", file=sys.stderr)
        return 1

    # Reade batch template
    with path.open('r') as f:
        batch_jsons = f.read()

    # Fill out template
    if args.template:
        if args.image is None:
            print(f"Please specify an image", file=sys.stderr)
            return 1

        t = jinja2.Template(batch_jsons)

        batch_jsons = t.render(name=args.name,
                               image=args.image,
                               radio_conf=args.config)

    batch_json = json.loads(batch_jsons)
    batch_jsons = json.dumps(batch_json, **JSON_DUMPS_KWARGS)

    if args.output:
        with args.output.open('w') as f:
          f.write(batch_jsons)
    else:
        print(batch_jsons)
