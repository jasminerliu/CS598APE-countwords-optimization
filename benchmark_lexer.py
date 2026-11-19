#!/usr/bin/env python3

from pathlib import Path
import re
import string
import os
import subprocess
import time

from benchmark_cpp import COMPILER, openmp_flags

ROOT = Path(__file__).resolve().parent
KINDS = ['whitespace', 'word', 'number', 'punctuation', 'other']
LETTER = rb'[A-Za-z\x80-\xff]'
WORD_CHAR = rb'[A-Za-z0-9\x80-\xff]'
DIGITS = rb'[0-9]+'
RULES = [
    ('whitespace', rb'[ \t\r\n\v\f]+'),
    ('word', LETTER + WORD_CHAR + rb"*(?:['-]" + WORD_CHAR + rb"+)*"),
    ('number', rb'(?:' + DIGITS + rb'|\.' + DIGITS + rb')(?:[.,]' + DIGITS + rb')*'),
    ('punctuation', b'[' + re.escape(string.punctuation.encode()) + b']'),
    ('other', rb'[\x00-\x08\x0e-\x1f\x7f]'),
]
PATTERNS = [(kind, re.compile(pattern)) for kind, pattern in RULES]


NUM_RUNS = 5
INPUT_FILENAME = 'kjvbible_x10.txt'
PROGRAMS = ['simple-lexer', 'optimized-lexer']
THREAD_COUNTS = [1, 2, 4, 8]
CHUNK_SIZE = 2 ** 16  # 64 KiB


def cases():
    # Small correctness tests
    small = [
        ('empty', b''),
        ('whitespace', b' \t\r\n\v\f'),
        ('words', b'Six six SEVEN seven'),
        ('connectors', b"si'x s-i-x seven- seven' s--n"),
        ('numbers', b'123 4.56 6,767.67 .8 9. 10th'),
        ('punctuation', string.punctuation.encode()),
        ('non-ASCII', '67\u00e9'.encode() + b' \x00\x7f'),
    ]
    tests = [('small/' + name, data) for name, data in small]

    # Tests for 16-byte SIMD block boundaries
    simd = [
        ('length-15', b'67 67 67 67 67 6'),
        ('length-16', b'67 67 67 67 67 67'),
        ('length-17', b'67 67 67 67 67 67 '),
        ('length-33', b'67 67 67 67 67 67 67 67 67 67 67 '),
        ('crossing-word', b' ' * 13 + b'sixseven sixseven'),
    ]
    tests.extend(('simd/' + name, data) for name, data in simd)

    # Tests for tokens across 64 KiB chunk boundaries.
    boundary = CHUNK_SIZE
    boundary_cases = [
        ('apostrophe', b' ' * (boundary - 3) + b"six'seven"),
        ('number', b' ' * (boundary - 2) + b'6,767.67 six'),
        ('whitespace', b'six' + b' ' * boundary + b'sev'),
        ('long', b's' * (boundary + 1)),
        ('end-at-boundary', b' ' * (boundary - 8) + b'sixseven'),
        ('repeated-across-chunks', (b'6767 ' * (boundary // 5)) * 3),
    ]
    tests.extend(('boundary/' + name, data) for name, data in boundary_cases)
    return tests


def verify(binary, threads=1, chunk_size=CHUNK_SIZE):
    def summary(data):
        counts = {kind: 0 for kind in KINDS}
        position = 0
        while position < len(data):
            for kind, pattern in PATTERNS:
                match = pattern.match(data, position)
                if match:
                    counts[kind] += 1
                    position = match.end()
                    break
        return counts

    tests = cases()
    env = dict(os.environ, OMP_NUM_THREADS=str(threads), CHUNK_SIZE=str(chunk_size))
    passed = 0
    for name, data in tests:
        try:
            expected = summary(data)
            run = subprocess.run([str(binary)], input=data, capture_output=True,
                                 env=env, timeout=10, check=True)
            actual = {key.decode(): int(value) for key, value in
                      (line.split() for line in run.stdout.splitlines())}
            if actual != expected:
                raise ValueError(f'expected={expected}, actual={actual}')
        except (OSError, subprocess.SubprocessError, ValueError) as error:
            print(f'FAIL {name}: {error}', flush=True)
        else:
            passed += 1
    return passed, len(tests)


def time_run(binary, input_path, threads, chunk_size):
    env = dict(os.environ, OMP_NUM_THREADS=str(threads), CHUNK_SIZE=str(chunk_size))
    times = []
    for _ in range(NUM_RUNS):
        with input_path.open('rb') as stream:
            start = time.perf_counter()
            result = subprocess.run([str(binary)], stdin=stream, stdout=subprocess.PIPE,
                                    env=env, check=True)
            times.append(time.perf_counter() - start)
    return min(times), result.stdout


def main():
    times = []
    expected_output = None
    flags = openmp_flags()
    for program in PROGRAMS:
        binary = './' + program
        subprocess.run([COMPILER, '-O2', '-DNDEBUG', '-std=c++17',
                        '-fno-vectorize', '-fno-slp-vectorize',
                        program + '.cpp', '-o', binary] + flags, check=True)

        all_passed = True
        for threads in THREAD_COUNTS:
            passed, total = verify(binary, threads, CHUNK_SIZE)
            print(f'{program}: PASS {passed}/{total} tests '
                  f'({threads} threads)', flush=True)
            all_passed = all_passed and passed == total
        if not all_passed:
            raise SystemExit(1)

        for threads in THREAD_COUNTS:
            elapsed, output = time_run(binary, ROOT / INPUT_FILENAME, threads, CHUNK_SIZE)
            if expected_output is None:
                expected_output = output
            if output != expected_output:
                raise ValueError(f'{program} ({threads} threads): lexer outputs differ')
            times.append((program, threads, CHUNK_SIZE, elapsed))
    times.sort(key=lambda x: x[3])

    print('\nVERIFIED OUTPUT')
    print(expected_output.decode(), end='')

    print()
    print('Program         | Threads | Chunk bytes | Seconds')
    print('--------------- | ------- | ----------- | --------')
    for program, threads, chunk_size, elapsed in times:
        print('{:<15} | {:7} | {:11} | {:.6f}'.format(program, threads, chunk_size, elapsed))


if __name__ == '__main__':
    main()
