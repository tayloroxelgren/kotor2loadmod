import sys
import re
import statistics

def parse_loadingscreen_times(file_path,pattern):
    """
    Reads the file at `file_path`, finds all lines containing 'loadingscreen',
    extracts the time in microseconds, and returns (total_time_us, average_time_us, count).
    """
    times = []

    with open(file_path, 'r', encoding='utf-8') as f:
        for line in f:
            m = pattern.search(line)
            if m:
                times.append(int(m.group(1)))

    count = len(times)
    if count == 0:
        return 0, 0.0, 0
    
    std_dev= statistics.stdev(times) if len(times)>1 else 0.0

    total = sum(times)
    average = total / count
    return total, average, count,std_dev

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <logfile>")
        sys.exit(1)

    logfile = sys.argv[1]

    patterns = [
        ("loadingscreen", re.compile(r'loadingscreen:\s*(\d+)\s*μs')),
        ("ProcessResourceQueue", re.compile(r'ProcessResourceQueue:\s*(\d+)\s*μs')),
        ("InitShadowCache", re.compile(r'InitShadowCache:\s*(\d+)\s*μs')),
        ("LoadResourceBlockOrFallback", re.compile(r'LoadResourceBlockOrFallback:\s*(\d+)\s*μs')),
        ("HandleBNPacket", re.compile(r'HandleBNPacket:\s*(\d+)\s*μs')),
        ("FlushTracer", re.compile(r'FlushTracer:\s*(\d+)\s*μs')),
        ("Elsefunction", re.compile(r'Elsefunction:\s*(\d+)\s*μs')),
        ("ResourcePacketDispatcher", re.compile(r'ResourcePacketDispatcher:\s*(\d+)\s*μs')),
        ("ResourceQueue_UnpackAndTrace", re.compile(r'ResourceQueue_UnpackAndTrace:\s*(\d+)\s*μs')),
        ("PpacketHandler", re.compile(r'PpacketHandler:\s*(\d+)\s*μs')),
        ("SpacketHandler", re.compile(r'SpacketHandler:\s*(\d+)\s*μs')),
        ("ModuleHandler", re.compile(r'ModuleHandler:\s*(\d+)\s*μs')),
        ("ModuleChunkLoadCore", re.compile(r'ModuleChunkLoadCore:\s*(\d+)\s*μs')),
        ("LoadingScreenUpdateFrame", re.compile(r'LoadingScreenUpdateFrame:\s*(\d+)\s*μs')),
        ("GameObjUpdate", re.compile(r'GameObjUpdate:\s*(\d+)\s*μs'))
    ]

    for name, p in patterns:
        try:
            total_us, avg_us, count, std_us = parse_loadingscreen_times(logfile, p)

            total_ms = total_us / 1000
            avg_ms = avg_us / 1000
            std_ms = std_us / 1000

            print(f"=== {name} ===") 
            print(f"Total time:   {total_us} μs ({total_ms:.2f} ms)")
            print(f"Average time: {avg_us:.2f} μs ({avg_ms:.2f} ms) over {count} samples")
            print(f"Std deviation: {std_us:.2f} μs ({std_ms:.2f} ms)")
            print()
        except:
            print(f"Function data not found for: {name}")

if __name__ == "__main__":
    main()
