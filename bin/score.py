#!/usr/bin/env python3
import sys
sys.path.insert(0, 'python')

import argparse
import json
import logging

from dragon.scoring import *

def main():
    parser = argparse.ArgumentParser(description='Plot trpr.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-d', '--debug', action='store_const', const=logging.DEBUG,
                        dest='loglevel',
                        default=logging.WARNING,
                        help='print debugging information')
    parser.add_argument('-v', '--verbose', action='store_const', const=logging.INFO,
                        dest='loglevel',
                        help='be verbose')
    parser.add_argument('--inspect-mp', type=int,
                        default=None,
                        metavar='MP',
                        help='inspect specific measurement period')
    parser.add_argument('scores',
                        metavar='FILE',
                        help='CSV scoring data file')

    # Parse arguments
    try:
        args = parser.parse_args()
    except SystemExit as ex:
        return ex.code

    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=args.loglevel)

    df = pd.read_csv(args.scores)
    df.set_index(['flow_uid', 'mp'], inplace=True)
    df.sort_index(inplace=True)

    df = scoreGoals(df)

    df.to_csv('goals.csv')

    df.groupby(['mp'], sort=False)['goal_stable'].sum().to_csv('score.csv', header=True)

    if args.inspect_mp:
        df_mp = df[df.index.get_level_values('mp') == args.inspect_mp]
        df_mp.to_csv('mp_{:d}.csv'.format(args.inspect_mp))
        print(df_mp['mp_score'].sum())

if __name__ == '__main__':
    main()
