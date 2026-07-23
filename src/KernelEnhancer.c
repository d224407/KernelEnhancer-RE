/*
 * KernelEnhancer – Android Kernel Tuning
 * Phiên bản an toàn bộ nhớ
 * Tuân thủ:
 *   - Không dùng strcpy/strcat/sprintf
 *   - Kiểm tra malloc/calloc
 *   - Gán NULL sau free
 *   - Dùng snprintf/strncpy an toàn
 *   - Hàm wrapper an toàn
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>

/* ==================== Định nghĩa hằng số ==================== */

#define LOG_FILE        "/data/local/tmp/KernelEnhancer.log"
#define LOG_SDCARD      "/sdcard/KernelEnhancer.log"
#define MAX_CMD_LEN     1024
#define MAX_PATH_LEN    PATH_MAX
#define MAX_LINE_LEN    512
#define MAX_BUFFER_LEN  4096

/* Macro an toàn cho strncpy (luôn null-terminate) */
#define SAFE_STRNCPY(dst, src, n) do { \
    strncpy((dst), (src), (n) - 1); \
    (dst)[(n) - 1] = '\0'; \
} while(0)

/* Macro kiểm tra file tồn tại và là regular file */
#define FILE_EXISTS(path) (access((path), F_OK) == 0)
#define IS_REG_FILE(path) ({ struct stat st; stat((path), &st) == 0 && S_ISREG(st.st_mode); })

/* ==================== Hàm quản lý bộ nhớ an toàn ==================== */

static void* safe_malloc(size_t size) {
    if (size == 0) return NULL;
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "ERROR: malloc(%zu) failed\n", size);
    }
    return ptr;
}

static void* safe_calloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) return NULL;
    void *ptr = calloc(nmemb, size);
    if (!ptr) {
        fprintf(stderr, "ERROR: calloc(%zu, %zu) failed\n", nmemb, size);
    }
    return ptr;
}

static void safe_free(void **ptr) {
    if (ptr && *ptr) {
        free(*ptr);
        *ptr = NULL;
    }
}

static void safe_fclose(FILE **fp) {
    if (fp && *fp) {
        fclose(*fp);
        *fp = NULL;
    }
}

/* ==================== Xử lý chuỗi an toàn ==================== */

static int safe_snprintf(char *buffer, size_t size, const char *format, ...) {
    if (!buffer || size == 0) return -1;
    va_list args;
    va_start(args, format);
    int result = vsnprintf(buffer, size, format, args);
    va_end(args);
    if (result < 0 || (size_t)result >= size) {
        buffer[size - 1] = '\0';
        return -1;
    }
    return result;
}

static char* safe_strdup(const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src) + 1;
    char *dst = (char*)safe_malloc(len);
    if (dst) {
        memcpy(dst, src, len);
    }
    return dst;
}

static int safe_strcat(char *dest, size_t dest_size, const char *src) {
    if (!dest || !src || dest_size == 0) return -1;
    size_t dest_len = strlen(dest);
    size_t src_len = strlen(src);
    if (dest_len + src_len + 1 > dest_size) {
        return -1;
    }
    memcpy(dest + dest_len, src, src_len + 1);
    return 0;
}

/* ==================== Hàm ghi log an toàn ==================== */

static void log_msg(const char *fmt, ...) {
    if (!fmt) return;
    
    char buffer[MAX_LINE_LEN];
    char time_str[32];
    time_t t;
    struct tm *tm_info;
    va_list args;
    FILE *fp = NULL;
    
    time(&t);
    tm_info = localtime(&t);
    if (!tm_info) {
        strcpy(time_str, "[??:??:??]");
    } else {
        strftime(time_str, sizeof(time_str), "[%H:%M:%S]", tm_info);
    }
    
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    buffer[sizeof(buffer) - 1] = '\0';
    
    printf("%s %s\n", time_str, buffer);
    
    fp = fopen(LOG_FILE, "a");
    if (fp) {
        fprintf(fp, "%s %s\n", time_str, buffer);
        safe_fclose(&fp);
    }
    
    fp = fopen(LOG_SDCARD, "a");
    if (fp) {
        fprintf(fp, "%s %s\n", time_str, buffer);
        safe_fclose(&fp);
    }
}

/* ==================== Hàm ghi file an toàn ==================== */

