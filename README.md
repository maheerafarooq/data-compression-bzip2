# BZip2 Course Project Implementation

A simplified BZip2 compression pipeline written in C, satisfying every stage,
data structure, and prototype required by the project description.

> Course: Data Compression — Instructor: Dr. Faisal Aslam
> Date: April 14, 2026

## Repository Layout (matches submission guidelines §9.1)

```
project-bzip2/
├── src/
│   ├── main.c           # CLI / menu / batch driver
│   ├── pipeline.c       # End-to-end pipeline orchestration
│   ├── block.c          # §3.1  block division and reassembly
│   ├── rle1.c           # §3.2  Run-Length Encoding (stage 1)
│   ├── bwt.c            # §3.3  Burrows–Wheeler Transform (matrix + suffix array)
│   ├── mtf.c            # §4.1  Move-to-Front transform
│   ├── rle2.c           # §4.2  RLE-2 for MTF output
│   ├── huffman.c        # §5.1  Canonical Huffman coding
│   ├── range.c          # §8.3  alternative entropy experiment
│   └── config.c         # §2.2  config.ini parser
├── include/
│   ├── block.h, bwt.h, rle1.h, rle2.h, mtf.h,
│   ├── huffman.h, range.h, pipeline.h, config.h
│   └── bzip2.h          # umbrella header
├── benchmarks/          # Calgary / Canterbury / Silesia / large files
├── results/
│   ├── results.csv      # §7.3 output produced by the C executable
│   └── compression_ratio.png  # §9.3 plot produced by benchmark.py
├── Makefile             # §6.1  all / clean / windows
├── config.ini           # §2.2  block_size, stage toggles, paths
├── benchmark.py         # §9.3  matplotlib chart generator
├── README.md            # this file
├── project.pdf          # generated project report (see `make_report.py`)
└── make_report.py       # builds `project.pdf` from results/results.csv
```

## Features (complete, per §1.1 and §2.1)

The encoding pipeline reproduces the BZip2 chain end-to-end:

```
Input File → Block Division → RLE-1 → BWT → MTF → RLE-2 → Huffman → Compressed Output
```

Decoding inverts every stage. Each stage encode + decode pair is independently
testable from the menu.

| Stage | Component                       | Source       | Headers / Data structures (§spec) |
|-------|---------------------------------|--------------|-----------------------------------|
| 1     | `BlockManager` / `Block`        | `block.c`    | `block.h` (§3.1.2)                |
| 1     | RLE-1 encode/decode             | `rle1.c`     | `rle1.h` (§3.2)                   |
| 1     | BWT (matrix) + `Rotation` struct| `bwt.c`      | `bwt.h` (§3.3)                    |
| 2     | MTF encode/decode               | `mtf.c`      | `mtf.h` (§4.1)                    |
| 2     | RLE-2 encode/decode             | `rle2.c`     | `rle2.h` (§4.2)                   |
| 3     | Canonical Huffman + `HuffmanCode`/`HuffmanNode` | `huffman.c` | `huffman.h` (§5.1.2/§5.1.3) |
| §8.1  | Threshold / adaptive / RLE+entropy | `rle1.c`  | `rle1.h`                          |
| §8.2  | Suffix-array BWT (`build_suffix_array`) | `bwt.c` | `bwt.h`                          |
| §8.3  | Alternative entropy experiment  | `range.c`    | `range.h`                         |

Implementation notes:

- **Block division**: streams the input file with `fread`, slicing into blocks
  of `block_size` (validated to 100 KB – 900 KB per spec). The default run
  uses 900 000 (max) for best ratios.
- **RLE-1**: binary-safe, escape-byte format `0xFF, byte, count`; correctly
  encodes single `0xFF` bytes (stored as `0xFF, 0xFF, 1`).
- **BWT (matrix)**: builds and qsorts cyclic rotations using the spec
  `compare_rotations`; outputs the last column and a primary index. The
  inverse uses the standard LF-mapping (next-array) to reconstruct in O(n).
