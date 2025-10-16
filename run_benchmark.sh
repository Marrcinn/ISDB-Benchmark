#!/bin/bash

# File I/O Performance Benchmark Script
# Tests read_file.c with files of different sizes and saves results with timestamps

set -e  # Exit on any error

# Configuration
SIZES=("1MB" "17MB" "1GB" "32GB" "64GB")
TEST_DIR="test_files"
RESULT_DIR="result"
TIMESTAMP=$(date +"%Y-%m-%d_%H-%M-%S")

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if required tools are available
check_dependencies() {
    print_status "Checking dependencies..."
    
    if ! command -v python3 &> /dev/null; then
        print_error "python3 is required but not installed"
        exit 1
    fi
    
    if ! command -v make &> /dev/null; then
        print_error "make is required but not installed"
        exit 1
    fi
    
    if ! command -v gcc &> /dev/null; then
        print_error "gcc is required but not installed"
        exit 1
    fi
    
    print_success "All dependencies are available"
}

# Create necessary directories
setup_directories() {
    print_status "Setting up directories..."
    
    mkdir -p "$TEST_DIR"
    mkdir -p "$RESULT_DIR"
    
    print_success "Directories created: $TEST_DIR, $RESULT_DIR"
}

# Compile the read_file program
compile_program() {
    print_status "Compiling read_file program..."
    
    if make clean && make; then
        print_success "Compilation completed successfully"
    else
        print_error "Compilation failed"
        exit 1
    fi
}

# Generate test files
generate_test_files() {
    print_status "Generating test files..."
    
    for size in "${SIZES[@]}"; do
        filename="test_${size,,}.bin"  # Convert to lowercase
        print_status "Generating $filename ($size)..."
        
        if python3 file_generation.py "$filename" "$size"; then
            print_success "Generated $filename"
        else
            print_error "Failed to generate $filename"
            exit 1
        fi
    done
}

# Run benchmark on a single file
run_benchmark() {
    local filename="$1"
    local size="$2"
    local result_file="$RESULT_DIR/${TIMESTAMP}_${size,,}_result.txt"
    
    print_status "Running benchmark on $filename ($size)..."
    
    # Run read_file and capture all output
    if ./read_file "$TEST_DIR/$filename" > "$result_file" 2>&1; then
        print_success "Benchmark completed for $filename"
        print_status "Results saved to: $result_file"
    else
        print_error "Benchmark failed for $filename"
        # Still save the error output
        print_status "Error output saved to: $result_file"
    fi
}

# Run all benchmarks
run_all_benchmarks() {
    print_status "Starting benchmark runs..."
    echo "=========================================="
    
    for size in "${SIZES[@]}"; do
        filename="test_${size,,}.bin"
        run_benchmark "$filename" "$size"
        echo "------------------------------------------"
    done
}

# Print summary
print_summary() {
    print_success "Benchmark script completed!"
    echo
    print_status "Generated files:"
    for size in "${SIZES[@]}"; do
        filename="test_${size,,}.bin"
        if [ -f "$TEST_DIR/$filename" ]; then
            file_size=$(du -h "$TEST_DIR/$filename" | cut -f1)
            echo "  - $filename ($file_size)"
        fi
    done
    
    echo
    print_status "Result files:"
    for size in "${SIZES[@]}"; do
        result_file="$RESULT_DIR/${TIMESTAMP}_${size,,}_result.txt"
        if [ -f "$result_file" ]; then
            echo "  - $result_file"
        fi
    done
    
    echo
    print_status "To view results, use:"
    for size in "${SIZES[@]}"; do
        result_file="$RESULT_DIR/${TIMESTAMP}_${size,,}_result.txt"
        if [ -f "$result_file" ]; then
            echo "  cat $result_file"
        fi
    done
}

# Main execution
main() {
    echo "=========================================="
    echo "File I/O Performance Benchmark"
    echo "=========================================="
    echo "Timestamp: $TIMESTAMP"
    echo "Test sizes: ${SIZES[*]}"
    echo "=========================================="
    echo
    
    check_dependencies
    setup_directories
    compile_program
    generate_test_files
    run_all_benchmarks
    print_summary
}

# Run main function
main "$@"
