#!/usr/bin/env python3

from collections import Counter
from pathlib import Path
import re
import string
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent
CHUNK_SIZES = [1024, 4096, 65536]
TIMEOUT = 10
COMPILER = 'clang++'
NUM_RUNS = 5

programs = [
    ('C++ original', 'optimized.cpp', 'existing optimized implementation'),
    ('C++ buffered', 'optimized-buffered.cpp', 'serial with 64 KiB buffered input'),
]


def reference(data):
    return Counter(word for word in re.split(rb'[\x00-\x20]+', data.lower()) if word)


def cases():
    # Small correctness tests
    small = [
        ('empty', b''),
        ('single', b'sixseven'),
        ('repeated', b'sixseven sixseven sixseven'),
        ('whitespace', b' \t\n\r\v\f'),
        ('digits', b'123 456 7890'),
        ('capitalization', b'Six SIX seVen SEVEN'),
        ('mixed', b'six seven\tsix\nseven\rsix\vseven\f'),
        ('ASCII', ' '.join(string.punctuation).encode('ascii')),
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

    # Tests for word counting across chunk boundaries
    for boundary in CHUNK_SIZES:
        boundary_cases = [
            ('crossing-word', b' ' * (boundary - 3) + b'sixseven sixseven'),
            ('separator-at-boundary', b' ' * (boundary - 4) + b'six6 seven7'),
            ('eof-at-boundary', b' ' * (boundary - 4) + b'6767'),
            ('repeated-across-chunks', (b' ' * (boundary - 4) + b'Six ') * 3 + b'six seven'),
        ]
        tests.extend((f'boundary-{boundary}/' + name, data) for name, data in boundary_cases)
    return tests


def check(output, expected):
    actual = {}
    counts = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) != 2:
            raise ValueError(f'incorrect output line: {line!r}')
        word, count = fields
        actual[word] = int(count)
        counts.append(int(count))
    if actual != expected:
        missing = list((expected - Counter(actual)).items())[:5]
        extra = list((Counter(actual) - expected).items())[:5]
        raise ValueError(f'incorrect counts: missing={missing}, extra={extra}')
    if counts != sorted(counts, reverse=True):
        raise ValueError('incorrect output order')


def time_run(cmdline, data):
    times = []
    with tempfile.TemporaryFile() as stream:
        stream.write(data)
        for _ in range(NUM_RUNS):
            stream.seek(0)
            start = time.perf_counter()
            subprocess.run(cmdline, stdin=stream, stdout=subprocess.DEVNULL,
                           check=True, timeout=TIMEOUT)
            times.append(time.perf_counter() - start)
    return min(times)


def test_run(cmdline, tests):
    passed = 0
    failures = []
    for name, data, expected in tests:
        elapsed = None
        try:
            result = subprocess.run(cmdline, input=data, capture_output=True,
                                    check=True, timeout=TIMEOUT)
            elapsed = time_run(cmdline, data)
            check(result.stdout, expected)
        except (OSError, subprocess.SubprocessError, ValueError) as error:
            failures.append((name, str(error)))
            timing = ' (best: {:.6f} s)'.format(elapsed) if elapsed is not None else ''
            print('FAIL {}: {}{}'.format(name, error, timing), flush=True)
        else:
            passed += 1
            print('PASS {} (best: {:.6f} s)'.format(name, elapsed), flush=True)
    return passed, failures


def build_targets(directory):
    targets = []
    for label, filename, notes in programs:
        binary = Path(directory) / Path(filename).stem
        subprocess.run([COMPILER, '-O2', '-DNDEBUG', '-std=c++17', str(ROOT / filename),
                        '-o', str(binary)], check=True)
        targets.append((label, [str(binary)], notes))
    return targets


def run_tests(targets, tests):
    results = []
    for program in targets:
        label, cmdline, _ = program
        print('\nTesting', label, flush=True)
        passed, failures = test_run(cmdline, tests)
        print('{}/{} passed'.format(passed, len(tests)))
        results.append((program, passed, failures))
    return results


def main():
    tests = [(name, data, reference(data)) for name, data in cases()]
    with tempfile.TemporaryDirectory(prefix='countwords-tests-') as directory:
        targets = build_targets(directory)
        results = run_tests(targets, tests)
    return 1 if any(failures for _, _, failures in results) else 0


if __name__ == '__main__':
    raise SystemExit(main())
