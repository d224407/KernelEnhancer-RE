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

#define LOG_FILE   "/data/local/tmp/kernelenhancer.log"
#define MAX_PATH   PATH_MAX
#define MAX_LINE   512
#define MAX_CMD    1024
#define SCHED_PERIOD_NS 1000000LL
#define SCHED_TASKS 10

void log_msg(const char *fmt, ...) {
    char buf[MAX_LINE];
    time_t t;
    struct tm *tm_info;
    va_list args;
    FILE *fp = NULL;
    time(&t);
    tm_info = localtime(&t);
    if (!tm_info) snprintf(buf, sizeof(buf), "[??:??:??] ");
    else strftime(buf, sizeof(buf), "[%H:%M:%S] ", tm_info);
    va_start(args, fmt);
    vsnprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), fmt, args);
    va_end(args);
    printf("%s\n", buf);
    fp = fopen(LOG_FILE, "a");
    if (fp) { fprintf(fp, "%s\n", buf); fclose(fp); }
}

int safe_write_file(const char *path, const char *value) {
    if (!path || !value) return -1;
    FILE *fp = NULL;
    struct stat st;
    int ret = -1;
    mode_t old_mode = 0;
    char readback[MAX_LINE] = {0};
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        log_msg("SKIP | %s → %s (file not found)", path, value);
        return -1;
    }
    old_mode = st.st_mode & 0777;
    if (access(path, W_OK) != 0) chmod(path, old_mode | S_IWUSR);
    fp = fopen(path, "w");
    if (!fp) {
        log_msg("FAIL | %s → %s (cannot open)", path, value);
        goto restore;
    }
    if (fprintf(fp, "%s\n", value) < 0) {
        log_msg("FAIL | %s → %s (write error)", path, value);
        fclose(fp);
        fp = NULL;
        goto restore;
    }
    fclose(fp);
    fp = NULL;
    fp = fopen(path, "r");
    if (fp) {
        if (fgets(readback, sizeof(readback), fp)) {
            size_t len = strlen(readback);
            while (len > 0 && (readback[len-1] == '\n' || readback[len-1] == '\r')) readback[--len] = '\0';
            if (strcmp(readback, value) == 0) {
                log_msg("OK   | %s → %s", path, value);
                ret = 0;
            } else log_msg("FAIL | %s → %s (readback: %s)", path, value, readback);
        }
        fclose(fp);
        fp = NULL;
    }
restore:
    if (old_mode > 0) chmod(path, old_mode);
    return ret;
}

int file_contains(const char *path, const char *needle) {
    if (!path || !needle) return 0;
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    char line[MAX_LINE];
    int found = 0;
    while (fgets(line, sizeof(line), fp)) if (strstr(line, needle)) { found = 1; break; }
    fclose(fp);
    return found;
}

int get_prop_int(const char *prop, int default_val) {
    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), "getprop %s 2>/dev/null", prop);
    FILE *fp = popen(cmd, "r");
    if (!fp) return default_val;
    char buf[32] = {0};
    if (fgets(buf, sizeof(buf), fp)) {
        if (strcmp(buf, "1\n") == 0 || strcmp(buf, "1") == 0) { pclose(fp); return 1; }
    }
    pclose(fp);
    return default_val;
}

void wait_for_boot(void) {
    log_msg("Waiting for boot...");
    int retry = 0;
    while (retry < 30) { if (get_prop_int("sys.boot_completed", 0) == 1) break; sleep(2); retry++; }
    retry = 0;
    while (retry < 20) {
        char cmd[MAX_CMD];
        snprintf(cmd, sizeof(cmd), "getprop init.svc.bootanim 2>/dev/null");
        FILE *fp = popen(cmd, "r");
        if (fp) { char buf[32] = {0}; if (fgets(buf, sizeof(buf), fp) && strstr(buf, "stopped")) { pclose(fp); break; } pclose(fp); }
        sleep(2);
        retry++;
    }
    sleep(3);
}

