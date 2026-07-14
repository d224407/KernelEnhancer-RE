/*
 * KernelEnhancer_clean.c
 * Dịch chính xác từ pseudo‑C gốc (32 & 64), không thêm thắt gì.
 * Biên dịch: gcc -static -O2 -o kernelenhancer KernelEnhancer_clean.c
 * (có thể dùng cross‑compiler cho ARMv7/ARMv8)
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
#include <libgen.h>

/* ==================== Định nghĩa chung ==================== */
#define LOG_FILE    "/data/local/tmp/KernelEnhancer.log"
#define LOG_SDCARD  "/sdcard/KernelEnhancer.log"

/* ----- Ghi log (giống FUN_000145c4 và FUN_00104534) ----- */
static void log_msg(const char *msg) {
    FILE *fp;
    time_t t;
    struct tm *tm;
    char buf[32];
    time(&t);
    tm = localtime(&t);
    strftime(buf, sizeof(buf), "[%H:%M:%S]", tm);

    fp = fopen(LOG_FILE, "a");
    if (fp) { fprintf(fp, "%s %s\n", buf, msg); fclose(fp); }
    fp = fopen(LOG_SDCARD, "a");
    if (fp) { fprintf(fp, "%s %s\n", buf, msg); fclose(fp); }
    printf("%s %s\n", buf, msg);
}

/* ----- Ghi giá trị vào file sysfs (giống FUN_0001469c) ----- */
static int write_file(const char *path, const char *value) {
    struct stat st;
    int ret = 0;
    FILE *fp;

    if (stat(path, &st) != 0)
        return 0;
    if (!S_ISREG(st.st_mode))
        return 0;

    // Nếu không có quyền ghi, thử chmod +w
    if (access(path, W_OK) != 0) {
        chmod(path, st.st_mode | S_IWUSR);
    }

    fp = fopen(path, "w");
    if (!fp) return 0;
    fprintf(fp, "%s\n", value);
    fclose(fp);

    // Xác minh (đọc lại và so sánh)
    char buf[256];
    fp = fopen(path, "r");
    if (fp) {
        if (fgets(buf, sizeof(buf), fp)) {
            // Xoá ký tự xuống dòng
            size_t len = strlen(buf);
            while (len > 0 && buf[len-1] < 0x21) {
                buf[--len] = '\0';
            }
            if (strcmp(buf, value) == 0)
                ret = 1;
        }
        fclose(fp);
    }

    // Trả về quyền cũ
    chmod(path, st.st_mode & 0777);
    return ret;
}

/* ----- Kiểm tra chuỗi trong file (giống FUN_0001490c) ----- */
static int file_contains(const char *path, const char *needle) {
    FILE *fp;
    char line[256];
    int found = 0;
    fp = fopen(path, "r");
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, needle)) {
            found = 1;
            break;
        }
    }
    fclose(fp);
    return found;
}

/* ----- Tune block device (giống FUN_000149c0) ----- */
static void tune_block_device(const char *devpath) {
    char path[256];
    struct stat st;

    snprintf(path, sizeof(path), "%s/read_ahead_kb", devpath);
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        write_file(path, "256");

    snprintf(path, sizeof(path), "%s/nr_requests", devpath);
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        write_file(path, "64");

    snprintf(path, sizeof(path), "%s/rq_affinity", devpath);
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        write_file(path, "2");

    snprintf(path, sizeof(path), "%s/iostats", devpath);
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        write_file(path, "0");

    snprintf(path, sizeof(path), "%s/add_random", devpath);
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        write_file(path, "0");
}

/* ----- Hàm so sánh cho qsort (giống FUN_00014b84 / FUN_00104b1c) ----- */
static int compare_freq(const void *a, const void *b) {
    long la = *(long*)a;
    long lb = *(long*)b;
    return (la > lb) - (la < lb);
}

