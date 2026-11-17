#!/usr/bin/env python3

# Benchmark thread/chunk configurations and output results table as Markdown.
# Compiles the latest source before timing; requires kjvbible_x10.txt.

from benchmark_cpp import COMPILER, openmp_flags

import os
import subprocess
import time

NUM_RUNS = 5
INPUT_FILENAME = 'kjvbible_x10.txt'
PROGRAM = './optimized-buffered-cpp'
THREAD_COUNTS = [1, 2, 4, 8]
CHUNK_SIZES = [
    2 ** 14,  # 16 KiB
    2 ** 16,  # 64 KiB
    2 ** 18,  # 256 KiB
    2 ** 20,  # 1 MiB
]


def time_run(threads, chunk_size):
    env = dict(os.environ, OMP_NUM_THREADS=str(threads), CHUNK_SIZE=str(chunk_size))
    times = []
    for _ in range(NUM_RUNS):
        with open(INPUT_FILENAME, 'rb') as stream:
            start = time.perf_counter()
            subprocess.run([PROGRAM], stdin=stream, stdout=subprocess.DEVNULL,
                           env=env, check=True)
            times.append(time.perf_counter() - start)
    return min(times)


programs = [
    (threads, chunk_size)
    for threads in THREAD_COUNTS
    for chunk_size in CHUNK_SIZES
]

subprocess.run([COMPILER, '-O2', '-DNDEBUG', '-std=c++17',
                'optimized-buffered.cpp', '-o', PROGRAM] + openmp_flags(), check=True)

times = []
for threads, chunk_size in programs:
    elapsed = time_run(threads, chunk_size)
    times.append((threads, chunk_size, elapsed))
times.sort(key=lambda x: x[2])

print()
print('Threads | Chunk bytes | Seconds')
print('------- | ----------- | --------')
for threads, chunk_size, elapsed in times:
    print('{:7} | {:11} | {:.6f}'.format(threads, chunk_size, elapsed))