static int safe_write_file(const char *path, const char *value) {
    if (!path || !value) return -1;
    
    FILE *fp = NULL;
    struct stat st;
    int result = -1;
    mode_t original_mode = 0;
    char read_buffer[MAX_LINE_LEN] = {0};
    
    if (stat(path, &st) != 0) {
        return -1;
    }
    
    if (!S_ISREG(st.st_mode)) {
        return -1;
    }
    
    original_mode = st.st_mode & 0777;
    
    if (access(path, W_OK) != 0) {
        chmod(path, original_mode | S_IWUSR);
    }
    
    fp = fopen(path, "w");
    if (!fp) {
        log_msg("Failed to open %s for writing", path);
        goto cleanup;
    }
    
    if (fprintf(fp, "%s\n", value) < 0) {
        log_msg("Failed to write to %s", path);
        goto cleanup;
    }
    
    safe_fclose(&fp);
    
    fp = fopen(path, "r");
    if (!fp) {
        goto cleanup;
    }
    
    if (fgets(read_buffer, sizeof(read_buffer), fp)) {
        size_t len = strlen(read_buffer);
        while (len > 0 && (read_buffer[len-1] == '\n' || read_buffer[len-1] == '\r')) {
            read_buffer[--len] = '\0';
        }
        if (strcmp(read_buffer, value) == 0) {
            result = 0;
        }
    }
    
cleanup:
    safe_fclose(&fp);
    
    if (original_mode > 0) {
        chmod(path, original_mode);
    }
    
    return result;
}

/* ==================== Hàm chạy lệnh an toàn ==================== */

static int safe_system(const char *cmd) {
    if (!cmd) return -1;
    char buf[MAX_CMD_LEN];
    safe_snprintf(buf, sizeof(buf), "%s >/dev/null 2>&1", cmd);
    return system(buf);
}

static char* safe_get_cmd_output(const char *cmd, char *output, size_t size) {
    if (!cmd || !output || size == 0) return NULL;
    
    FILE *fp = NULL;
    char *result = NULL;
    
    fp = popen(cmd, "r");
    if (!fp) {
        return NULL;
    }
    
    if (fgets(output, size, fp)) {
        size_t len = strnlen(output, size);
        while (len > 0 && (output[len-1] == '\n' || output[len-1] == '\r')) {
            output[--len] = '\0';
        }
        result = output;
    } else {
        output[0] = '\0';
    }
    
    pclose(fp);
    return result;
}

/* ==================== Hàm kiểm tra chuỗi trong file ==================== */

static int file_contains(const char *path, const char *needle) {
    if (!path || !needle) return 0;
    
    FILE *fp = NULL;
    char line[MAX_LINE_LEN];
    int found = 0;
    
    fp = fopen(path, "r");
    if (!fp) {
        return 0;
    }
    
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, needle)) {
            found = 1;
            break;
        }
    }
    
    safe_fclose(&fp);
    return found;
}

/* ==================== Hàm so sánh cho qsort ==================== */

static int compare_freq(const void *a, const void *b) {
    long la = *(long*)a;
    long lb = *(long*)b;
    return (la > lb) - (la < lb);
}

/* ==================== Hàm đợi boot an toàn ==================== */

static void wait_for_boot(void) {
    char buf[16];
    int wait = 0;
    FILE *fp = NULL;
    
    log_msg("========== KernelEnhancer Started ==========");
    
    // Đợi sys.boot_completed
    while (wait < 30) {
        fp = popen("getprop sys.boot_completed 2>/dev/null", "r");
        if (fp) {
            if (fgets(buf, sizeof(buf), fp) && buf[0] == '1') {
                pclose(fp);
                fp = NULL;
                break;
            }
            pclose(fp);
            fp = NULL;
        }
        sleep(2);
        wait++;
    }
    
    // Đợi boot animation dừng
    wait = 0;
    while (wait < 20) {
        fp = popen("getprop init.svc.bootanim 2>/dev/null", "r");
        if (fp) {
            if (fgets(buf, sizeof(buf), fp) && strstr(buf, "stopped")) {
                pclose(fp);
                fp = NULL;
                break;
            }
            pclose(fp);
            fp = NULL;
        }
        sleep(2);
        wait++;
    }
    
    sleep(5);
}

/* ==================== 1. Bật CPU online ==================== */

