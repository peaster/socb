#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <stdbool.h>
#include <sys/stat.h>

/* Configuration Constants */
#define DEFAULT_NUM_THREADS 4
#define DEFAULT_MEMORY_BLOCK_SIZE (100 * 1024 * 1024)  // 100 MB blocks
#define DEFAULT_FILE_SIZE (10 * 1024 * 1024)           // 10 MB file operations
#define DEFAULT_TEST_DURATION 20                        // Test duration in seconds
#define BILLION 1000000000.0

/* Benchmark Baseline Reference Values (from a reference system) */
// These values represent performance on a reference system (adjust based on your baseline hardware)
#define CPU_REFERENCE_FLOPS 5000000000.0        // 5 GFLOPS reference
#define MEMORY_READ_REFERENCE 10000.0           // 10 GB/s reference
#define MEMORY_WRITE_REFERENCE 8000.0           // 8 GB/s reference
#define DISK_READ_REFERENCE 500.0               // 500 MB/s reference
#define DISK_WRITE_REFERENCE 400.0              // 400 MB/s reference
#define DISK_IOPS_REFERENCE 5000.0              // 5000 IOPS reference

/* Score Weighting */
#define CPU_WEIGHT 0.40                         // CPU is 40% of total score
#define MEMORY_WEIGHT 0.35                      // Memory is 35% of total score
#define DISK_WEIGHT 0.25                        // Disk is 25% of total score

/* Benchmark Results Structure */
typedef struct {
    // Raw performance metrics
    double cpu_flops;                  // Floating point operations per second
    double memory_read_bandwidth;      // Memory read bandwidth in MB/s
    double memory_write_bandwidth;     // Memory write bandwidth in MB/s
    double disk_read_throughput;       // Disk read throughput in MB/s
    double disk_write_throughput;      // Disk write throughput in MB/s
    double disk_seek_iops;             // Disk I/O operations per second (random)
    
    // Performance scores (normalized against reference values)
    int cpu_score;                     // CPU performance score
    int memory_score;                  // Memory performance score
    int disk_score;                    // Disk performance score
    int overall_score;                 // Combined performance score
} benchmark_result_t;

/* Global Variables */
volatile bool running = true;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t results_mutex = PTHREAD_MUTEX_INITIALIZER;
int num_threads = DEFAULT_NUM_THREADS;
size_t memory_block_size = DEFAULT_MEMORY_BLOCK_SIZE;
size_t file_size = DEFAULT_FILE_SIZE;
int duration = DEFAULT_TEST_DURATION;
benchmark_result_t global_results = {0};
bool verbose_output = false;           // Detailed logging control

/* Configuration structure */
typedef struct {
    int thread_id;
    int duration;
    char* temp_filename;
    void* thread_buffer;               // Thread-specific buffer
    benchmark_result_t thread_results;
} thread_args_t;

/* Timespec difference in seconds */
double timespec_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / BILLION;
}

/* Thread-safe logging function */
void log_message(const char* format, ...) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    char timestamp[26];
    struct tm* tm_info = localtime(&tv.tv_sec);
    strftime(timestamp, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    
    pthread_mutex_lock(&log_mutex);
    printf("[%s.%03ld] ", timestamp, tv.tv_usec / 1000);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&log_mutex);
}

/* Verbose logging function - only prints if verbose mode is enabled */
void verbose_log(const char* format, ...) {
    if (!verbose_output) return;
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    char timestamp[26];
    struct tm* tm_info = localtime(&tv.tv_sec);
    strftime(timestamp, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    
    pthread_mutex_lock(&log_mutex);
    printf("[%s.%03ld] [VERBOSE] ", timestamp, tv.tv_usec / 1000);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&log_mutex);
}

/* Signal handler for graceful termination */
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        log_message("Received termination signal. Shutting down gracefully...");
        running = false;
    }
}

