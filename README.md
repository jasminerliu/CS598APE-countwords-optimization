# CS598APE Project: Parallel and SIMD Optimization of Countwords

Jasmine Liu and Sherry Long. This fork adds a parallel Countwords program and a scalar and ARM NEON text lexer. Everything below the "Original README" line is Ben Hoyt's original README.

## Hardware and software requirements

- An ARM64 Mac (Apple Silicon). We tested on an Apple M1 with 8 GB of memory. `optimized-lexer.cpp` uses ARM NEON and only builds on ARM64.
- macOS with Apple Clang (we used version 17), Homebrew `libomp` (`brew install libomp`), and Python 3.

## Our files

| File | Description |
|---|---|
| `optimized.cpp` | Original optimized C++ Countwords (unchanged baseline) |
| `optimized-buffered.cpp` | Our parallel Countwords (OpenMP, per-thread hash tables) |
| `simple-lexer.cpp` | Our scalar lexer (counts whitespace, word, number, punctuation, and other tokens) |
| `optimized-lexer.cpp` | Our NEON lexer |
| `benchmark_cpp.py` | Countwords correctness tests (20 cases) |
| `benchmark_parallel.py` | Countwords timing across thread counts and chunk sizes |
| `benchmark_lexer.py` | Lexer correctness tests (18 cases) and timing |
| `kjvbible.txt` | Input text (King James Bible) |

## How to run

```bash
# Create the 43 MB benchmark input
python3 -c "from pathlib import Path; Path('kjvbible_x10.txt').write_bytes(Path('kjvbible.txt').read_bytes()*10)"

python3 benchmark_cpp.py       # Countwords tests
python3 benchmark_parallel.py  # Countwords timing
python3 benchmark_lexer.py     # lexer tests and timing
```

Each script compiles the code before running it. To run a program by hand:

```bash
OMP_NUM_THREADS=8 CHUNK_SIZE=65536 ./optimized-lexer < kjvbible_x10.txt
```

---

# Original README
Playing with counting word frequencies (and the performance thereof) in various languages. See the full article for context and background: [https://benhoyt.com/writings/count-words/](https://benhoyt.com/writings/count-words/)


## NOTE

I received dozens of pull requests when my [count-words article](https://benhoyt.com/writings/count-words/) first came out, almost to the point where I couldn't keep up with them. Thanks for all the submissions!

However, I'm **not taking new submissions anymore and have archived this repository,** due mainly to the amount of time I've spent on this already. The major languages are there, and many minor ones. However, I'm still open to receiving feedback -- you can either email me (via the email address on my website) or open an issue here. Thanks!


## To run the benchmarks

To run the tests and benchmarks (you'll need Go, Rust, Python 3, AWK and perhaps other tools installed). Only tested on Linux:

```bash
./test.sh
./benchmark.py
```

For the latest results (run on my machine against these [versions](https://github.com/benhoyt/countwords/blob/master/versions.txt)), see the article's [performance results section](https://benhoyt.com/writings/count-words/#performance-results-and-learnings).


## Credits

Thanks to these contributors for additional language versions:

* Bash: [Jesse Hathaway](https://github.com/lollipopman) - not included in benchmarks as it takes over 2 minutes
* C#: [John Taylor](https://github.com/jftuga), [Yuriy Ostapenko](https://github.com/uncleyo) and [Osman Turan](https://github.com/osman-turan)
* C# (LINQ): [Osman Turan](https://github.com/osman-turan) - not run in benchmarks
* C++ optimized version: [Jussi Pakkanen](https://github.com/jpakkane), [Adev](https://github.com/adevress), [Nathan Myers](https://github.com/ncm)
* Common Lisp: [Brad Svercl](https://github.com/bradms)
* Crystal: [Andrea Manzini](https://github.com/ilmanzo)
* D: [Ross Lonstein](https://github.com/rlonstein)
* F#: [Yuriy Ostapenko](https://github.com/uncleyo)
* Go: [Miguel Angel](https://github.com/ntrrg) - simplifying the Go optimized version; [Joshua Corbin](https://github.com/jcorbin) - adding a [parallel Go version](https://github.com/benhoyt/countwords/blob/9db2ab6808921e649fc5212c00712e61edf6fa1c/parallel.go) for demonstration
* Haskell: [Adrien Glauser](https://github.com/why-not-try-calmer)
* Java: [Iulian Pleșoianu](https://github.com/bit-twit)
* JavaScript: [Dani Biró](https://github.com/Daninet) and [Flo Hinze](https://github.com/laubsauger)
* Julia: [Alessandro Melis](https://github.com/alemelis)
* Kotlin: [Kazik Pogoda](https://github.com/morisil)
* Lua: [Flemming Madsen](https://github.com/themadsens)
* Nim: [csterritt](https://github.com/csterritt) and [euantorano](https://github.com/euantorano)
* OCaml: [doesntgolf](https://github.com/doesntgolf)
* Pascal: [Osman Turan](https://github.com/osman-turan)
* Perl: [Charles Randall](https://github.com/charles-randall)
* PHP: [Max Semenik](https://github.com/MaxSem)
* Ruby: [Bill Mill](https://github.com/llimllib), with input from [Niklas](https://github.com/nhh)
* Rust: [Andrew Gallant](https://github.com/BurntSushi)
* Swift: [Daniel Müllenborn](https://github.com/damuellen)
* Tcl: [William Ross](https://github.com/arideden)
* Zig: [ifreund](https://github.com/ifreund) and [matu3ba](https://github.com/matu3ba) and [ansingh](https://github.com/ansingh)

See other versions [on Rosetta Code](https://rosettacode.org/wiki/Word_frequency).