- **BWT (suffix array, §8.2)**: prefix-doubling (Manber–Myers), O(n log² n);
  used for benchmark runs because it is dramatically faster than the naive
  matrix BWT on large blocks. Selected via `bwt_type = suffix_array` in
  `config.ini`.
- **MTF**: keeps a 256-symbol list in an array; encode emits the index then
  moves the symbol to the front; decode is the exact inverse.
- **RLE-2**: targets MTF output (lots of zeros) by run-length-encoding zero
  runs as `0x00, run`; non-zero bytes are copied literally.
- **Canonical Huffman (§5.1)**: uses the exact spec types — `HuffmanNode`
  (`unsigned char symbol; int freq; ...`) and `HuffmanCode`
  (`unsigned short code; unsigned char length;`). Emits a self-describing
  per-block header with a `H U F 1` magic, original size, and 256-entry
  frequency table; decode rebuilds the tree from the same frequencies.
- **Pipeline container (`BZP1`)**: blocks are wrapped in a small package
  header (magic + version + num_blocks; per-block `(plain_size, chunk_size)`)
  so files larger than `block_size` are handled correctly.

## Build (cross-platform per §6.1)

Required Makefile targets are provided: **`all`**, **`clean`**, **`windows`**.

### Linux / macOS

```bash
make           # build  → ./bzip2_impl
./bzip2_impl   # default action: full pipeline on benchmarks/ → results/results.csv
```

### Windows (MSYS2 / MinGW-w64)

```bash
mingw32-make
./bzip2_impl.exe
```

### Windows (cmd.exe + mingw32-make)

```cmd
mingw32-make
bzip2_impl.exe
```

### Cross-build (Linux → Windows)

```bash
make windows CC=x86_64-w64-mingw32-gcc
```

The Makefile auto-detects Windows (`OS=Windows_NT`), adds the `.exe` suffix,
and uses `del /q` for `clean` on plain Windows shells.

## Configuration (§2.2)

`config.ini` controls the pipeline:

```ini
[ General ]
block_size = 900000          ; bytes (100 KB – 900 KB)
rle1_enabled = true
bwt_type = suffix_array      ; matrix | suffix_array
mtf_enabled = true
rle2_enabled = true
huffman_enabled = true

[ Performance ]
benchmark_mode = false
output_metrics = true

[ Paths ]
input_directory = ./benchmarks/
output_directory = ./results/
```

## Usage

### 1. Default — batch the whole corpus

```bash
./bzip2_impl
```

Walks `input_directory`, runs the full pipeline (encode → decode → verify) on
every file, and writes `results/results.csv` in the format mandated by §7.3:

```csv
File,Size,BlockSize,CompressionRatio,Time,Memory
```

`CompressionRatio` is reported as **percent saved** = 100 × (1 − compressed/original).

### 2. Single-file CLI benchmark

```bash
./bzip2_impl --benchmark <file> <block-size> [csv-path]
# example
./bzip2_impl --benchmark benchmarks/alice29.txt 900000 results/alice.csv
```

### 3. Interactive menu (per-stage tests)

```bash
./bzip2_impl --menu
```

Menu options exercise each stage and the extras independently:

| #  | Option                                              | Spec   |
|----|-----------------------------------------------------|--------|
| 1  | RLE-1 encode / decode                               | §3.2   |
| 2  | BWT (matrix) encode / decode                        | §3.3   |
| 3  | Block divide / reassemble                           | §3.1   |
| 4  | Config parsing                                      | §2.2   |
| 5  | Stage-1 pipeline with intermediate outputs          | §3.4   |
| 6  | MTF encode / decode                                 | §4.1   |
| 7  | RLE-2 encode / decode                               | §4.2   |
| 8  | Huffman encode / decode                             | §5.1   |
| 9  | Full pipeline with all intermediate outputs         | §4.3 + §5 |
| 10 | Enhanced RLE variants                               | §8.1   |
| 11 | Suffix-array BWT                                    | §8.2   |
| 12 | Alternative entropy experiment                      | §8.3   |
| 13 | Batch benchmarks/ → results/results.csv             | §7.3   |
| 14 | Single-file benchmark (interactive)                 | §7.3   |

