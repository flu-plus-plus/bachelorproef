from __future__ import division

def compute_stats(data):
    stats = []
    for test_name, measurements in data.items():
        mean = sum(measurements) / len(measurements)
        stddev = (sum(map(lambda x: (x - mean) ** 2, measurements)) / (len(measurements) - 1)) ** 0.5
        stats.append('%s mean: %.2f' % (test_name, mean))
        stats.append('%s standard deviation: %.2f' % (test_name, stddev))
        stats.append('%s relative standard deviation: %.2f' % (test_name, stddev / mean))
    return stats

def lines_to_data(lines):
    results = {}
    test_name = ""
    for line in lines:
        if line.startswith('#'):
            test_name = line[1:].strip()
            results[test_name] = []
        else:
            results[test_name].append(float(line.strip()))
    return results

if __name__ == "__main__":
    with open('measurements.txt') as f:
        for output_line in compute_stats(lines_to_data(f.readlines())):
            print(output_line)