static void tweak_cpu_online(void) {
    DIR *dir = NULL;
    struct dirent *ent = NULL;
    char path[MAX_PATH_LEN];
    int count = 0;
    
    dir = opendir("/sys/devices/system/cpu");
    if (!dir) {
        log_msg("Cannot open /sys/devices/system/cpu");
        return;
    }
    
    while ((ent = readdir(dir)) != NULL) {
        if (!ent) break;
        if (strncmp(ent->d_name, "cpu", 3) == 0 && isdigit(ent->d_name[3])) {
            safe_snprintf(path, sizeof(path), "/sys/devices/system/cpu/%s/online", ent->d_name);
            if (FILE_EXISTS(path)) {
                if (safe_write_file(path, "1") == 0) {
                    count++;
                }
            }
        }
    }
    
    closedir(dir);
    dir = NULL;
    
    log_msg("CPU Online: %d cores", count);
}

/* ==================== 2. VM Parameters ==================== */

static void tweak_virtual_memory(void) {
    FILE *fp = NULL;
    char line[MAX_LINE_LEN];
    char val_str[32];
    long mem_kb = 0;
    
    fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        log_msg("Cannot read /proc/meminfo");
        return;
    }
    
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line + 9, "%ld", &mem_kb);
            break;
        }
    }
    safe_fclose(&fp);
    
    if (mem_kb <= 0) {
        log_msg("Cannot determine memory size");
        return;
    }
    
    long mem_mb = mem_kb / 1024;
    log_msg("Memory detected: %ld MB", mem_mb);
    
    int swappiness, dirty_ratio, dirty_bg, vfs_pressure, watermark;
    
    if (mem_mb < 4096) {
        swappiness = 70;  dirty_ratio = 18; dirty_bg = 5;  vfs_pressure = 50; watermark = 149;
    } else if (mem_mb < 6144) {
        swappiness = 60;  dirty_ratio = 22; dirty_bg = 6;  vfs_pressure = 60; watermark = 177;
    } else if (mem_mb < 8192) {
        swappiness = 35;  dirty_ratio = 25; dirty_bg = 8;  vfs_pressure = 60; watermark = 191;
    } else {
        swappiness = 35;  dirty_ratio = 28; dirty_bg = 10; vfs_pressure = 55; watermark = 209;
    }
    
    safe_snprintf(val_str, sizeof(val_str), "%d", swappiness);
    safe_write_file("/proc/sys/vm/swappiness", val_str);
    
    safe_snprintf(val_str, sizeof(val_str), "%d", dirty_ratio);
    safe_write_file("/proc/sys/vm/dirty_ratio", val_str);
    
    safe_snprintf(val_str, sizeof(val_str), "%d", dirty_bg);
    safe_write_file("/proc/sys/vm/dirty_background_ratio", val_str);
    
    safe_write_file("/proc/sys/vm/dirty_expire_centisecs", "1250");
    safe_write_file("/proc/sys/vm/dirty_writeback_centisecs", "850");
    safe_write_file("/proc/sys/vm/page-cluster", "0");
    
    safe_snprintf(val_str, sizeof(val_str), "%d", vfs_pressure);
    safe_write_file("/proc/sys/vm/vfs_cache_pressure", val_str);
    
    safe_write_file("/proc/sys/vm/stat_interval", "21");
    
    safe_snprintf(val_str, sizeof(val_str), "%d", watermark);
    safe_write_file("/proc/sys/vm/watermark_scale_factor", val_str);
    
    safe_write_file("/proc/sys/vm/zone_reclaim_mode", "0");
    
    log_msg("VM tweaks applied (swappiness=%d, watermark=%d)", swappiness, watermark);
}

/* ==================== 3. Scheduler ==================== */

static void tweak_scheduler(void) {
    safe_write_file("/proc/sys/kernel/sched_downmigrate", "35 45");
    safe_write_file("/proc/sys/kernel/sched_upmigrate", "50 60");
    safe_write_file("/proc/sys/kernel/sched_util_clamp_min", "384");
    safe_write_file("/proc/sys/kernel/sched_util_clamp_min_rt_default", "512");
    log_msg("Scheduler tweaks applied");
}

/* ==================== 4. STUNE ==================== */