For options 1, 2, 6, 7, 8, 10, 11, 12 each test offers three modes:
`1) encode only · 2) decode only · 3) encode + decode + verify`.

### 4. Plot the chart (§9.3)

```bash
python3 benchmark.py --no-run            # use the existing CSV
python3 benchmark.py                     # also build + run the C target first
python3 benchmark.py --no-bzip2          # skip the bzip2 reference line
```

Output: `results/compression_ratio.png` — bars for **Our impl**, with a line
for **bzip2 -9** if `bzip2` is on PATH (used for §7.2 reference comparison).

### 5. Generate the report PDF

```bash
python3 make_report.py
```

Reads `results/results.csv` and writes `project.pdf` in the repository root.
The PDF contains a cover page, per-stage write-up, the configuration, the
results table, the compression chart, and the extras section — i.e. it is the
deliverable report for the submission.

## Performance Results

Running on the full benchmarks/ corpus (Calgary + Canterbury + Silesia + extra
large files) with `block_size = 900000` and `bwt_type = suffix_array`:

| Subset       | Files | Total bytes | Ratio (saved) | Time      |
|--------------|-------|-------------|---------------|-----------|
| Small text (Canterbury / Calgary) | 22 | < 5 MB        | 60–93 %       | < 1 s each|
| Medium binary (kennedy.xls, geo, obj1/2, sum, ptt5/pic) | 7 | ~3 MB | 36–90 % | < 1 s each |
| Large text (book1, plrabn12, lcet10, dickens, large_text, webster, nci, reymont) | many | tens of MB | 66–93 % | seconds–minutes |
| Random binary (large_binary.bin) | 1 | 10 MB | -1.21 % (incompressible) | ~5 s |

See `results/results.csv` for the full table and `results/compression_ratio.png`
for the bar chart. The script `display_csv_results.sh` (Linux) prints the CSV
nicely:

```bash
./display_csv_results.sh
```

### Performance score (§7.2)

```
Score = w1 × C_ref / C  +  w2 × S / S_ref
```

with `C` = our compression ratio, `C_ref` = `bzip2 -9` ratio, `S` = our
throughput (MB/s), `S_ref` = bzip2 throughput. The chart shows that on text
corpora our ratio tracks bzip2 within a couple of percent.

## Extra Features (§8)

| Section | What is delivered                                                  |
|---------|--------------------------------------------------------------------|
| §8.1    | `rle1_encode_threshold`, `rle1_encode_adaptive`, `rle1_entropy_pipeline_encode/decode` (RLE + Huffman) |
| §8.2    | `build_suffix_array` + `bwt_encode_suffix_array` (prefix doubling) |
| §8.3    | `range_encode` / `range_decode` — experimental container based on a frequency model, exposed via menu option 12 |

## Notes for the Grader

- All Stage 1, 2, and 3 evaluation criteria are exercised by the CLI batch
  run and individually verifiable from the interactive menu.
- One command reproduces every deliverable (`results.csv`, chart, and the
  report `project.pdf`):

  ```bash
  ./bzip2_impl && python3 benchmark.py --no-run && python3 make_report.py
  ```

  On Windows / MSYS2:

  ```bash
  mingw32-make && ./bzip2_impl.exe && python benchmark.py --no-run && python make_report.py
  ```
- Tested on Ubuntu 24.04 (gcc 13) and Windows 11 / MSYS2 (MinGW-w64).
- For very small files (<1 KB) the per-block Huffman header dominates and
  the savings can be negative; this is expected and matches the BZip2
  reference behaviour.