/* CPU Benchmark Implementation 1: FLOPS Benchmark */
double cpu_benchmark_impl_flops(int thread_id, int duration) {
    verbose_log("Thread %d: Starting FLOPS benchmark...", thread_id);
    
    struct timespec start, end;
    volatile double result = 0.0;
    long long total_ops = 0;
    
    time_t end_time = time(NULL) + duration;
    double elapsed_total = 0.0;
    
    // Main measurement loop
    while (running && time(NULL) < end_time) {
        const long long ops_per_iter = 1000000;
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        // Mix of floating-point operations (transcendental and algebraic)
        for (long long i = 1; i <= ops_per_iter && running; i++) {
            // This mix prevents compiler optimization while providing a realistic workload
            result += sin(i * 0.1) * cos(i * 0.2) / sqrt(i + 1.0);
        }
        
        clock_gettime(CLOCK_MONOTONIC, &end);
        double elapsed = timespec_diff(start, end);
        
        total_ops += ops_per_iter;
        elapsed_total += elapsed;
        
        // Prevent result from being optimized away
        if (result > 1e100) result = 0.0;
        
        // Brief pause to prevent CPU hogging
        usleep(5000);
    }
    
    double flops = (elapsed_total > 0) ? (double)total_ops / elapsed_total : 0;
    verbose_log("Thread %d: FLOPS benchmark completed. Result: %.2f FLOPS", thread_id, flops);
    
    return flops;
}

/* Memory Benchmark Implementation 1: Bandwidth */
void memory_benchmark_impl_bandwidth(int thread_id, int duration, 
                                   double *read_bw, double *write_bw) {
    verbose_log("Thread %d: Starting memory bandwidth benchmark...", thread_id);
    
    struct timespec start, end;
    size_t buffer_size = memory_block_size;
    
    // Allocate aligned memory for benchmark
    char* buffer = (char*)malloc(buffer_size);
    if (!buffer) {
        log_message("Thread %d: Memory allocation failed for bandwidth test", thread_id);
        *read_bw = *write_bw = 0;
        return;
    }
    
    time_t end_time = time(NULL) + duration;
    double total_read_bytes = 0, total_read_time = 0;
    double total_write_bytes = 0, total_write_time = 0;
    
    // Main measurement loop
    while (running && time(NULL) < end_time) {
        // WRITE benchmark
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        for (int iter = 0; iter < 5 && running; iter++) {
            memset(buffer, (iter * thread_id) & 0xFF, buffer_size);
        }
        
        clock_gettime(CLOCK_MONOTONIC, &end);
        double write_time = timespec_diff(start, end);
        total_write_time += write_time;
        total_write_bytes += 5 * buffer_size;
        
        // READ benchmark
        volatile unsigned char checksum = 0;  // Prevent optimization
        
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        for (int iter = 0; iter < 5 && running; iter++) {
            for (size_t i = 0; i < buffer_size; i += 128) {
                checksum ^= buffer[i];
            }
        }
        
        clock_gettime(CLOCK_MONOTONIC, &end);
        double read_time = timespec_diff(start, end);
        total_read_time += read_time;
        total_read_bytes += 5 * buffer_size;
        
        // Ensure checksum is used
        if (checksum == 0xFF) buffer[0] = 0;
        
        usleep(5000);  // Brief pause
    }
    
    // Calculate bandwidth in MB/s
    *read_bw = (total_read_time > 0) ? (total_read_bytes / (1024*1024)) / total_read_time : 0;
    *write_bw = (total_write_time > 0) ? (total_write_bytes / (1024*1024)) / total_write_time : 0;
    
    verbose_log("Thread %d: Memory bandwidth benchmark completed. Read: %.2f MB/s, Write: %.2f MB/s", 
               thread_id, *read_bw, *write_bw);
    
    free(buffer);
}

