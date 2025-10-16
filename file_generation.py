import os
import random
import time
import sys
import argparse

def generate_random_file(filename, size_bytes):
    """
    Generate a file with random data of specified size.
    
    Args:
        filename (str): Name of the file to create
        size_bytes (int): Size of the file in bytes
    """
    print(f"Generating {filename} ({size_bytes / (1024**3):.2f} GB)...")
    start_time = time.time()
    
    # Use a buffer size for efficient writing
    buffer_size = 1024 * 1024  # 1MB buffer

    # If the file already exists, skip it
    if os.path.exists("test_files/" + filename):
        print(f"{filename} already exists, skipping...")
        return
    
    with open("test_files/" + filename, 'wb') as f:
        bytes_written = 0
        while bytes_written < size_bytes:
            # Calculate how many bytes to write in this iteration
            remaining_bytes = size_bytes - bytes_written
            write_size = min(buffer_size, remaining_bytes)
            
            # Generate random data
            random_data = os.urandom(write_size)
            f.write(random_data)
            bytes_written += write_size
            
            # Show progress for large files
            if size_bytes > 1024**3:  # For files > 1GB
                progress = (bytes_written / size_bytes) * 100
                print(f"\rProgress: {progress:.1f}%", end='', flush=True)
    
    if size_bytes > 1024**3:
        print()  # New line after progress
    
    end_time = time.time()
    print(f"Completed {filename} in {end_time - start_time:.2f} seconds")

def parse_size(size_str):
    """
    Parse size string with MB/GB postfixes and return size in bytes.
    
    Args:
        size_str (str): Size string (e.g., "1MB", "2.5GB", "1000")
    
    Returns:
        int: Size in bytes
    """
    size_str = size_str.upper().strip()
    
    if size_str.endswith('GB'):
        try:
            value = float(size_str[:-2])
            return int(value * 1024 * 1024 * 1024)
        except ValueError:
            raise ValueError(f"Invalid size format: {size_str}")
    elif size_str.endswith('MB'):
        try:
            value = float(size_str[:-2])
            return int(value * 1024 * 1024)
        except ValueError:
            raise ValueError(f"Invalid size format: {size_str}")
    else:
        try:
            return int(size_str)
        except ValueError:
            raise ValueError(f"Invalid size format: {size_str}. Use format like '1MB', '2.5GB', or plain number for bytes")

def main():
    """Generate a file with specified filename and size."""
    parser = argparse.ArgumentParser(description='Generate a file with random data of specified size')
    parser.add_argument('filename', help='Name of the file to create')
    parser.add_argument('size', help='Size of the file (e.g., 1MB, 2.5GB, 1000 for bytes)')
    
    args = parser.parse_args()
    
    try:
        size_bytes = parse_size(args.size)
    except ValueError as e:
        print(f"Error: {e}")
        sys.exit(1)
    
    print("Starting file generation...")
    print("=" * 50)
    
    try:
        generate_random_file(args.filename, size_bytes)
        print(f"Successfully created {args.filename}")
    except Exception as e:
        print(f"Error creating {args.filename}: {e}")
        sys.exit(1)
    
    print("File generation completed!")

if __name__ == "__main__":
    main()
