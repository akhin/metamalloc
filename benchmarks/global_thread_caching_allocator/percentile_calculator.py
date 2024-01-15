#!/usr/bin/python
import sys

def read_percentiles(filename):
    try:
        with open(filename, 'r') as file:
            values = [float(line.strip()) for line in file]
        return values
    except FileNotFoundError:
        print(f"Error: File '{filename}' not found.")
        sys.exit(1)

def calculate_percentile(values, percentile):
    values.sort()
    k = (len(values) - 1) * (percentile / 100.0)
    f = int(k)
    c = k - f
    if f + 1 < len(values):
        return values[f] + c * (values[f + 1] - values[f])
    else:
        return values[f]

if __name__ == "__main__":
    if len(sys.argv) > 1:
        filename = sys.argv[1]
    else:
        filename = "samples.txt"

    values = read_percentiles(filename)

    if not values:
        print("Error: No data found.")
        sys.exit(1)

    percentiles = [50, 75, 90, 95]
    for percentile in percentiles:
        value = calculate_percentile(values, percentile)
        print(f"{percentile}th Percentile: {value}")