/* Disk Benchmark Implementation 1: Throughput and IOPS */
void disk_benchmark_impl_throughput(int thread_id, int duration, const char* filename,
                                 double *read_tp, double *write_tp, double *iops) {
    verbose_log("Thread %d: Starting disk throughput benchmark...", thread_id);
    
    struct timespec start, end;
    
    // Allocate buffer for disk operations
    char* buffer = (char*)malloc(file_size);
    if (!buffer) {
        log_message("Thread %d: Memory allocation failed for disk test", thread_id);
        *read_tp = *write_tp = *iops = 0;
        return;
    }
    
    // Initialize buffer with pattern data
    for (size_t i = 0; i < file_size; i++) {
        buffer[i] = (char)((i + thread_id) % 256);
    }
    
    time_t end_time = time(NULL) + duration;
    double total_read_bytes = 0, total_read_time = 0;
    double total_write_bytes = 0, total_write_time = 0;
    double total_seek_ops = 0, total_seek_time = 0;
    
    // Main measurement loop
    while (running && time(NULL) < end_time) {
        // WRITE benchmark
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        FILE* file = fopen(filename, "wb");
        if (file) {
            size_t written = fwrite(buffer, 1, file_size, file);
            fclose(file);
            
            if (written > 0) {
                clock_gettime(CLOCK_MONOTONIC, &end);
                double elapsed = timespec_diff(start, end);
                total_write_time += elapsed;
                total_write_bytes += written;
            }
        }
        
        // READ benchmark
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        file = fopen(filename, "rb");
        if (file) {
            size_t bytes_read = fread(buffer, 1, file_size, file);
            fclose(file);
            
            if (bytes_read > 0) {
                clock_gettime(CLOCK_MONOTONIC, &end);
                double elapsed = timespec_diff(start, end);
                total_read_time += elapsed;
                total_read_bytes += bytes_read;
            }
        }
        
        // IOPS (Random access) benchmark
        const int iops_iterations = 100;
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        file = fopen(filename, "r+b");
        if (file) {
            char small_buf[512];
            for (int i = 0; i < iops_iterations && running; i++) {
                long pos = rand() % (file_size - sizeof(small_buf));
                fseek(file, pos, SEEK_SET);
                fread(small_buf, 1, sizeof(small_buf), file);
            }
            fclose(file);
            
            clock_gettime(CLOCK_MONOTONIC, &end);
            double elapsed = timespec_diff(start, end);
            total_seek_time += elapsed;
            total_seek_ops += iops_iterations;
        }
        
        usleep(5000);  // Brief pause
    }
    
    // Calculate metrics
    *read_tp = (total_read_time > 0) ? (total_read_bytes / (1024*1024)) / total_read_time : 0;
    *write_tp = (total_write_time > 0) ? (total_write_bytes / (1024*1024)) / total_write_time : 0;
    *iops = (total_seek_time > 0) ? total_seek_ops / total_seek_time : 0;
    
    verbose_log("Thread %d: Disk benchmark completed. Read: %.2f MB/s, Write: %.2f MB/s, IOPS: %.2f",
               thread_id, *read_tp, *write_tp, *iops);
    
    free(buffer);
}

/* CPU benchmark thread function */
void* cpu_benchmark(void* args) {
    thread_args_t* t_args = (thread_args_t*)args;
    log_message("CPU benchmark thread %d started", t_args->thread_id);
    
    // Run the FLOPS benchmark
    double flops = cpu_benchmark_impl_flops(t_args->thread_id, t_args->duration);
    
    pthread_mutex_lock(&results_mutex);
    t_args->thread_results.cpu_flops = flops;
    if (t_args->thread_id == 0) {  // Only record global results from thread 0
        global_results.cpu_flops = flops;
    }
    pthread_mutex_unlock(&results_mutex);
    
    log_message("CPU benchmark thread %d completed. Result: %.2f MFLOPS", 
                t_args->thread_id, flops / 1000000.0);
    return NULL;
}

/* Memory benchmark thread function */
void* memory_benchmark(void* args) {
    thread_args_t* t_args = (thread_args_t*)args;
    log_message("Memory benchmark thread %d started", t_args->thread_id);
    
    double read_bandwidth = 0.0, write_bandwidth = 0.0;
    
    // Run the memory bandwidth benchmark
    memory_benchmark_impl_bandwidth(t_args->thread_id, t_args->duration, 
                                  &read_bandwidth, &write_bandwidth);
    
    pthread_mutex_lock(&results_mutex);
    t_args->thread_results.memory_read_bandwidth = read_bandwidth;
    t_args->thread_results.memory_write_bandwidth = write_bandwidth;
    
    // Fix: Check if this is the first memory thread (thread_id == num_threads)
    if (t_args->thread_id == num_threads) {  // First memory thread
        global_results.memory_read_bandwidth = read_bandwidth;
        global_results.memory_write_bandwidth = write_bandwidth;
    }
    pthread_mutex_unlock(&results_mutex);
    
    log_message("Memory benchmark thread %d completed. Read: %.2f MB/s, Write: %.2f MB/s",
                t_args->thread_id, read_bandwidth, write_bandwidth);
    
    return NULL;
}

