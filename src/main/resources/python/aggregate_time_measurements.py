from __future__ import division
from collections import OrderedDict
import sys


def compute_stats(data):
    stats = []
    for test_name, measurements in data.items():
        mean = sum(measurements) / len(measurements)
        stddev = (sum(map(lambda x: (x - mean)**2, measurements)) /
                  (len(measurements) - 1))**0.5
        stats.append('%s mean: %.2f' % (test_name, mean))
        stats.append('%s standard deviation: %.2f' % (test_name, stddev))
        stats.append('%s relative standard deviation: %.2f' %
                     (test_name, stddev / mean))
    return stats


def hours_to_minutes(hours):
    return hours * 60


def minutes_to_seconds(minutes):
    return minutes * 60


def hours_to_seconds(hours):
    return minutes_to_seconds(hours_to_minutes(hours))


def milliseconds_to_seconds(milliseconds):
    return milliseconds / 1000


def microseconds_to_milliseconds(microseconds):
    return microseconds / 1000


def microseconds_to_seconds(microseconds):
    return milliseconds_to_seconds(microseconds_to_milliseconds(microseconds))


def lines_to_data(lines):
    results = OrderedDict()
    test_name = ""
    for line in lines:
        if line.startswith('#'):
            test_name = line[1:].strip()
            results[test_name] = []
        else:
            # stride formats timing data as hours:minutes:seconds:milliseconds:microseconds
            split_data = line.strip().split(':')
            assert (len(split_data) == 5)
            elapsed_secs = \
                hours_to_seconds(float(split_data[0])) + \
                minutes_to_seconds(float(split_data[1])) + \
                float(split_data[2]) + \
                milliseconds_to_seconds(float(split_data[3])) + \
                microseconds_to_seconds(float(split_data[4]))

            results[test_name].append(elapsed_secs)
    return results


if __name__ == "__main__":
    file_name = 'measurements.txt' if len(sys.argv) <= 1 else sys.argv[1]
    with open(file_name) as f:
        for output_line in compute_stats(lines_to_data(f.readlines())):
            print(output_line)
