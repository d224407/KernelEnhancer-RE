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

#define LOG_FILE "/data/local/tmp/KernelEnhancer_Compatible.log"
#define MAX_LINE_LEN 512
#define MAX_PATH_LEN PATH_MAX

static void safe_fclose(FILE **fp) {
    if (fp && *fp) {
        fclose(*fp);
        *fp = NULL;
    }
}

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
}

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
        log_msg("Failed to open %s", path);
        goto cleanup;
    }
    
    if (fprintf(fp, "%s\n", value) < 0) {
        log_msg("Failed to write %s", path);
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

static int file_exists(const char *path) {
    if (!path) return 0;
    struct stat st;
    return stat(path, &st) == 0;
}


static void apply_cpu_online(void) {
    DIR *dir = opendir("/sys/devices/system/cpu");
    if (!dir) return;
    struct dirent *ent;
    char path[MAX_PATH_LEN];
    while ((ent = readdir(dir))) {
        if (strncmp(ent->d_name, "cpu", 3) == 0 && isdigit(ent->d_name[3])) {
            snprintf(path, sizeof(path), "/sys/devices/system/cpu/%s/online", ent->d_name);
            safe_write_file(path, "1");
        }
    }
    closedir(dir);
}

static void apply_filesystem(void) {
    safe_write_file("/proc/sys/fs/lease-break-time", "10");
    safe_write_file("/proc/sys/fs/dir-notify-enable", "1");
    safe_write_file("/proc/sys/fs/inotify/max_user_watches", "1048576");
    safe_write_file("/proc/sys/fs/aio-max-nr", "1048576");
}

static void apply_workqueue(void) {
    safe_write_file("/sys/module/workqueue/parameters/disable_numa", "N");
    safe_write_file("/sys/module/workqueue/parameters/debug_force_rr_cpu", "0");
}

static void apply_ged(void) {
    if (access("/sys/kernel/ged/hal/loading_base_dvfs_step", F_OK) == 0)
        safe_write_file("/sys/kernel/ged/hal/loading_base_dvfs_step", "1");
}

static void apply_scheduler_light(void) {
    safe_write_file("/proc/sys/kernel/perf_cpu_time_max_percent", "3");
    safe_write_file("/proc/sys/kernel/sched_autogroup_enabled", "1");
    safe_write_file("/proc/sys/kernel/sched_child_runs_first", "1");
    safe_write_file("/proc/sys/kernel/printk_devkmsg", "off");
}

static void apply_network_light(void) {
    safe_write_file("/proc/sys/net/ipv4/tcp_ecn", "1");
    safe_write_file("/proc/sys/net/ipv4/tcp_fastopen", "3");
}

static void apply_sched_features(void) {
    if (file_exists("/sys/kernel/debug/sched_features")) {
        safe_write_file("/sys/kernel/debug/sched_features", "NEXT_BUDDY");
        safe_write_file("/sys/kernel/debug/sched_features", "NO_TTWU_QUEUE");
    }
}


int main(void) {
    log_msg("========================================");
    log_msg("KernelEnhancer - Compatible Mode");
    log_msg("(No conflicts with FDE.AI)");
    log_msg("========================================");
    
    if (geteuid() != 0) {
        log_msg("ERROR: Need root");
        return 1;
    }
    
    log_msg("Applying safe optimizations only...");
    
    apply_cpu_online();
    apply_filesystem();
    apply_workqueue();
    apply_ged();
    apply_scheduler_light();
    apply_network_light();
    apply_sched_features();
    
    log_msg("========================================");
    log_msg("Optimizations applied successfully");
    log_msg("Log saved to: %s", LOG_FILE);
    log_msg("========================================");
    
    return 0;
}