/* I/O benchmark thread function */
void* io_benchmark(void* args) {
    thread_args_t* t_args = (thread_args_t*)args;
    log_message("I/O benchmark thread %d started", t_args->thread_id);
    
    double read_throughput = 0.0, write_throughput = 0.0, seek_iops = 0.0;
    
    // Run the disk throughput benchmark
    disk_benchmark_impl_throughput(t_args->thread_id, t_args->duration, t_args->temp_filename,
                                &read_throughput, &write_throughput, &seek_iops);
    
    pthread_mutex_lock(&results_mutex);
    t_args->thread_results.disk_read_throughput = read_throughput;
    t_args->thread_results.disk_write_throughput = write_throughput;
    t_args->thread_results.disk_seek_iops = seek_iops;
    
    // Fix: Check if this is the first I/O thread (thread_id == 2*num_threads)
    if (t_args->thread_id == 2 * num_threads) {  // First I/O thread
        global_results.disk_read_throughput = read_throughput;
        global_results.disk_write_throughput = write_throughput;
        global_results.disk_seek_iops = seek_iops;
    }
    pthread_mutex_unlock(&results_mutex);
    
    log_message("I/O benchmark thread %d completed. Read: %.2f MB/s, Write: %.2f MB/s, IOPS: %.2f",
                t_args->thread_id, read_throughput, write_throughput, seek_iops);
    
    return NULL;
}

/* Calculate benchmark scores */
void calculate_benchmark_scores() {
    // Calculate individual component scores (1000 points = reference system)
    
    // CPU Score: based on FLOPS performance relative to reference
    double cpu_ratio = global_results.cpu_flops / CPU_REFERENCE_FLOPS;
    global_results.cpu_score = (int)(1000 * cpu_ratio);
    
    // Memory Score: average of read and write bandwidth scores
    double mem_read_ratio = global_results.memory_read_bandwidth / MEMORY_READ_REFERENCE;
    double mem_write_ratio = global_results.memory_write_bandwidth / MEMORY_WRITE_REFERENCE;
    global_results.memory_score = (int)(1000 * (mem_read_ratio * 0.6 + mem_write_ratio * 0.4));
    
    // Disk Score: weighted combination of read, write, and IOPS
    double disk_read_ratio = global_results.disk_read_throughput / DISK_READ_REFERENCE;
    double disk_write_ratio = global_results.disk_write_throughput / DISK_WRITE_REFERENCE;
    double disk_iops_ratio = global_results.disk_seek_iops / DISK_IOPS_REFERENCE;
    global_results.disk_score = (int)(1000 * (disk_read_ratio * 0.4 + 
                                            disk_write_ratio * 0.3 + 
                                            disk_iops_ratio * 0.3));
    
    // Calculate overall score: weighted average of component scores
    global_results.overall_score = (int)(
        global_results.cpu_score * CPU_WEIGHT +
        global_results.memory_score * MEMORY_WEIGHT +
        global_results.disk_score * DISK_WEIGHT
    );
}

/* Resource cleanup function */
void cleanup_resources(thread_args_t* args, int count, pthread_t* threads) {
    // Free thread arguments memory
    if (args) {
        for (int i = 0; i < count; i++) {
            if (args[i].temp_filename) {
                remove(args[i].temp_filename);  // Remove temporary files
                free(args[i].temp_filename);
            }
            if (args[i].thread_buffer) {
                free(args[i].thread_buffer);
            }
        }
        free(args);
    }
    
    // Free threads array
    if (threads) {
        free(threads);
    }
    
    // Destroy mutexes
    pthread_mutex_destroy(&log_mutex);
    pthread_mutex_destroy(&results_mutex);
    verbose_log("Resource cleanup complete");
}

/* Parse command line arguments */
void parse_arguments(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            num_threads = atoi(argv[i + 1]);
            if (num_threads <= 0) num_threads = DEFAULT_NUM_THREADS;
            i++;
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            memory_block_size = (size_t)atoll(argv[i + 1]) * 1024 * 1024;  // Convert MB to bytes
            if (memory_block_size == 0) memory_block_size = DEFAULT_MEMORY_BLOCK_SIZE;
            i++;
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            file_size = (size_t)atoll(argv[i + 1]) * 1024 * 1024;  // Convert MB to bytes
            if (file_size == 0) file_size = DEFAULT_FILE_SIZE;
            i++;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            duration = atoi(argv[i + 1]);
            if (duration <= 0) duration = DEFAULT_TEST_DURATION;
            i++;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose_output = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  -t THREADS   Number of threads per test type (default: %d)\n", DEFAULT_NUM_THREADS);
            printf("  -m SIZE      Memory block size in MB (default: %d MB)\n", 
                   (int)(DEFAULT_MEMORY_BLOCK_SIZE / (1024 * 1024)));
            printf("  -f SIZE      File size in MB (default: %d MB)\n", 
                   (int)(DEFAULT_FILE_SIZE / (1024 * 1024)));
            printf("  -d SECONDS   Test duration in seconds (default: %d)\n", DEFAULT_TEST_DURATION);
            printf("  -v, --verbose Enable verbose output\n");
            printf("  -h, --help   Show this help message\n");
            exit(0);
        }
    }
}