/* ----- Hàm main (gộp logic của cả 32 và 64) ----- */
int main(int argc, char **argv) {
    char modpath[256] = "";
    char buf[256];
    FILE *fp;
    DIR *dir;
    struct dirent *ent;
    struct stat st;
    long mem_kb = 0;
    int swappiness, dirty_ratio, dirty_bg, vfs_pressure, watermark;
    char val[16];
    int i;
    long freq_list[64];
    int freq_count = 0;
    char path[256];
    char schedutil_path[256];
    char cmd[256];

    // ----- Ghi log bắt đầu -----
    log_msg("========== KernelEnhancer Started ==========");

    // ----- Xác định đường dẫn module (giống 64-bit) -----
    if (argc > 0) {
        strncpy(modpath, argv[0], sizeof(modpath)-1);
        modpath[sizeof(modpath)-1] = '\0';
        char *dir_end = strrchr(modpath, '/');
        if (dir_end) *dir_end = '\0';
        // Lấy dirname
        char *dname = dirname(modpath);
        snprintf(modpath, sizeof(modpath), "%s/module.prop", dname);
    } else {
        strcpy(modpath, "/data/adb/modules/KernelEnhancer/module.prop");
    }

    // ----- Kiểm tra module.prop (bỏ SHA) -----
    if (stat(modpath, &st) != 0 || !S_ISREG(st.st_mode)) {
        log_msg("module.prop not found, continuing anyway");
    }

    // ----- Chờ boot hoàn tất -----
    int wait = 0;
    while (wait < 30) {
        fp = popen("getprop sys.boot_completed 2>/dev/null", "r");
        if (fp) {
            if (fgets(buf, sizeof(buf), fp) && buf[0] == '1')
                break;
            pclose(fp);
        }
        sleep(2);
        wait++;
    }
    wait = 0;
    while (wait < 20) {
        fp = popen("getprop init.svc.bootanim 2>/dev/null", "r");
        if (fp) {
            if (fgets(buf, sizeof(buf), fp) && strstr(buf, "stopped"))
                break;
            pclose(fp);
        }
        sleep(2);
        wait++;
    }
    sleep(8);

    // ----- Xoá log cũ -----
    fp = fopen(LOG_FILE, "w"); if (fp) fclose(fp);
    fp = fopen(LOG_SDCARD, "w"); if (fp) fclose(fp);

    // ----- 2. Bật CPU online -----
    dir = opendir("/sys/devices/system/cpu");
    if (dir) {
        while ((ent = readdir(dir)) != NULL) {
            if (strncmp(ent->d_name, "cpu", 3) == 0 && isdigit(ent->d_name[3])) {
                snprintf(path, sizeof(path), "/sys/devices/system/cpu/%s/online", ent->d_name);
                if (access(path, F_OK) == 0)
                    write_file(path, "1");
            }
        }
        closedir(dir);
    }
    log_msg("CPU Online");

    // ----- 3. Đọc MemTotal và tính VM parameters -----
    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        while (fgets(buf, sizeof(buf), fp)) {
            if (strncmp(buf, "MemTotal:", 9) == 0) {
                sscanf(buf + 9, "%ld", &mem_kb);
                break;
            }
        }
        fclose(fp);
    }
    if (mem_kb > 0) {
        long mem_mb = mem_kb / 1024;
        if (mem_mb < 4096) {
            swappiness = 70; dirty_ratio = 18; dirty_bg = 5; vfs_pressure = 50; watermark = 149;
        } else if (mem_mb < 6144) {
            swappiness = 60; dirty_ratio = 22; dirty_bg = 6; vfs_pressure = 60; watermark = 177;
        } else if (mem_mb < 8192) {
            swappiness = 35; dirty_ratio = 25; dirty_bg = 8; vfs_pressure = 60; watermark = 191;
        } else {
            swappiness = 35; dirty_ratio = 28; dirty_bg = 10; vfs_pressure = 55; watermark = 209;
        }

        snprintf(val, sizeof(val), "%d", swappiness);
        write_file("/proc/sys/vm/swappiness", val);
        snprintf(val, sizeof(val), "%d", dirty_ratio);
        write_file("/proc/sys/vm/dirty_ratio", val);
        snprintf(val, sizeof(val), "%d", dirty_bg);
        write_file("/proc/sys/vm/dirty_background_ratio", val);
        write_file("/proc/sys/vm/dirty_expire_centisecs", "1250");
        write_file("/proc/sys/vm/dirty_writeback_centisecs", "850");
        write_file("/proc/sys/vm/page-cluster", "0");
        snprintf(val, sizeof(val), "%d", vfs_pressure);
        write_file("/proc/sys/vm/vfs_cache_pressure", val);
        write_file("/proc/sys/vm/stat_interval", "21");
        snprintf(val, sizeof(val), "%d", watermark);
        write_file("/proc/sys/vm/watermark_scale_factor", val);
        write_file("/proc/sys/vm/zone_reclaim_mode", "0");
        log_msg("VM tweaks applied");
    }

    // ----- 4. Scheduler -----
    write_file("/proc/sys/kernel/sched_downmigrate", "35 45");
    write_file("/proc/sys/kernel/sched_upmigrate", "50 60");
    write_file("/proc/sys/kernel/sched_util_clamp_min", "384");
    write_file("/proc/sys/kernel/sched_util_clamp_min_rt_default", "512");
    log_msg("Scheduler");

    // ----- 5. STUNE -----
    const char *stune_paths[] = {"/dev/stune", "/sys/fs/cgroup/stune", "/sys/fs/cgroup/cpu/stune", NULL};
    const char *stune_base = NULL;
    for (i = 0; stune_paths[i]; i++) {
        if (stat(stune_paths[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            stune_base = stune_paths[i];
            break;
        }
    }
    if (stune_base) {
        snprintf(path, sizeof(path), "%s/top-app/schedtune.boost", stune_base);
        write_file(path, "3");
        snprintf(path, sizeof(path), "%s/top-app/schedtune.prefer_idle", stune_base);
        write_file(path, "0");
        snprintf(path, sizeof(path), "%s/foreground/schedtune.boost", stune_base);
        write_file(path, "0");
        snprintf(path, sizeof(path), "%s/background/schedtune.boost", stune_base);
        write_file(path, "-10");
        log_msg("STUNE Boost (Top App)");
    }

    // ----- 6. CPU Boost (input_boost) -----
    if (stat("/sys/module/cpu_boost/parameters", &st) == 0 && S_ISDIR(st.st_mode)) {
        char boost_freq[4096] = "";
        dir = opendir("/sys/devices/system/cpu");
        if (dir) {
            while ((ent = readdir(dir)) != NULL) {
                if (strncmp(ent->d_name, "cpu", 3) == 0 && isdigit(ent->d_name[3])) {
                    snprintf(path, sizeof(path), "/sys/devices/system/cpu/%s/cpufreq/cpuinfo_max_freq", ent->d_name);
                    fp = fopen(path, "r");
                    if (fp) {
                        long freq = 0;
                        if (fscanf(fp, "%ld", &freq) == 1 && freq > 0) {
                            char pair[64];
                            snprintf(pair, sizeof(pair), " %s:%ld", ent->d_name + 3, freq / 2);
                            strcat(boost_freq, pair);
                        }
                        fclose(fp);
                    }
                }
            }
            closedir(dir);
        }
        if (boost_freq[0] == ' ') {
            memmove(boost_freq, boost_freq + 1, strlen(boost_freq));
        }
        if (strlen(boost_freq) > 0) {
            write_file("/sys/module/cpu_boost/parameters/input_boost_freq", boost_freq);
            write_file("/sys/module/cpu_boost/parameters/sched_boost_on_input", "1");
            write_file("/sys/module/cpu_boost/parameters/input_boost_ms", "50");
            write_file("/sys/module/cpu_boost/parameters/input_boost_duration", "50");
            log_msg("Input Boost");
        }
    }

    // ----- 7. I/O Scheduler và Queue -----
    dir = opendir("/sys/block");
    if (dir) {
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            snprintf(path, sizeof(path), "/sys/block/%s/queue", ent->d_name);
            if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

            // Scheduler
            char sched_path[256];
            snprintf(sched_path, sizeof(sched_path), "%s/scheduler", path);
            if (access(sched_path, F_OK) == 0) {
                if (file_contains(sched_path, "mq-deadline")) {
                    write_file(sched_path, "mq-deadline");
                    char iosched[256];
                    snprintf(iosched, sizeof(iosched), "%s/iosched", path);
                    if (stat(iosched, &st) == 0 && S_ISDIR(st.st_mode)) {
                        snprintf(buf, sizeof(buf), "%s/read_expire", iosched);
                        write_file(buf, "50");
                        snprintf(buf, sizeof(buf), "%s/write_expire", iosched);
                        write_file(buf, "150");
                        snprintf(buf, sizeof(buf), "%s/writes_starved", iosched);
                        write_file(buf, "1");
                        snprintf(buf, sizeof(buf), "%s/front_merges", iosched);
                        write_file(buf, "0");
                    }
                } else if (file_contains(sched_path, "cfq")) {
                    write_file(sched_path, "cfq");
                    char iosched[256];
                    snprintf(iosched, sizeof(iosched), "%s/iosched", path);
                    if (stat(iosched, &st) == 0 && S_ISDIR(st.st_mode)) {
                        snprintf(buf, sizeof(buf), "%s/slice_idle", iosched);
                        write_file(buf, "0");
                        snprintf(buf, sizeof(buf), "%s/low_latency", iosched);
                        write_file(buf, "1");
                        snprintf(buf, sizeof(buf), "%s/quantum", iosched);
                        write_file(buf, "8");
                        snprintf(buf, sizeof(buf), "%s/group_idle", iosched);
                        write_file(buf, "0");
                        snprintf(buf, sizeof(buf), "%s/back_seek_penalty", iosched);
                        write_file(buf, "1");
                        snprintf(buf, sizeof(buf), "%s/back_seek_max", iosched);
                        write_file(buf, "1000000000");
                        snprintf(buf, sizeof(buf), "%s/slice_sync", iosched);
                        write_file(buf, "85");
                        snprintf(buf, sizeof(buf), "%s/slice_async", iosched);
                        write_file(buf, "85");
                        snprintf(buf, sizeof(buf), "%s/slice_async_rq", iosched);
                        write_file(buf, "2");
                        snprintf(buf, sizeof(buf), "%s/slice_async_us", iosched);
                        write_file(buf, "75000");
                        snprintf(buf, sizeof(buf), "%s/target_latency_us", iosched);
                        write_file(buf, "20000");
                        snprintf(buf, sizeof(buf), "%s/fifo_expire_sync", iosched);
                        write_file(buf, "100");
                        snprintf(buf, sizeof(buf), "%s/fifo_expire_async", iosched);
                        write_file(buf, "250");
                    }
                }
            }

            // Các tham số queue chung
            tune_block_device(path);
        }
        closedir(dir);
    }

    // ZRAM riêng
    dir = opendir("/sys/block");
    if (dir) {
        while ((ent = readdir(dir)) != NULL) {
            if (strncmp(ent->d_name, "zram", 4) == 0) {
                snprintf(path, sizeof(path), "/sys/block/%s/queue/read_ahead_kb", ent->d_name);
                if (access(path, F_OK) == 0)
                    write_file(path, "32");
            }
        }
        closedir(dir);
    }
    log_msg("IO tweaks");

    // ----- 8. Filesystem -----
    write_file("/proc/sys/fs/lease-break-time", "10");
    write_file("/proc/sys/fs/dir-notify-enable", "1");
    write_file("/proc/sys/fs/inotify/max_user_watches", "1048576");
    write_file("/proc/sys/fs/aio-max-nr", "1048576");
    log_msg("Filesystem");

    // ----- 9. Workqueue -----
    write_file("/sys/module/workqueue/parameters/disable_numa", "N");
    write_file("/sys/module/workqueue/parameters/debug_force_rr_cpu", "0");
    log_msg("Miscellaneous");

    // ----- 10. GED -----
    if (access("/sys/kernel/ged/hal/loading_base_dvfs_step", F_OK) == 0) {
        write_file("/sys/kernel/ged/hal/loading_base_dvfs_step", "1");
        log_msg("GED DVFS Step");
    }

    // ----- 11. CPU Governor: schedutil -----
    dir = opendir("/sys/devices/system/cpu/cpufreq");
    if (dir) {
        while ((ent = readdir(dir)) != NULL) {
            if (strncmp(ent->d_name, "policy", 6) != 0) continue;
            snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpufreq/%s", ent->d_name);
            char avail[256];
            snprintf(avail, sizeof(avail), "%s/scaling_available_governors", path);
            if (access(avail, F_OK) != 0) continue;
            if (!file_contains(avail, "schedutil")) continue;

            // Chuyển sang schedutil
            char gov[256];
            snprintf(gov, sizeof(gov), "%s/scaling_governor", path);
            char current_gov[64] = "";
            fp = fopen(gov, "r");
            if (fp) {
                fscanf(fp, "%63s", current_gov);
                fclose(fp);
            }
            if (strcmp(current_gov, "schedutil") != 0) {
                write_file(gov, "schedutil");
                sleep(1);
            }

            // Tìm thư mục schedutil
            char sdir[256];
            snprintf(sdir, sizeof(sdir), "%s/schedutil", path);
            if (stat(sdir, &st) != 0 || !S_ISDIR(st.st_mode)) {
                strcpy(sdir, path);
            }

            snprintf(buf, sizeof(buf), "%s/rate_limit_us", sdir);
            write_file(buf, "500");
            snprintf(buf, sizeof(buf), "%s/up_rate_limit_us", sdir);
            write_file(buf, "500");
            snprintf(buf, sizeof(buf), "%s/down_rate_limit_us", sdir);
            write_file(buf, "500");
            snprintf(buf, sizeof(buf), "%s/hispeed_load", sdir);
            write_file(buf, "90");
        }
        closedir(dir);
        log_msg("CPU tweaks applied (Harmonized Profile)");
    }

    // ----- Kết thúc -----
    log_msg("========== KernelEnhancer Completed ==========");
    fp = fopen("/data/local/tmp/kernel_optimized", "w");
    if (fp) {
        time_t t;
        time(&t);
        fprintf(fp, "KernelEnhancer completed at %s", ctime(&t));
        fclose(fp);
    }

    return 0;
}