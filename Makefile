# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS = -lpthread

PYTHON = python3

# Directories
TEST_DIR = test_files
SRC_DIR = .

# Source files
SOURCES = read_file.c crc64_simple.c
TARGET = read_file

# Test files (no longer generated automatically)

# Default target
all: $(TARGET)

# Compile the C program with simple CRC64 library
$(TARGET): $(SOURCES)
	@echo "Compiling $(TARGET) with simple CRC64 library..."
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)
	@echo "Compilation completed successfully!"

# Create test_files directory (if needed)
test_files: $(TEST_DIR)

# Create test_files directory
$(TEST_DIR):
	@echo "Creating test_files directory..."
	mkdir -p $(TEST_DIR)

# Run the program
run: $(TARGET)
	@echo "Running $(TARGET)..."
	./$(TARGET)

# Clean compiled files
clean:
	@echo "Cleaning compiled files..."
	rm -f $(TARGET)
	@echo "Clean completed!"

# Clean test files and directory
test-clean: clean
	@echo "Removing test files and directory..."
	rm -rf $(TEST_DIR)
	@echo "Distribution clean completed!"

# Clean everything (both compiled files and test files)
clean-all: test-clean

# Help target
help:
	@echo "Available targets:"
	@echo "  all        - Compile the program (default)"
	@echo "  test_files - Create test_files directory"
	@echo "  run        - Compile and run the program"
	@echo "  clean      - Remove compiled files"
	@echo "  test-clean - Remove test files and directory"
	@echo "  clean-all  - Remove everything (compiled files and test files)"
	@echo "  help       - Show this help message"
	@echo ""
	@echo "To generate test files, use: python3 file_generation.py <filename> <size>"
	@echo "  Example: python3 file_generation.py test.bin 1MB"
	@echo "  Example: python3 file_generation.py large.bin 2.5GB"

# Phony targets
.PHONY: all test_files run clean test-clean clean-all help
