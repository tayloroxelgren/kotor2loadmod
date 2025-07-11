import sys
import re

def parse_loadingscreen_times(file_path):
    """
    Reads the file at `file_path`, finds all lines containing 'loadingscreen',
    extracts the time in microseconds, and returns (total_time_us, average_time_us, count).
    """
    pattern = re.compile(r'loadingscreen:\s*(\d+)\s*μs')
    times = []

    with open(file_path, 'r', encoding='utf-8') as f:
        for line in f:
            m = pattern.search(line)
            if m:
                times.append(int(m.group(1)))

    count = len(times)
    if count == 0:
        return 0, 0.0, 0

    total = sum(times)
    average = total / count
    return total, average, count

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <logfile>")
        sys.exit(1)

    logfile = sys.argv[1]
    total_us, avg_us, count = parse_loadingscreen_times(logfile)

    total_ms = total_us / 1000
    avg_ms   = avg_us   / 1000

    print(f"Total time:   {total_us} μs ({total_ms:.2f} ms)")
    print(f"Average time: {avg_us:.2f} μs ({avg_ms:.2f} ms) over {count} samples")

if __name__ == "__main__":
    main()