long get_total_mem_mb(void) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0;
    char line[MAX_LINE];
    long kb = 0;
    while (fgets(line, sizeof(line), fp)) if (strncmp(line, "MemTotal:", 9) == 0) { sscanf(line + 9, "%ld", &kb); break; }
    fclose(fp);
    return kb / 1024;
}

void apply_cpu_online(void) {
    DIR *dir = opendir("/sys/devices/system/cpu");
    if (!dir) return;
    struct dirent *ent;
    char path[MAX_PATH];
    while ((ent = readdir(dir))) {
        if (strncmp(ent->d_name, "cpu", 3) == 0 && isdigit(ent->d_name[3])) {
            snprintf(path, sizeof(path), "/sys/devices/system/cpu/%s/online", ent->d_name);
            safe_write_file(path, "1");
        }
    }
    closedir(dir);
}

void apply_vm(void) {
    long mb = get_total_mem_mb();
    if (mb <= 0) return;
    int swappiness, dirty_ratio, dirty_bg, vfs_pressure, watermark;
    if (mb < 4096) { swappiness = 70; dirty_ratio = 18; dirty_bg = 5; vfs_pressure = 50; watermark = 149; }
    else if (mb < 6144) { swappiness = 60; dirty_ratio = 22; dirty_bg = 6; vfs_pressure = 60; watermark = 177; }
    else if (mb < 8192) { swappiness = 35; dirty_ratio = 25; dirty_bg = 8; vfs_pressure = 60; watermark = 191; }
    else { swappiness = 35; dirty_ratio = 28; dirty_bg = 10; vfs_pressure = 55; watermark = 209; }
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", swappiness); safe_write_file("/proc/sys/vm/swappiness", buf);
    snprintf(buf, sizeof(buf), "%d", dirty_ratio); safe_write_file("/proc/sys/vm/dirty_ratio", buf);
    snprintf(buf, sizeof(buf), "%d", dirty_bg); safe_write_file("/proc/sys/vm/dirty_background_ratio", buf);
    safe_write_file("/proc/sys/vm/dirty_expire_centisecs", "1250");
    safe_write_file("/proc/sys/vm/dirty_writeback_centisecs", "850");
    safe_write_file("/proc/sys/vm/page-cluster", "0");
    snprintf(buf, sizeof(buf), "%d", vfs_pressure); safe_write_file("/proc/sys/vm/vfs_cache_pressure", buf);
    safe_write_file("/proc/sys/vm/stat_interval", "21");
    snprintf(buf, sizeof(buf), "%d", watermark); safe_write_file("/proc/sys/vm/watermark_scale_factor", buf);
    safe_write_file("/proc/sys/vm/zone_reclaim_mode", "0");
}

void apply_scheduler(void) {
    safe_write_file("/proc/sys/kernel/sched_autogroup_enabled", "1");
    safe_write_file("/proc/sys/kernel/sched_child_runs_first", "1");
    safe_write_file("/proc/sys/kernel/sched_tunable_scaling", "0");
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", SCHED_PERIOD_NS);
    safe_write_file("/proc/sys/kernel/sched_latency_ns", buf);
    snprintf(buf, sizeof(buf), "%lld", SCHED_PERIOD_NS / SCHED_TASKS);
    safe_write_file("/proc/sys/kernel/sched_min_granularity_ns", buf);
    snprintf(buf, sizeof(buf), "%lld", SCHED_PERIOD_NS / 2);
    safe_write_file("/proc/sys/kernel/sched_wakeup_granularity_ns", buf);
    safe_write_file("/proc/sys/kernel/sched_migration_cost_ns", "5000000");
    safe_write_file("/proc/sys/kernel/sched_nr_migrate", "4");
    safe_write_file("/proc/sys/kernel/sched_schedstats", "0");
    safe_write_file("/proc/sys/kernel/sched_downmigrate", "35 45");
    safe_write_file("/proc/sys/kernel/sched_upmigrate", "50 60");
    safe_write_file("/proc/sys/kernel/sched_util_clamp_min", "256"); /* hạ từ 384 -> 256: 384 ép core chạy tần số khá cao ngay cả lúc idle nhẹ, dễ gây hao pin/nóng máy trên thiết bị yếu hơn */
    safe_write_file("/proc/sys/kernel/sched_util_clamp_min_rt_default", "384"); /* hạ tương ứng để đồng nhất với mức trên */
}