/* Print benchmark results with scores */
void print_benchmark_results() {
    // Calculate scores before printing
    calculate_benchmark_scores();
    
    // Get system information
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    
    // Get current time
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║               HARDWARE PERFORMANCE BENCHMARK               ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║ System: %-52s ║\n", hostname);
    printf("║ Date:   %-52s ║\n", timestamp);
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║                      BENCHMARK SCORE                       ║\n");
    printf("║                                                           ║\n");
    printf("║                         [ %4d ]                          ║\n", global_results.overall_score);
    printf("║                                                           ║\n");
    printf("╠═══════════════════════════════════╦═══════════╦═══════════╣\n");
    printf("║ Component                         ║ Raw Value ║   Score   ║\n");
    printf("╠═══════════════════════════════════╬═══════════╬═══════════╣\n");
    printf("║ CPU                               ║           ║           ║\n");
    printf("║   Floating Point Performance      ║ %7.2f M ║ %9d ║\n", 
           global_results.cpu_flops / 1000000.0, global_results.cpu_score);
    printf("╠═══════════════════════════════════╬═══════════╬═══════════╣\n");
    printf("║ Memory                            ║           ║           ║\n");
    printf("║   Read Bandwidth                  ║ %7.2f MB ║           ║\n", 
           global_results.memory_read_bandwidth);
    printf("║   Write Bandwidth                 ║ %7.2f MB ║ %9d ║\n", 
           global_results.memory_write_bandwidth, global_results.memory_score);
    printf("╠═══════════════════════════════════╬═══════════╬═══════════╣\n");
    printf("║ Disk                              ║           ║           ║\n");
    printf("║   Sequential Read                 ║ %7.2f MB ║           ║\n", 
           global_results.disk_read_throughput);
    printf("║   Sequential Write                ║ %7.2f MB ║           ║\n", 
           global_results.disk_write_throughput);
    printf("║   Random Access (IOPS)            ║ %7.2f    ║ %9d ║\n", 
           global_results.disk_seek_iops, global_results.disk_score);
    printf("╚═══════════════════════════════════╩═══════════╩═══════════╝\n");
    printf("\n");
    
    // Save results to file
    FILE* result_file = fopen("benchmark_results.txt", "w");
    if (result_file) {
        fprintf(result_file, "Benchmark Results\n");
        fprintf(result_file, "=================\n");
        fprintf(result_file, "System: %s\n", hostname);
        fprintf(result_file, "Date: %s\n\n", timestamp);
        fprintf(result_file, "Overall Score: %d\n\n", global_results.overall_score);
        
        fprintf(result_file, "CPU Benchmark:\n");
        fprintf(result_file, "  FLOPS: %.2f MFLOPS\n", global_results.cpu_flops / 1000000.0);
        fprintf(result_file, "  Score: %d\n\n", global_results.cpu_score);
        
        fprintf(result_file, "Memory Benchmark:\n");
        fprintf(result_file, "  Read Bandwidth: %.2f MB/s\n", global_results.memory_read_bandwidth);
        fprintf(result_file, "  Write Bandwidth: %.2f MB/s\n", global_results.memory_write_bandwidth);
        fprintf(result_file, "  Score: %d\n\n", global_results.memory_score);
        
        fprintf(result_file, "Disk Benchmark:\n");
        fprintf(result_file, "  Read Throughput: %.2f MB/s\n", global_results.disk_read_throughput);
        fprintf(result_file, "  Write Throughput: %.2f MB/s\n", global_results.disk_write_throughput);
        fprintf(result_file, "  Random Access: %.2f IOPS\n", global_results.disk_seek_iops);
        fprintf(result_file, "  Score: %d\n", global_results.disk_score);
        
        fclose(result_file);
        printf("Detailed results saved to benchmark_results.txt\n\n");
    }
    
    // Also save in CSV format for analysis
    result_file = fopen("benchmark_results.csv", "w");
    if (result_file) {
        fprintf(result_file, "System,Date,OverallScore,CPUScore,MFLOPS,MemoryScore,ReadBandwidth,WriteBandwidth,DiskScore,ReadThroughput,WriteThroughput,IOPS\n");
        fprintf(result_file, "%s,%s,%d,%d,%.2f,%d,%.2f,%.2f,%d,%.2f,%.2f,%.2f\n",
                hostname, timestamp, 
                global_results.overall_score, 
                global_results.cpu_score, global_results.cpu_flops / 1000000.0,
                global_results.memory_score, global_results.memory_read_bandwidth, global_results.memory_write_bandwidth,
                global_results.disk_score, global_results.disk_read_throughput, global_results.disk_write_throughput, global_results.disk_seek_iops);
        fclose(result_file);
        printf("CSV results saved to benchmark_results.csv\n");
    }
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    parse_arguments(argc, argv);
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize random seed
    srand((unsigned int)time(NULL));
    
    log_message("Starting hardware performance benchmark with configuration:");
    log_message("  Threads per test: %d", num_threads);
    log_message("  Memory block size: %zu MB", memory_block_size / (1024 * 1024));
    log_message("  File size: %zu MB", file_size / (1024 * 1024));
    log_message("  Duration: %d seconds", duration);
    
    int total_threads = num_threads * 3;  // Threads for CPU, memory, and I/O tests
    pthread_t* threads = (pthread_t*)calloc(total_threads, sizeof(pthread_t));
    thread_args_t* args = (thread_args_t*)calloc(total_threads, sizeof(thread_args_t));
    
    if (!threads || !args) {
        log_message("Memory allocation for thread management failed");
        cleanup_resources(args, total_threads, threads);
        return EXIT_FAILURE;
    }
    
    // Prepare thread arguments
    for (int i = 0; i < total_threads; i++) {
        args[i].thread_id = i;
        args[i].duration = duration;
        
        // Initialize thread-specific results
        memset(&args[i].thread_results, 0, sizeof(benchmark_result_t));
        
        // Allocate I/O buffers for I/O threads
        if (i >= 2 * num_threads) {  // I/O threads
            args[i].thread_buffer = malloc(file_size);
            if (!args[i].thread_buffer) {
                log_message("Failed to allocate buffer for I/O thread %d", i);
                cleanup_resources(args, total_threads, threads);
                return EXIT_FAILURE;
            }
            
            // Create unique filename for each I/O thread
            args[i].temp_filename = malloc(64);
            if (!args[i].temp_filename) {
                log_message("Failed to allocate filename buffer for I/O thread %d", i);
                cleanup_resources(args, total_threads, threads);
                return EXIT_FAILURE;
            }
            snprintf(args[i].temp_filename, 64, "benchmark_file_%d.tmp", i);
        }
    }
    
    printf("\n");
    log_message("╔═══════════════════════════════════════════════════╗");
    log_message("║             STARTING BENCHMARK SUITE              ║");
    log_message("╚═══════════════════════════════════════════════════╝");
    
    // Run CPU benchmark
    log_message("╔═══ CPU BENCHMARK ═══╗");
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, cpu_benchmark, &args[i]) != 0) {
            log_message("Failed to create CPU benchmark thread %d: %s", i, strerror(errno));
            cleanup_resources(args, total_threads, threads);
            return EXIT_FAILURE;
        }
    }
    
    // Wait for CPU benchmark threads to complete
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    log_message("╚═══════════════════╝");
    
    // Run memory benchmark
    log_message("╔═══ MEMORY BENCHMARK ═══╗");
    for (int i = 0; i < num_threads; i++) {
        int idx = i + num_threads;
        if (pthread_create(&threads[idx], NULL, memory_benchmark, &args[idx]) != 0) {
            log_message("Failed to create memory benchmark thread %d: %s", i, strerror(errno));
            running = false;
            break;
        }
    }
    
    // Wait for memory benchmark threads to complete
    for (int i = num_threads; i < 2 * num_threads; i++) {
        if (threads[i]) {
            pthread_join(threads[i], NULL);
        }
    }
    log_message("╚══════════════════════╝");
    
    // Run I/O benchmark
    log_message("╔═══ DISK I/O BENCHMARK ═══╗");
    for (int i = 0; i < num_threads; i++) {
        int idx = i + 2 * num_threads;
        if (pthread_create(&threads[idx], NULL, io_benchmark, &args[idx]) != 0) {
            log_message("Failed to create I/O benchmark thread %d: %s", i, strerror(errno));
            running = false;
            break;
        }
    }
    
    // Wait for I/O benchmark threads to complete
    for (int i = 2 * num_threads; i < total_threads; i++) {
        if (threads[i]) {
            pthread_join(threads[i], NULL);
        }
    }
    log_message("╚═════════════════════════╝");
    
    log_message("All benchmarks completed");
    
    // Print benchmark results with scores
    print_benchmark_results();
    
    cleanup_resources(args, total_threads, threads);
    return EXIT_SUCCESS;
}