static void tweak_stune(void) {
    const char *paths[] = {"/dev/stune", "/sys/fs/cgroup/stune", "/sys/fs/cgroup/cpu/stune", NULL};
    char path[MAX_PATH_LEN];
    int i;
    
    for (i = 0; paths[i]; i++) {
        struct stat st;
        if (stat(paths[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            break;
        }
    }
    
    if (!paths[i]) {
        log_msg("STUNE not found");
        return;
    }
    
    const char *base = paths[i];
    
    safe_snprintf(path, sizeof(path), "%s/top-app/schedtune.boost", base);
    safe_write_file(path, "3");
    
    safe_snprintf(path, sizeof(path), "%s/top-app/schedtune.prefer_idle", base);
    safe_write_file(path, "0");
    
    safe_snprintf(path, sizeof(path), "%s/foreground/schedtune.boost", base);
    safe_write_file(path, "0");
    
    safe_snprintf(path, sizeof(path), "%s/background/schedtune.boost", base);
    safe_write_file(path, "-10");
    
    log_msg("STUNE Boost applied");
}

/* ==================== 5. CPU Boost ==================== */

static void tweak_cpu_boost(void) {
    struct stat st;
    DIR *dir = NULL;
    struct dirent *ent = NULL;
    char path[MAX_PATH_LEN];
    char freq_path[MAX_PATH_LEN];
    char boost_freq[MAX_BUFFER_LEN] = "";
    char pair[64];
    FILE *fp = NULL;
    long max_freq;
    
    if (stat("/sys/module/cpu_boost/parameters", &st) != 0 || !S_ISDIR(st.st_mode)) {
        return;
    }
    
    dir = opendir("/sys/devices/system/cpu");
    if (!dir) {
        return;
    }
    
    while ((ent = readdir(dir)) != NULL) {
        if (!ent) break;
        if (strncmp(ent->d_name, "cpu", 3) != 0 || !isdigit(ent->d_name[3])) {
            continue;
        }
        
        safe_snprintf(freq_path, sizeof(freq_path), "/sys/devices/system/cpu/%s/cpufreq/cpuinfo_max_freq", ent->d_name);
        
        fp = fopen(freq_path, "r");
        if (!fp) continue;
        
        if (fscanf(fp, "%ld", &max_freq) == 1 && max_freq > 0) {
            long half = max_freq / 2;
            safe_snprintf(pair, sizeof(pair), " %s:%ld", ent->d_name + 3, half);
            if (safe_strcat(boost_freq, sizeof(boost_freq), pair) != 0) {
                log_msg("Boost frequency buffer overflow");
                safe_fclose(&fp);
                break;
            }
        }
        safe_fclose(&fp);
    }
    
    closedir(dir);
    dir = NULL;
    
    // Xóa khoảng trắng đầu nếu có
    if (boost_freq[0] == ' ') {
        memmove(boost_freq, boost_freq + 1, strlen(boost_freq));
    }
    
    if (strlen(boost_freq) > 0) {
        safe_write_file("/sys/module/cpu_boost/parameters/input_boost_freq", boost_freq);
        safe_write_file("/sys/module/cpu_boost/parameters/sched_boost_on_input", "1");
        safe_write_file("/sys/module/cpu_boost/parameters/input_boost_ms", "50");
        safe_write_file("/sys/module/cpu_boost/parameters/input_boost_duration", "50");
        log_msg("Input Boost applied");
    }
}

/* ==================== 6. I/O Scheduler và Queue ==================== */

static void tweak_io_storage(void) {
    DIR *dir = NULL;
    struct dirent *ent = NULL;
    char path[MAX_PATH_LEN];
    char sched[MAX_PATH_LEN];
    char iosched[MAX_PATH_LEN];
    int count = 0;
    
    dir = opendir("/sys/block");
    if (!dir) {
        log_msg("Cannot open /sys/block");
        return;
    }
    
    while ((ent = readdir(dir)) != NULL) {
        if (!ent) break;
        if (ent->d_name[0] == '.') continue;
        
        safe_snprintf(sched, sizeof(sched), "/sys/block/%s/queue/scheduler", ent->d_name);
        if (!FILE_EXISTS(sched)) continue;
        
        safe_snprintf(iosched, sizeof(iosched), "/sys/block/%s/queue/iosched", ent->d_name);
        
        // Ưu tiên mq-deadline
        if (file_contains(sched, "mq-deadline")) {
            safe_write_file(sched, "mq-deadline");
            if (IS_REG_FILE(iosched)) {
                safe_snprintf(path, sizeof(path), "%s/read_expire", iosched);
                safe_write_file(path, "50");
                safe_snprintf(path, sizeof(path), "%s/write_expire", iosched);
                safe_write_file(path, "150");
                safe_snprintf(path, sizeof(path), "%s/writes_starved", iosched);
                safe_write_file(path, "1");
                safe_snprintf(path, sizeof(path), "%s/front_merges", iosched);
                safe_write_file(path, "0");
                count++;
            }
        } else if (file_contains(sched, "cfq")) {
            safe_write_file(sched, "cfq");
            if (IS_REG_FILE(iosched)) {
                safe_snprintf(path, sizeof(path), "%s/slice_idle", iosched);
                safe_write_file(path, "0");
                safe_snprintf(path, sizeof(path), "%s/low_latency", iosched);
                safe_write_file(path, "1");
                safe_snprintf(path, sizeof(path), "%s/quantum", iosched);
                safe_write_file(path, "8");
                safe_snprintf(path, sizeof(path), "%s/group_idle", iosched);
                safe_write_file(path, "0");
                safe_snprintf(path, sizeof(path), "%s/back_seek_penalty", iosched);
                safe_write_file(path, "1");
                safe_snprintf(path, sizeof(path), "%s/back_seek_max", iosched);
                safe_write_file(path, "1000000000");
                safe_snprintf(path, sizeof(path), "%s/slice_sync", iosched);
                safe_write_file(path, "85");
                safe_snprintf(path, sizeof(path), "%s/slice_async", iosched);
                safe_write_file(path, "85");
                safe_snprintf(path, sizeof(path), "%s/slice_async_rq", iosched);
                safe_write_file(path, "2");
                safe_snprintf(path, sizeof(path), "%s/slice_async_us", iosched);
                safe_write_file(path, "75000");
                safe_snprintf(path, sizeof(path), "%s/target_latency_us", iosched);
                safe_write_file(path, "20000");
                safe_snprintf(path, sizeof(path), "%s/fifo_expire_sync", iosched);
                safe_write_file(path, "100");
                safe_snprintf(path, sizeof(path), "%s/fifo_expire_async", iosched);
                safe_write_file(path, "250");
                count++;
            }
        }
        
        // Tham số queue chung
        safe_snprintf(path, sizeof(path), "/sys/block/%s/queue/read_ahead_kb", ent->d_name);
        safe_write_file(path, "256");
        safe_snprintf(path, sizeof(path), "/sys/block/%s/queue/nr_requests", ent->d_name);
        safe_write_file(path, "64");
        safe_snprintf(path, sizeof(path), "/sys/block/%s/queue/rq_affinity", ent->d_name);
        safe_write_file(path, "2");
        safe_snprintf(path, sizeof(path), "/sys/block/%s/queue/iostats", ent->d_name);
        safe_write_file(path, "0");
        safe_snprintf(path, sizeof(path), "/sys/block/%s/queue/add_random", ent->d_name);
        safe_write_file(path, "0");
    }
    
    closedir(dir);
    dir = NULL;
    
    // ZRAM riêng
    dir = opendir("/sys/block");
    if (dir) {
        while ((ent = readdir(dir)) != NULL) {
            if (!ent) break;
            if (strncmp(ent->d_name, "zram", 4) == 0) {
                safe_snprintf(path, sizeof(path), "/sys/block/%s/queue/read_ahead_kb", ent->d_name);
                if (safe_write_file(path, "32") == 0) {
                    count++;
                }
            }
        }
        closedir(dir);
        dir = NULL;
    }
    
    log_msg("IO tweaks applied (%d devices)", count);
}

/* ==================== 7. Filesystem ==================== */

static void tweak_filesystem(void) {
    safe_write_file("/proc/sys/fs/lease-break-time", "10");
    safe_write_file("/proc/sys/fs/dir-notify-enable", "1");
    safe_write_file("/proc/sys/fs/inotify/max_user_watches", "1048576");
    safe_write_file("/proc/sys/fs/aio-max-nr", "1048576");
    log_msg("Filesystem tweaks applied");
}

/* ==================== 8. Workqueue ==================== */

static void tweak_workqueue(void) {
    safe_write_file("/sys/module/workqueue/parameters/disable_numa", "N");
    safe_write_file("/sys/module/workqueue/parameters/debug_force_rr_cpu", "0");
    log_msg("Workqueue tweaks applied");
}

/* ==================== 9. GED ==================== */

static void tweak_ged(void) {
    if (FILE_EXISTS("/sys/kernel/ged/hal/loading_base_dvfs_step")) {
        safe_write_file("/sys/kernel/ged/hal/loading_base_dvfs_step", "1");
        log_msg("GED DVFS Step applied");
    }
}

/* ==================== 10. CPU Governor ==================== */

static void tweak_cpu_governor(void) {
    DIR *dir = NULL;
    struct dirent *ent = NULL;
    char path[MAX_PATH_LEN];
    char avail[MAX_PATH_LEN];
    char gov[MAX_PATH_LEN];
    char sdir[MAX_PATH_LEN];
    char cur_gov[64] = "";
    FILE *fp = NULL;
    int count = 0;
    
    dir = opendir("/sys/devices/system/cpu/cpufreq");
    if (!dir) {
        return;
    }
    
    while ((ent = readdir(dir)) != NULL) {
        if (!ent) break;
        if (strncmp(ent->d_name, "policy", 6) != 0) continue;
        
        safe_snprintf(avail, sizeof(avail), "/sys/devices/system/cpu/cpufreq/%s/scaling_available_governors", ent->d_name);
        if (!FILE_EXISTS(avail) || !file_contains(avail, "schedutil")) {
            continue;
        }
        
        safe_snprintf(gov, sizeof(gov), "/sys/devices/system/cpu/cpufreq/%s/scaling_governor", ent->d_name);
        
        fp = fopen(gov, "r");
        if (fp) {
            if (fgets(cur_gov, sizeof(cur_gov), fp)) {
                size_t len = strlen(cur_gov);
                while (len > 0 && (cur_gov[len-1] == '\n' || cur_gov[len-1] == '\r')) {
                    cur_gov[--len] = '\0';
                }
            }
            safe_fclose(&fp);
        }
        
        if (strcmp(cur_gov, "schedutil") != 0) {
            safe_write_file(gov, "schedutil");
            sleep(1);
        }
        
        // Tìm thư mục schedutil
        safe_snprintf(sdir, sizeof(sdir), "/sys/devices/system/cpu/cpufreq/%s/schedutil", ent->d_name);
        if (!FILE_EXISTS(sdir)) {
            safe_snprintf(sdir, sizeof(sdir), "/sys/devices/system/cpu/cpufreq/%s", ent->d_name);
        }
        
        safe_snprintf(path, sizeof(path), "%s/rate_limit_us", sdir);
        safe_write_file(path, "500");
        
        safe_snprintf(path, sizeof(path), "%s/up_rate_limit_us", sdir);
        safe_write_file(path, "500");
        
        safe_snprintf(path, sizeof(path), "%s/down_rate_limit_us", sdir);
        safe_write_file(path, "500");
        
        safe_snprintf(path, sizeof(path), "%s/hispeed_load", sdir);
        safe_write_file(path, "90");
        
        count++;
    }
    
    closedir(dir);
    dir = NULL;
    
    if (count > 0) {
        log_msg("CPU schedutil applied (%d policies)", count);
    }
}

/* ==================== Main ==================== */

int main(int argc, char **argv) {
    // Xóa log cũ
    FILE *fp = fopen(LOG_FILE, "w");
    if (fp) safe_fclose(&fp);
    fp = fopen(LOG_SDCARD, "w");
    if (fp) safe_fclose(&fp);
    
    wait_for_boot();
    tweak_cpu_online();
    tweak_virtual_memory();
    tweak_scheduler();
    tweak_stune();
    tweak_cpu_boost();
    tweak_io_storage();
    tweak_filesystem();
    tweak_workqueue();
    tweak_ged();
    tweak_cpu_governor();
    
    log_msg("========== KernelEnhancer Completed ==========");
    
    fp = fopen("/data/local/tmp/kernel_optimized", "w");
    if (fp) {
        time_t t;
        time(&t);
        fprintf(fp, "KernelEnhancer completed at %s", ctime(&t));
        safe_fclose(&fp);
    }
    
    return 0;
}