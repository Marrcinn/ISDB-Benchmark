# File I/O Performance Benchmark Tool

A comprehensive C-based benchmarking tool that compares different file reading strategies and I/O patterns to measure performance across various file sizes and access methods.

## Overview

This project provides a detailed analysis of file I/O performance using multiple approaches:
- **Sequential vs Random access patterns**
- **Standard I/O vs Memory mapping**
- **Single-threaded vs Multi-threaded processing**
- **Order-independent hashing using CRC64 with XOR**

The tool is designed to help understand how different I/O strategies perform across various file sizes, from small files (1MB) to very large files (64GB+).

## Project Structure

```
├── read_file.c          # Main benchmarking program
├── crc64_simple.c       # CRC64 implementation
├── crc64_simple.h       # CRC64 header file
├── file_generation.py   # Test file generator
├── Makefile            # Build configuration
├── run_benchmark.sh    # Automated benchmark runner
├── test_files/         # Generated test files
└── result/             # Benchmark results
```

---

## read_file.c

The main benchmarking program that implements five different file reading strategies:

### Features

- **Sequential Read**: Standard file reading using `fread()` in 16MB blocks
- **Random Read**: Alternating access pattern from file ends toward center
- **Sequential Memory Mapping**: Memory-mapped file access with sequential processing
- **Random Memory Mapping**: Memory-mapped file with alternating access pattern
- **Async Sequential Read**: Multi-threaded producer-consumer pattern with parallel readers and processors

### Key Components

- **Order-Independent Hashing**: Uses CRC64 with XOR operation to allow parallel processing while maintaining consistent results
- **High-Resolution Timing**: Uses `clock_gettime()` for precise performance measurements
- **Thread-Safe Operations**: Implements proper synchronization for multi-threaded processing
- **Configurable Verbosity**: Three levels of output detail (times only, times+checksums, debug)

### Usage

```bash
./read_file [options] <file>
  -v, --verbose LEVEL  Set verbosity level (0-2, default: 1)
  -h, --help           Show help message
```

### Performance Metrics

The program measures and reports:
- Execution time for each reading method
- CRC64 checksums for data integrity verification
- Total bytes processed
- Thread synchronization statistics (in debug mode)

---

## crc64_simple

A lightweight CRC64 implementation optimized for performance benchmarking.

### Implementation Details

- **Polynomial**: Uses ECMA-182 standard polynomial (`0x42F0E1EBA9EA3693`)
- **Lookup Table**: Pre-computed 256-entry table for fast byte-by-byte processing
- **Lazy Initialization**: Tables are computed only when first needed
- **Memory Efficient**: Minimal memory footprint with static table storage

### API

```c
void crc64_init(void);                                    // Initialize lookup tables
uint64_t crc64_compute(const unsigned char *data, size_t len);  // Compute CRC64
```

### Design Rationale

The CRC64 implementation is designed for:
- **Speed**: Table-driven approach for maximum performance
- **Simplicity**: Minimal dependencies and straightforward API
- **Reliability**: Well-tested ECMA-182 polynomial
- **Order Independence**: When combined with XOR, allows parallel processing

---

## file_generation.py

A Python utility for generating test files with random data of specified sizes.

### Features

- **Flexible Size Specification**: Supports MB, GB, and byte formats
- **Progress Tracking**: Real-time progress display for large files
- **Efficient Generation**: Uses `os.urandom()` with 1MB buffers
- **Duplicate Prevention**: Skips generation if file already exists
- **Error Handling**: Comprehensive error checking and user feedback

### Usage

```bash
python3 file_generation.py <filename> <size>
```

### Examples

```bash
python3 file_generation.py test_1mb.bin 1MB
python3 file_generation.py test_2gb.bin 2.5GB
python3 file_generation.py test_large.bin 1000000000
```

### Size Format Support

- **MB**: `1MB`, `17MB`, `2.5MB`
- **GB**: `1GB`, `32GB`, `0.5GB`
- **Bytes**: `1048576`, `1000000000`

---

## Makefile

A comprehensive build system that manages compilation, testing, and cleanup operations.

### Targets

- **`all`** (default): Compile the main program with CRC64 library
- **`test_files`**: Create the test_files directory
- **`run`**: Compile and run the program
- **`clean`**: Remove compiled files
- **`test-clean`**: Remove test files and directory
- **`clean-all`**: Remove everything (compiled and test files)
- **`help`**: Display available targets and usage information

### Configuration

- **Compiler**: GCC with optimization flags (`-O2`)
- **Libraries**: pthread for multi-threading support
- **Warnings**: Full warning detection (`-Wall -Wextra`)
- **Debug Info**: Includes debugging symbols (`-g`)

### Usage

```bash
make                    # Compile the program
make clean             # Clean compiled files
make test-clean        # Clean test files
make clean-all         # Clean everything
make help              # Show help
```

---

## run_benchmark.sh

An automated benchmark runner that orchestrates the entire testing process.

### Features

- **Automated Workflow**: Complete end-to-end testing process
- **Multiple File Sizes**: Tests 1MB, 17MB, 1GB, 32GB, and 64GB files
- **Timestamped Results**: Saves results with timestamps for historical tracking
- **Dependency Checking**: Verifies required tools are available
- **Progress Tracking**: Colored output with status indicators
- **Error Handling**: Comprehensive error checking and reporting

### Test Sizes

The script automatically tests the following file sizes:
- **1MB**: Small file baseline
- **17MB**: Medium file with multiple blocks
- **1GB**: Large file for significant I/O testing
- **32GB**: Very large file for stress testing
- **64GB**: Extreme size for maximum I/O load

### Output

Results are saved in the `result/` directory with format:
```
result/YYYY-MM-DD_HH-MM-SS_<size>_result.txt
```

### Usage

```bash
chmod +x run_benchmark.sh
./run_benchmark.sh
```

### Prerequisites

The script checks for and requires:
- `python3` for file generation
- `make` for compilation
- `gcc` for C compilation
- Sufficient disk space for test files

---

## Getting Started

1. **Clone or download the project**
2. **Make the benchmark script executable**:
   ```bash
   chmod +x run_benchmark.sh
   ```
3. **Run the complete benchmark suite**:
   ```bash
   ./run_benchmark.sh
   ```
4. **View results**:
   ```bash
   cat result/*_result.txt
   ```

## Requirements

- **Operating System**: Linux/Unix (uses POSIX APIs)
- **Compiler**: GCC with pthread support
- **Python**: Python 3.x for file generation
- **Disk Space**: ~100GB+ for full test suite
- **Memory**: Sufficient RAM for memory mapping large files

## Performance Considerations

- **Block Size**: Optimized for 16MB blocks (configurable in source)
- **Threading**: Uses 4 reader threads and 4 consumer threads
- **Memory**: Memory-mapped files for large file access
- **Caching**: OS file system caching affects results

## License

This project is provided as-is for educational and benchmarking purposes.