void apply_stune(void) {
    const char *paths[] = {"/dev/stune", "/sys/fs/cgroup/stune", "/sys/fs/cgroup/cpu/stune", NULL};
    const char *base = NULL;
    for (int i = 0; paths[i]; i++) { struct stat st; if (stat(paths[i], &st) == 0 && S_ISDIR(st.st_mode)) { base = paths[i]; break; } }
    if (!base) return;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/top-app/schedtune.boost", base); safe_write_file(path, "3");
    snprintf(path, sizeof(path), "%s/top-app/schedtune.prefer_idle", base); safe_write_file(path, "1");
    snprintf(path, sizeof(path), "%s/foreground/schedtune.boost", base); safe_write_file(path, "0");
    snprintf(path, sizeof(path), "%s/background/schedtune.boost", base); safe_write_file(path, "-10");
}

void apply_cpu_boost(void) {
    if (access("/sys/module/cpu_boost/parameters", F_OK) != 0) return;
    char boost_freq[MAX_LINE] = {0};
    DIR *dir = opendir("/sys/devices/system/cpu");
    if (!dir) return;
    struct dirent *ent;
    char freq_path[MAX_PATH];
    while ((ent = readdir(dir))) {
        if (strncmp(ent->d_name, "cpu", 3) != 0 || !isdigit(ent->d_name[3])) continue;
        snprintf(freq_path, sizeof(freq_path), "/sys/devices/system/cpu/%s/cpufreq/cpuinfo_max_freq", ent->d_name);
        FILE *fp = fopen(freq_path, "r");
        if (!fp) continue;
        long max_freq;
        if (fscanf(fp, "%ld", &max_freq) == 1 && max_freq > 0) {
            long half = max_freq / 2;
            char pair[32];
            snprintf(pair, sizeof(pair), " %s:%ld", ent->d_name + 3, half);
            if (strlen(boost_freq) + strlen(pair) + 1 < sizeof(boost_freq)) strcat(boost_freq, pair);
        }
        fclose(fp);
    }
    closedir(dir);
    if (boost_freq[0] == ' ') memmove(boost_freq, boost_freq + 1, strlen(boost_freq));
    if (strlen(boost_freq) > 0) {
        safe_write_file("/sys/module/cpu_boost/parameters/input_boost_freq", boost_freq);
        safe_write_file("/sys/module/cpu_boost/parameters/sched_boost_on_input", "1");
        safe_write_file("/sys/module/cpu_boost/parameters/input_boost_ms", "50");
        safe_write_file("/sys/module/cpu_boost/parameters/input_boost_duration", "50");
    }
}

