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


def plot_epochs(json_data1, json_data2, label, unit="", output=None):
    """
    plot the time series of a specified stat serie (e.g. bw, power, etc)
    """
    print('ploting {}'.format(label))
    cycles_per_epoch = json_data1[0]['num_cycles']
    y_data1 = extract_epoch_data(json_data1, label)
    x_ticks = [i * cycles_per_epoch for i in range(len(y_data1))]

    cycles_per_epoch = json_data2[0]['num_cycles']
    y_data2 = extract_epoch_data(json_data2, label)
    x_ticks = [i * cycles_per_epoch for i in range(len(y_data2))]

    plt.plot(x_ticks, y_data1)
    plt.plot(x_ticks, y_data2)

    plt.title(label)
    plt.ticklabel_format(style='sci', axis='x', scilimits=(0, 0))
    plt.xlabel('Cycles')
    plt.ylabel('{} ({})'.format(label, unit))
    comp1 = max(y_data1)
    comp2 = max(y_data2) 
    plt.ylim(bottom=0, top=1.1*max(comp1, comp2))
    if output:
        plt.savefig(output+'_epochs_{}.pdf'.format(label))
        plt.clf()
    else:
        plt.show()
    return

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Plot time serie graphs from '
                                     'stats outputs, type -h for more options')
    parser.add_argument('json1', help='stats json file1')
    parser.add_argument('json2', help='stats json file2')
    parser.add_argument('-d', '--dir', help='output dir', default='.')
    parser.add_argument('-o', '--output',
                        help='output name (withouth extension name)',
                        default='dramsim')
    parser.add_argument('-k', '--key',
                        help='plot a specific key name in epoch stats, '
                        'use the name in JSON')
    args = parser.parse_args()

    with open(args.json1, 'r') as j_file1:
        is_epoch = False
        try:
            j_data1 = json.load(j_file1)
        except:
            print('cannot load file ' + args.json1)
            exit(1)
        if isinstance(j_data1, list):
            is_epoch = True
        else:
            is_epoch = False

    with open(args.json2, 'r') as j_file2:
        is_epoch = False
        try:
            j_data2 = json.load(j_file2)
        except:
            print('cannot load file ' + args.json2)
            exit(1)
        if isinstance(j_data2, list):
            is_epoch = True
        else:
            is_epoch = False
    

    prefix = os.path.join(args.dir, args.output)
    if is_epoch:
        #data_units = {'average_bandwidth': 'GB/s',
        #              'average_power': 'mW',
        #              'average_read_latency': 'cycles'}
        data_units = {'average_power': 'mW'}
        if args.key:
            data_units[args.key] = ''
        for label, unit in data_units.items():
            plot_epochs(j_data1, j_data2, label, unit, prefix)
    else:
        data_units = {'read_latency': 'cycles',
                      'write_latency': 'cycles',
                      'interarrival_latency': 'cycles'}
        #for label, unit in data_units.items():
            #plot_histogram(j_data, label, unit, prefix)
