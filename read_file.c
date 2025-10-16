/*
 * File I/O Performance Benchmark Tool
 * 
 * Compares different file reading strategies:
 * - Sequential vs Random access patterns
 * - Standard I/O vs Memory mapping
 * - Single-threaded vs Multi-threaded processing
 * 
 * Uses CRC64 with XOR for order-independent hashing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include "crc64_simple.h"
    
typedef char* String;

// Configuration constants
#define BLOCK_SIZE (16 * 1024 * 1024)  // 16MB blocks for optimal I/O
#define CRC64_LEN 8
#define MAX_QUEUE_SIZE 16  // Buffer queue capacity
#define NUM_READERS 4      // Parallel reader threads
#define NUM_CONSUMERS 4    // Parallel processor threads


// Verbosity levels: 0=times only, 1=times+checksums, 2=debug output
int verbosity = 1;

// Buffer node for producer-consumer queue
typedef struct BufferNode {
    unsigned char *data;
    size_t size;
    struct BufferNode *next;
} BufferNode;

// Thread-safe queue for async processing
typedef struct {
    BufferNode *head;
    BufferNode *tail;
    int count;
    pthread_mutex_t mutex;
    sem_t empty_slots;  // Available queue capacity
    sem_t full_slots;   // Available items to process
    int reading_done;
    int active_readers;
    size_t total_blocks;   // total number of BLOCK_SIZE-aligned blocks
    size_t next_block;     // next block index to assign to a reader
    size_t file_size;      // file size for last block size calculation
} BufferQueue;

// Arguments passed to reader threads
typedef struct {
    String filename;
    BufferQueue *queue;
    size_t start_offset;
    size_t end_offset;
    int reader_id;
} ReaderArgs;

// Global state
static uint64_t global_hash_xor = 0;  // XOR allows order-independent hashing
static pthread_mutex_t hash_mutex = PTHREAD_MUTEX_INITIALIZER;

// High-resolution timing utilities
static inline struct timespec timer_start() {
    struct timespec start;
    clock_gettime(CLOCK_REALTIME, &start);
    return start;
}

static inline void timer_end_print(const char *label, struct timespec start) {
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);
    double time_taken = (end.tv_sec - start.tv_sec) +
                        (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("%s: %f seconds\n", label, time_taken);
}

// File size validation and error handling
static int get_file_size(const char *filename, size_t *file_size) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        if (verbosity >= 2) {
            printf("Error: Cannot open file %s\n", filename);
        }
        return 0;
    }
    
    fseek(file, 0, SEEK_END);
    *file_size = ftell(file);
    fclose(file);
    
    if (*file_size == 0) {
        if (verbosity >= 2) {
            printf("File is empty\n");
        }
        return 0;
    }
    
    if (verbosity >= 2) {
        printf("File size: %zu bytes\n", *file_size);
    }
    
    return 1;
}

// Common setup for all reading functions
static void setup_hashing(void) {
    crc64_init();
}

// Common output for all reading functions
static void print_results(const char *method_name, uint64_t hash, size_t total_bytes, struct timespec start_time) {
    if (verbosity >= 1) {
        printf("Hash (XOR): %016llx\n", (unsigned long long)hash);
    }
    if (verbosity >= 2) {
        printf("Total bytes processed: %zu\n", total_bytes);
    }
    
    timer_end_print(method_name, start_time);
}

// Process a single block and update XOR of per-block CRCs (order-independent)
static inline void process_block_xor(const unsigned char *data, size_t size, uint64_t *hash_xor) {
    uint64_t block_hash = crc64_compute(data, size);
    *hash_xor ^= block_hash;
}

// Memory-mapped file operations
static void* map_file(const char *filename, size_t *file_size) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        if (verbosity >= 2) {
            printf("Error: Cannot open file %s\n", filename);
        }
        return NULL;
    }
    
    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1) {
        if (verbosity >= 2) {
            printf("Error: Cannot get file size\n");
        }
        close(fd);
        return NULL;
    }
    
    *file_size = file_stat.st_size;
    if (*file_size == 0) {
        if (verbosity >= 2) {
            printf("File is empty\n");
        }
        close(fd);
        return NULL;
    }
    
    if (verbosity >= 2) {
        printf("File size: %zu bytes\n", *file_size);
    }
    
    void *mapped_file = mmap(NULL, *file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped_file == MAP_FAILED) {
        if (verbosity >= 2) {
            printf("Error: Cannot map file\n");
        }
        close(fd);
        return NULL;
    }
    
    close(fd);
    return mapped_file;
}

static void unmap_file(void *mapped_file, size_t file_size) {
    munmap(mapped_file, file_size);
}


// Forward declarations for async processing
void* process_buffers(void *arg);
void* reader_thread(void *arg);

// Buffer queue management
void init_buffer_queue(BufferQueue *queue) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->reading_done = 0;
    queue->active_readers = 0;
    pthread_mutex_init(&queue->mutex, NULL);
    sem_init(&queue->empty_slots, 0, MAX_QUEUE_SIZE);
    sem_init(&queue->full_slots, 0, 0);
    queue->total_blocks = 0;
    queue->next_block = 0;
    queue->file_size = 0;
}

void cleanup_buffer_queue(BufferQueue *queue) {
    BufferNode *node = queue->head;
    while (node) {
        BufferNode *next = node->next;
        free(node->data);
        free(node);
        node = next;
    }
    pthread_mutex_destroy(&queue->mutex);
    sem_destroy(&queue->empty_slots);
    sem_destroy(&queue->full_slots);
}

// Producer: add buffer to queue
int enqueue_buffer(BufferQueue *queue, const unsigned char *data, size_t size) {
    sem_wait(&queue->empty_slots);
    pthread_mutex_lock(&queue->mutex);
    
    BufferNode *node = (BufferNode*)malloc(sizeof(BufferNode));
    if (!node) {
        pthread_mutex_unlock(&queue->mutex);
        sem_post(&queue->empty_slots);
        return 0;
    }
    
    node->data = (unsigned char*)malloc(size);
    if (!node->data) {
        free(node);
        pthread_mutex_unlock(&queue->mutex);
        sem_post(&queue->empty_slots);
        return 0;
    }
    
    memcpy(node->data, data, size);
    node->size = size;
    node->next = NULL;
    
    // Add to end of queue
    if (queue->tail) {
        queue->tail->next = node;
    } else {
        queue->head = node;
    }
    queue->tail = node;
    queue->count++;
    
    pthread_mutex_unlock(&queue->mutex);
    sem_post(&queue->full_slots);
    return 1;
}

// Consumer: get buffer from queue
int dequeue_buffer(BufferQueue *queue, unsigned char **data, size_t *size) {
    sem_wait(&queue->full_slots);
    pthread_mutex_lock(&queue->mutex);
    
    // Check if reading is complete and queue is empty
    if (queue->count == 0) {
        int done = queue->reading_done && queue->active_readers == 0;
        pthread_mutex_unlock(&queue->mutex);
        if (!done) {
            sem_post(&queue->full_slots);
        }
        return 0;
    }
    
    // Remove first node
    BufferNode *node = queue->head;
    queue->head = node->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    queue->count--;
    
    pthread_mutex_unlock(&queue->mutex);
    sem_post(&queue->empty_slots);
    
    *data = node->data;
    *size = node->size;
    free(node);
    return 1;
}

// Data processing
void process_buffer_data(const unsigned char *data, size_t size) {
    uint64_t block_hash = crc64_compute(data, size);
    
    // XOR allows order-independent hashing for parallel processing
    pthread_mutex_lock(&hash_mutex);
    global_hash_xor ^= block_hash;
    pthread_mutex_unlock(&hash_mutex);
    
    if (verbosity >= 2) {
        printf("Processed buffer: %zu bytes, block_hash: %016llx\n", 
               size, (unsigned long long)block_hash);
    }
}

// ============================================================================
// File Reading Functions
// ============================================================================

// Multi-threaded async reading with producer-consumer pattern
void async_sequential_read(String filename) {
    struct timespec t0;
    size_t file_size;
    
    if (verbosity >= 2) {
        printf("Async sequential read with %d readers and %d consumers: %s\n", 
               NUM_READERS, NUM_CONSUMERS, filename);
    }
    
    if (!get_file_size(filename, &file_size)) {
        return;
    }
    
    // Setup
    BufferQueue queue;
    init_buffer_queue(&queue);
    setup_hashing();
    global_hash_xor = 0;
    queue.file_size = file_size;
    queue.total_blocks = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    queue.next_block = 0;
    
    // Start consumer threads first
    pthread_t consumer_threads[NUM_CONSUMERS];
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        int thread_rc = pthread_create(&consumer_threads[i], NULL, process_buffers, &queue);
        if (thread_rc != 0) {
            if (verbosity >= 2) {
                fprintf(stderr, "Error: Failed to create consumer thread %d (%d)\n", i, thread_rc);
            }
            cleanup_buffer_queue(&queue);
            return;
        }
        if (verbosity >= 2) {
            printf("Created consumer thread %d\n", i);
        }
    }
    
    // Start timing after setup
    t0 = timer_start();
    
    // Create reader threads that will pull BLOCK_SIZE-aligned blocks round-robin
    pthread_t reader_threads[NUM_READERS];
    queue.active_readers = NUM_READERS;
    for (int i = 0; i < NUM_READERS; i++) {
        ReaderArgs *args = (ReaderArgs*)malloc(sizeof(ReaderArgs));
        args->filename = filename;
        args->queue = &queue;
        args->reader_id = i;
        args->start_offset = 0; // unused in new scheme
        args->end_offset = 0;   // unused in new scheme
        
        int thread_rc = pthread_create(&reader_threads[i], NULL, reader_thread, args);
        if (thread_rc != 0) {
            if (verbosity >= 2) {
                fprintf(stderr, "Error: Failed to create reader thread %d (%d)\n", i, thread_rc);
            }
            pthread_mutex_lock(&queue.mutex);
            queue.active_readers--;
            pthread_mutex_unlock(&queue.mutex);
            free(args);
        } else {
            if (verbosity >= 2) {
                printf("Created reader thread %d (offset %zu to %zu)\n", 
                       i, args->start_offset, args->end_offset);
            }
        }
    }
    
    // Wait for all readers to finish
    for (int i = 0; i < NUM_READERS; i++) {
        pthread_join(reader_threads[i], NULL);
    }
    
    if (verbosity >= 2) {
        printf("All reader threads completed\n");
    }
    
    // Wake up consumers to check for completion
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        sem_post(&queue.full_slots);
    }
    
    // Wait for all consumers to finish
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_join(consumer_threads[i], NULL);
    }
    
    if (verbosity >= 2) {
        printf("All consumer threads completed\n");
    }
    
    // Output results
    uint64_t final_hash = global_hash_xor;
    if (verbosity >= 1) {
        printf("Hash (XOR): %016llx\n", (unsigned long long)final_hash);
    }
    if (verbosity >= 2) {
        printf("Total file size: %zu bytes\n", file_size);
    }
    
    timer_end_print("Async sequential read", t0);
    cleanup_buffer_queue(&queue);
}

// Reader thread: reads assigned file portion and enqueues data
void* reader_thread(void *arg) {
    ReaderArgs *args = (ReaderArgs*)arg;
    
    FILE *file = fopen(args->filename, "rb");
    if (!file) {
        if (verbosity >= 2) {
            printf("Reader %d: Error opening file %s\n", args->reader_id, args->filename);
        }
        pthread_mutex_lock(&args->queue->mutex);
        args->queue->active_readers--;
        pthread_mutex_unlock(&args->queue->mutex);
        free(args);
        return NULL;
    }
    
    unsigned char *read_buffer = malloc(BLOCK_SIZE);
    if (!read_buffer) {
        if (verbosity >= 2) {
            printf("Reader %d: Error allocating buffer\n", args->reader_id);
        }
        fclose(file);
        pthread_mutex_lock(&args->queue->mutex);
        args->queue->active_readers--;
        pthread_mutex_unlock(&args->queue->mutex);
        free(args);
        return NULL;
    }
    
    size_t bytes_read;
    size_t total_bytes = 0;

    // Dynamically claim next block index and read BLOCK_SIZE-aligned chunks
    while (1) {
        size_t block_index;
        pthread_mutex_lock(&args->queue->mutex);
        if (args->queue->next_block >= args->queue->total_blocks) {
            pthread_mutex_unlock(&args->queue->mutex);
            break;
        }
        block_index = args->queue->next_block++;
        pthread_mutex_unlock(&args->queue->mutex);

        size_t offset = block_index * BLOCK_SIZE;
        size_t bytes_to_read = BLOCK_SIZE;
        if (offset + bytes_to_read > args->queue->file_size) {
            bytes_to_read = args->queue->file_size - offset;
        }

        if (fseek(file, offset, SEEK_SET) != 0) {
            break;
        }
        bytes_read = fread(read_buffer, 1, bytes_to_read, file);
        if (bytes_read == 0) {
            break;
        }

        enqueue_buffer(args->queue, read_buffer, bytes_read);
        total_bytes += bytes_read;

        if (verbosity >= 2) {
            printf("Reader %d: Enqueued block %zu (offset %zu, size %zu)\n",
                   args->reader_id, block_index, offset, bytes_read);
        }
    }
    
    if (verbosity >= 2) {
        printf("Reader %d: Completed, read %zu bytes\n", args->reader_id, total_bytes);
    }
    
    free(read_buffer);
    fclose(file);
    
    // Mark reader as done
    pthread_mutex_lock(&args->queue->mutex);
    args->queue->active_readers--;
    if (args->queue->active_readers == 0) {
        args->queue->reading_done = 1;
    }
    pthread_mutex_unlock(&args->queue->mutex);
    
    free(args);
    return NULL;
}

// Consumer thread: processes buffers from queue
void* process_buffers(void *arg) {
    BufferQueue *queue = (BufferQueue*)arg;
    unsigned char *data = NULL;
    size_t size = 0;
    
    while (1) {
        // Check if all reading is complete and queue is empty
        pthread_mutex_lock(&queue->mutex);
        int done = queue->reading_done && queue->active_readers == 0 && queue->count == 0;
        pthread_mutex_unlock(&queue->mutex);
        
        if (done) {
            break;
        }
        
        // Process available buffers
        if (dequeue_buffer(queue, &data, &size)) {
            process_buffer_data(data, size);
            free(data);
        }
    }
    
    return NULL;
}

// Standard sequential file reading
void sequential_read(String filename) {
    struct timespec t0;
    size_t file_size;
    
    if (verbosity >= 2) {
        printf("Sequential read: %s\n", filename);
    }
    
    if (!get_file_size(filename, &file_size)) {
        return;
    }
    
    FILE *file = fopen(filename, "rb");
    if (!file) {
        if (verbosity >= 2) {
            printf("Error: Cannot open file %s\n", filename);
        }
        return;
    }
    
    setup_hashing();
    uint64_t hash_xor = 0;
    
    unsigned char *buffer = malloc(BLOCK_SIZE);
    if (!buffer) {
        if (verbosity >= 2) {
            printf("Error: Cannot allocate memory for buffer\n");
        }
        fclose(file);
        return;
    }
    
    size_t bytes_read;
    size_t total_bytes = 0;
    t0 = timer_start();
    
    // Read and hash file in blocks (order-independent XOR)
    while ((bytes_read = fread(buffer, 1, BLOCK_SIZE, file)) > 0) {
        process_block_xor(buffer, bytes_read, &hash_xor);
        total_bytes += bytes_read;
        
        if (verbosity >= 2) {
            printf("Read %zu bytes (total: %zu)\n", bytes_read, total_bytes);
        }
    }
    
    print_results("Sequential read", hash_xor, total_bytes, t0);
    free(buffer);
    fclose(file);
}

// Random access pattern: alternating from ends toward center
void random_read(String filename) {
    struct timespec t0;
    size_t file_size;
    
    if (verbosity >= 2) {
        printf("Random read: %s\n", filename);
    }
    
    if (!get_file_size(filename, &file_size)) {
        return;
    }
    
    FILE *file = fopen(filename, "rb");
    if (!file) {
        if (verbosity >= 2) {
            printf("Error: Cannot open file %s\n", filename);
        }
        return;
    }
    
    size_t num_blocks = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (verbosity >= 2) {
        printf("Number of blocks: %zu\n", num_blocks);
    }
    
    setup_hashing();
    uint64_t hash_xor = 0;
    
    unsigned char *buffer = malloc(BLOCK_SIZE);
    if (!buffer) {
        if (verbosity >= 2) {
            printf("Error: Cannot allocate memory for buffer\n");
        }
        fclose(file);
        return;
    }
    
    size_t total_bytes = 0;
    t0 = timer_start();
    
    // Read blocks in alternating pattern: first, last, second, second-to-last, etc.
    for (size_t i = 0; i < (num_blocks + 1) / 2; i++) {
        size_t first_block = i;
        size_t last_block = num_blocks - 1 - i;
        
        // Read first block
        if (first_block < num_blocks) {
            size_t offset = first_block * BLOCK_SIZE;
            size_t block_size = (offset + BLOCK_SIZE > file_size) ? 
                               (file_size - offset) : BLOCK_SIZE;
            
            fseek(file, offset, SEEK_SET);
            size_t bytes_read = fread(buffer, 1, block_size, file);
            
            if (bytes_read > 0) {
                process_block_xor(buffer, bytes_read, &hash_xor);
                total_bytes += bytes_read;
                if (verbosity >= 2) {
                    printf("Read block %zu (offset %zu, size %zu)\n", 
                           first_block, offset, bytes_read);
                }
            }
        }
        
        // Read last block (if different from first)
        if (last_block != first_block && last_block < num_blocks) {
            size_t offset = last_block * BLOCK_SIZE;
            size_t block_size = (offset + BLOCK_SIZE > file_size) ? 
                               (file_size - offset) : BLOCK_SIZE;
            
            fseek(file, offset, SEEK_SET);
            size_t bytes_read = fread(buffer, 1, block_size, file);
            
            if (bytes_read > 0) {
                process_block_xor(buffer, bytes_read, &hash_xor);
                total_bytes += bytes_read;
                if (verbosity >= 2) {
                    printf("Read block %zu (offset %zu, size %zu)\n", 
                           last_block, offset, bytes_read);
                }
            }
        }
    }
    
    print_results("Random read", hash_xor, total_bytes, t0);
    free(buffer);
    fclose(file);
}

// ============================================================================
// Memory-Mapped File Functions
// ============================================================================

// Sequential processing using memory mapping
void sequential_mmap(String filename) {
    struct timespec t0;
    size_t file_size;
    
    if (verbosity >= 2) {
        printf("Sequential mmap: %s\n", filename);
    }
    
    void *mapped_file = map_file(filename, &file_size);
    if (!mapped_file) {
        return;
    }
    
    setup_hashing();
    uint64_t hash_xor = 0;
    t0 = timer_start();
    
    // Process file in blocks from mapped memory
    unsigned char *file_ptr = (unsigned char*)mapped_file;
    size_t offset = 0;
    size_t total_bytes = 0;
    
    while (offset < file_size) {
        size_t block_size = (offset + BLOCK_SIZE > file_size) ? 
                           (file_size - offset) : BLOCK_SIZE;
        
        process_block_xor(file_ptr + offset, block_size, &hash_xor);
        total_bytes += block_size;
        offset += block_size;
        
        if (verbosity >= 2) {
            printf("Processed %zu bytes (total: %zu)\n", block_size, total_bytes);
        }
    }
    
    print_results("Sequential mmap", hash_xor, total_bytes, t0);
    unmap_file(mapped_file, file_size);
}
// Random access pattern using memory mapping
void random_mmap(String filename) {
    struct timespec t0;
    size_t file_size;
    
    if (verbosity >= 2) {
        printf("Random mmap: %s\n", filename);
    }
    
    void *mapped_file = map_file(filename, &file_size);
    if (!mapped_file) {
        return;
    }
    
    size_t num_blocks = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (verbosity >= 2) {
        printf("Number of blocks: %zu\n", num_blocks);
    }
    
    setup_hashing();
    uint64_t hash_xor = 0;
    
    unsigned char *file_ptr = (unsigned char*)mapped_file;
    size_t total_bytes = 0;
    t0 = timer_start();
    
    // Process blocks in alternating pattern
    for (size_t i = 0; i < (num_blocks + 1) / 2; i++) {
        size_t first_block = i;
        size_t last_block = num_blocks - 1 - i;
        
        // Process first block
        if (first_block < num_blocks) {
            size_t offset = first_block * BLOCK_SIZE;
            size_t block_size = (offset + BLOCK_SIZE > file_size) ? 
                               (file_size - offset) : BLOCK_SIZE;
            
            process_block_xor(file_ptr + offset, block_size, &hash_xor);
            total_bytes += block_size;
            
            if (verbosity >= 2) {
                printf("Processed block %zu (offset %zu, size %zu)\n", 
                       first_block, offset, block_size);
            }
        }
        
        // Process last block (if different from first)
        if (last_block != first_block && last_block < num_blocks) {
            size_t offset = last_block * BLOCK_SIZE;
            size_t block_size = (offset + BLOCK_SIZE > file_size) ? 
                               (file_size - offset) : BLOCK_SIZE;
            
            process_block_xor(file_ptr + offset, block_size, &hash_xor);
            total_bytes += block_size;
            
            if (verbosity >= 2) {
                printf("Processed block %zu (offset %zu, size %zu)\n", 
                       last_block, offset, block_size);
            }
        }
    }
    
    print_results("Random mmap", hash_xor, total_bytes, t0);
    unmap_file(mapped_file, file_size);
}

// ============================================================================
// Main Functions
// ============================================================================

// Run all file reading benchmarks
void read_file(String filename) {
    sequential_read(filename);
    random_read(filename);
    sequential_mmap(filename);
    random_mmap(filename);
    async_sequential_read(filename);
}

int main(int argc, char *argv[]) {
    // Parse options
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            if (i + 1 < argc) {
                verbosity = atoi(argv[i + 1]);
                if (verbosity < 0 || verbosity > 2) {
                    printf("Error: Verbosity level must be 0, 1, or 2\n");
                    printf("Usage: %s [options] <file>\n", argv[0]);
                    printf("  -v, --verbose LEVEL  Set verbosity level (0-2, default: 1)\n");
                    printf("  -h, --help           Show this help message\n");
                    return 1;
                }
                i += 2; // consume option and its value
            } else {
                printf("Error: -v/--verbose requires a level (0, 1, or 2)\n");
                printf("Usage: %s [options] <file>\n", argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options] <file>\n", argv[0]);
            printf("  -v, --verbose LEVEL  Set verbosity level (0-2, default: 1)\n");
            printf("  -h, --help           Show this help message\n");
            printf("\nVerbosity levels:\n");
            printf("  0: Only times\n");
            printf("  1: Times and checksums (default)\n");
            printf("  2: All output including debug messages\n");
            return 0;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            printf("Usage: %s [options] <file>\n", argv[0]);
            return 1;
        }
    }

    if (i >= argc) {
        printf("Error: Missing <file> argument\n");
        printf("Usage: %s [options] <file>\n", argv[0]);
        return 1;
    }

    String filename = argv[i];

    if (verbosity >= 2) {
        printf("Verbosity level: %d\n", verbosity);
        printf("Input file: %s\n", filename);
    }

    read_file(filename);
    return 0;
}