void apply_io(void) {
    DIR *dir = opendir("/sys/block");
    if (!dir) return;
    struct dirent *ent;
    char path[MAX_PATH], sched_path[MAX_PATH];
    while ((ent = readdir(dir))) {
        if (ent->d_name[0] == '.') continue;
        snprintf(sched_path, sizeof(sched_path), "/sys/block/%s/queue/scheduler", ent->d_name);
        if (access(sched_path, F_OK) != 0) continue;
        char iosched_path[MAX_PATH];
        snprintf(iosched_path, sizeof(iosched_path), "/sys/block/%s/queue/iosched", ent->d_name);
        if (file_contains(sched_path, "mq-deadline")) {
            safe_write_file(sched_path, "mq-deadline");
            if (access(iosched_path, F_OK) == 0) {
                snprintf(path, sizeof(path), "%s/read_expire", iosched_path); safe_write_file(path, "50");
                snprintf(path, sizeof(path), "%s/write_expire", iosched_path); safe_write_file(path, "150");
                snprintf(path, sizeof(path), "%s/writes_starved", iosched_path); safe_write_file(path, "1");
                snprintf(path, sizeof(path), "%s/front_merges", iosched_path); safe_write_file(path, "0");
            }
        } else if (file_contains(sched_path, "cfq")) {
            safe_write_file(sched_path, "cfq");
            if (access(iosched_path, F_OK) == 0) {
                snprintf(path, sizeof(path), "%s/slice_idle", iosched_path); safe_write_file(path, "0");
                snprintf(path, sizeof(path), "%s/low_latency", iosched_path); safe_write_file(path, "1");
                snprintf(path, sizeof(path), "%s/quantum", iosched_path); safe_write_file(path, "8");
                snprintf(path, sizeof(path), "%s/group_idle", iosched_path); safe_write_file(path, "0");
                snprintf(path, sizeof(path), "%s/back_seek_penalty", iosched_path); safe_write_file(path, "1");
                snprintf(path, sizeof(path), "%s/back_seek_max", iosched_path); safe_write_file(path, "1000000000");
                snprintf(path, sizeof(path), "%s/slice_sync", iosched_path); safe_write_file(path, "85");
                snprintf(path, sizeof(path), "%s/slice_async", iosched_path); safe_write_file(path, "85");
                snprintf(path, sizeof(path), "%s/slice_async_rq", iosched_path); safe_write_file(path, "2");
                snprintf(path, sizeof(path), "%s/slice_async_us", iosched_path); safe_write_file(path, "75000");
                snprintf(path, sizeof(path), "%s/target_latency_us", iosched_path); safe_write_file(path, "20000");
                snprintf(path, sizeof(path), "%s/fifo_expire_sync", iosched_path); safe_write_file(path, "100");
                snprintf(path, sizeof(path), "%s/fifo_expire_async", iosched_path); safe_write_file(path, "250");
            }
        }
        snprintf(path, sizeof(path), "/sys/block/%s/queue/read_ahead_kb", ent->d_name);
        if (strncmp(ent->d_name, "zram", 4) == 0) safe_write_file(path, "32");
        else safe_write_file(path, "256");
        snprintf(path, sizeof(path), "/sys/block/%s/queue/nr_requests", ent->d_name);
        safe_write_file(path, "64");
        snprintf(path, sizeof(path), "/sys/block/%s/queue/rq_affinity", ent->d_name);
        safe_write_file(path, "2");
        snprintf(path, sizeof(path), "/sys/block/%s/queue/iostats", ent->d_name);
        safe_write_file(path, "0");
        snprintf(path, sizeof(path), "/sys/block/%s/queue/add_random", ent->d_name);
        safe_write_file(path, "0");
    }
    closedir(dir);
}

void apply_filesystem(void) {
    safe_write_file("/proc/sys/fs/lease-break-time", "10");
    safe_write_file("/proc/sys/fs/dir-notify-enable", "1");
    safe_write_file("/proc/sys/fs/inotify/max_user_watches", "1048576");
    safe_write_file("/proc/sys/fs/aio-max-nr", "1048576");
}

void apply_workqueue(void) {
    safe_write_file("/sys/module/workqueue/parameters/disable_numa", "N");
    safe_write_file("/sys/module/workqueue/parameters/debug_force_rr_cpu", "0");
}

void apply_ged(void) {
    if (access("/sys/kernel/ged/hal/loading_base_dvfs_step", F_OK) == 0)
        safe_write_file("/sys/kernel/ged/hal/loading_base_dvfs_step", "1");
}

