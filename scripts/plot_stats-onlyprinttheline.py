#!/usr/bin/env python3
"""
Generate time series graphs of power/bandwidth/energy...
"""

import argparse
import json
import os
import sys
import numpy as np
import matplotlib.pyplot as plt


def extract_epoch_data(json_data, label, merge_channel=True):
    """
    TODO enable merge_channel=False option later
    """
    if merge_channel:
        merged_data = {}
        for line in json_data:
            epoch_num = line["epoch_num"]
            if epoch_num in merged_data:
                merged_data[epoch_num] += line[label]
            else:
                merged_data[epoch_num] = line[label]
        return [v for (k, v) in sorted(merged_data.items(),
                                       key=lambda t: t[0])]


def plot_epochs(json_data, label, unit="", output=None):
    """
    plot the time series of a specified stat serie (e.g. bw, power, etc)
    """
    print('ploting {}'.format(label))
    cycles_per_epoch = json_data[0]['num_cycles']
    y_data = extract_epoch_data(json_data, label)
    x_ticks = [i * cycles_per_epoch for i in range(len(y_data))]

    plt.plot(x_ticks, y_data)
    #plt.ylim(bottom=0, top=1.1*max(y_data))
    plt.savefig(output+'_epochs_{}.pdf'.format(label))

    #print(y_data)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Plot time serie graphs from '
                                     'stats outputs, type -h for more options')
    parser.add_argument('json', help='stats json file')
    parser.add_argument('-d', '--dir', help='output dir', default='.')
    parser.add_argument('-o', '--output',
                        help='output name (withouth extension name)',
                        default='dramsim')
    parser.add_argument('-k', '--key',
                        help='plot a specific key name in epoch stats, '
                        'use the name in JSON')
    args = parser.parse_args()

    with open(args.json, 'r') as j_file:
        is_epoch = False
        try:
            j_data = json.load(j_file)
        except:
            print('cannot load file ' + args.json)
            exit(1)
        if isinstance(j_data, list):
            is_epoch = True
        else:
            is_epoch = False
        
    #print(j_data[0]['num_cycles'])

    prefix = os.path.join(args.dir, args.output)
    if is_epoch:
        #data_units = {'average_bandwidth': 'GB/s',
        #              'average_power': 'mW',
        #              'average_read_latency': 'cycles'}
        data_units = {'average_power': 'mW'}
        if args.key:
            data_units[args.key] = ''
        for label, unit in data_units.items():
            plot_epochs(j_data, label, unit, prefix)