void apply_governor(void) {
    DIR *dir = opendir("/sys/devices/system/cpu/cpufreq");
    if (!dir) return;
    struct dirent *ent;
    char path[MAX_PATH], gov_path[MAX_PATH];
    while ((ent = readdir(dir))) {
        if (strncmp(ent->d_name, "policy", 6) != 0) continue;
        snprintf(gov_path, sizeof(gov_path), "/sys/devices/system/cpu/cpufreq/%s/scaling_governor", ent->d_name);
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpufreq/%s/scaling_available_governors", ent->d_name);
        if (!file_contains(path, "schedutil")) continue;
        safe_write_file(gov_path, "schedutil");
        sleep(1);
        char sched_dir[MAX_PATH];
        snprintf(sched_dir, sizeof(sched_dir), "/sys/devices/system/cpu/cpufreq/%s/schedutil", ent->d_name);
        if (access(sched_dir, F_OK) != 0) snprintf(sched_dir, sizeof(sched_dir), "/sys/devices/system/cpu/cpufreq/%s", ent->d_name);
        /* 500us quá thấp so với mặc định thường thấy (10000-20000us): ép CPU đổi tần số liên tục,
           dễ gây giật/đơ tạm thời trên kernel không handle tốt. Nâng lên mức vẫn nhạy nhưng an toàn hơn. */
        snprintf(path, sizeof(path), "%s/rate_limit_us", sched_dir); safe_write_file(path, "2000");
        snprintf(path, sizeof(path), "%s/up_rate_limit_us", sched_dir); safe_write_file(path, "1000");
        snprintf(path, sizeof(path), "%s/down_rate_limit_us", sched_dir); safe_write_file(path, "20000");
        snprintf(path, sizeof(path), "%s/hispeed_load", sched_dir); safe_write_file(path, "90");
    }
    closedir(dir);
}

void apply_network(void) {
    safe_write_file("/proc/sys/net/ipv4/tcp_ecn", "1");
    safe_write_file("/proc/sys/net/ipv4/tcp_fastopen", "3");
    safe_write_file("/proc/sys/net/ipv4/tcp_syncookies", "1"); /* giữ bật để chống SYN flood, tắt (0) không mang lại lợi ích rõ rệt trên mobile mà lại tăng rủi ro */
}

void apply_debug(void) {
    safe_write_file("/proc/sys/kernel/perf_cpu_time_max_percent", "3");
    safe_write_file("/proc/sys/kernel/printk_devkmsg", "off");
    safe_write_file("/proc/sys/kernel/sched_schedstats", "0");
    if (access("/sys/kernel/debug/sched_features", F_OK) == 0) {
        safe_write_file("/sys/kernel/debug/sched_features", "NEXT_BUDDY");
        safe_write_file("/sys/kernel/debug/sched_features", "NO_TTWU_QUEUE");
    }
}

int main(void) {
    /* Giữ log cũ giữa các lần chạy để tiện đối chiếu lịch sử; nếu file vượt 1MB thì reset để tránh phình to vô hạn */
    struct stat log_st;
    if (stat(LOG_FILE, &log_st) == 0 && log_st.st_size > 1024 * 1024) {
        FILE *fp = fopen(LOG_FILE, "w");
        if (fp) fclose(fp);
    }
    time_t now = time(NULL);
    char time_buf[64];
    struct tm *tm_now = localtime(&now);
    if (tm_now) strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_now);
    else snprintf(time_buf, sizeof(time_buf), "unknown");
    log_msg("==========================================");
    log_msg("KERNELENHANCER START: %s", time_buf);
    log_msg("==========================================");
    if (geteuid() != 0) { log_msg("ERROR: Need root"); return 1; }
    if (access("/system/bin/getprop", F_OK) == 0) wait_for_boot();
    apply_cpu_online();
    apply_vm();
    apply_scheduler();
    apply_stune();
    apply_cpu_boost();
    apply_io();
    apply_filesystem();
    apply_workqueue();
    apply_ged();
    apply_governor();
    apply_network();
    apply_debug();
    sync();
    now = time(NULL);
    tm_now = localtime(&now);
    if (tm_now) strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_now);
    else snprintf(time_buf, sizeof(time_buf), "unknown");
    log_msg("==========================================");
    log_msg("KERNELENHANCER COMPLETED: %s", time_buf);
    log_msg("Log saved to: %s", LOG_FILE);
    log_msg("==========================================");
    return 0;
}
