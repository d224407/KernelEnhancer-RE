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

#define LOG_FILE "/data/local/tmp/KernelEnhancer.log"
#define MAX_PATH PATH_MAX
#define MAX_LINE 512
#define MAX_CMD 1024

static void safe_fclose(FILE **fp) {
    if (fp && *fp) { fclose(*fp); *fp = NULL; }
}

static int safe_write_file(const char *path, const char *value) {
    if (!path || !value) return -1;
    FILE *fp = NULL;
    struct stat st;
    int result = -1;
    mode_t original_mode = 0;
    char read_buffer[MAX_LINE] = {0};

    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;

    original_mode = st.st_mode & 0777;
    if (access(path, W_OK) != 0) chmod(path, original_mode | S_IWUSR);

    fp = fopen(path, "w");
    if (!fp) goto cleanup;

    if (fprintf(fp, "%s\n", value) < 0) goto cleanup;
    fclose(fp);
    fp = NULL;

    fp = fopen(path, "r");
    if (fp) {
        if (fgets(read_buffer, sizeof(read_buffer), fp)) {
            size_t len = strlen(read_buffer);
            while (len > 0 && (read_buffer[len-1] == '\n' || read_buffer[len-1] == '\r'))
                read_buffer[--len] = '\0';
            if (strcmp(read_buffer, value) == 0) result = 0;
        }
        fclose(fp);
        fp = NULL;
    }

cleanup:
    safe_fclose(&fp);
    if (original_mode > 0) chmod(path, original_mode);
    return result;
}

static int file_exists(const char *path) {
    if (!path) return 0;
    struct stat st;
    return stat(path, &st) == 0;
}

static int file_contains(const char *path, const char *needle) {
    if (!path || !needle) return 0;
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    char line[MAX_LINE];
    int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, needle)) { found = 1; break; }
    }
    fclose(fp);
    return found;
}

static int run_and_capture(const char *cmd, char *out, size_t out_sz) {
    FILE *pipe = popen(cmd, "r");
    if (!pipe) { out[0] = 0; return 0; }
    char *r = fgets(out, (int)out_sz, pipe);
    pclose(pipe);
    if (!r) { out[0] = 0; return 0; }
    size_t len = strlen(out);
    while (len != 0 && (unsigned char)out[len - 1] < '!') out[--len] = 0;
    return out[0] != 0;
}

static int is_digit_string(const char *s) {
    if (!s || !*s) return 0;
    for (const char *p = s; *p; p++)
        if (*p < '0' || *p > '9') return 0;
    return 1;
}

static int get_prop_int(const char *prop, int default_val) {
    char cmd[MAX_CMD], buf[32] = {0};
    snprintf(cmd, sizeof(cmd), "getprop %s 2>/dev/null", prop);
    if (run_and_capture(cmd, buf, sizeof(buf)) && is_digit_string(buf))
        return atoi(buf);
    return default_val;
}

static void wait_for_boot(void) {
    for (int i = 0; i < 30; i++) {
        if (get_prop_int("sys.boot_completed", 0) == 1) break;
        sleep(2);
    }
    for (int i = 0; i < 20; i++) {
        char buf[32] = {0};
        run_and_capture("getprop init.svc.bootanim 2>/dev/null", buf, sizeof(buf));
        if (strstr(buf, "stopped")) break;
        sleep(2);
    }
    sleep(10);
}

static int set_prop_if_different(const char *prop, const char *desired) {
    char cmd[MAX_CMD], current[512], result_cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), "getprop \"%s\" 2>/dev/null", prop);
    run_and_capture(cmd, current, sizeof(current));

    if (strcmp(current, desired) == 0) return 0;

    snprintf(result_cmd, sizeof(result_cmd),
             "resetprop \"%s\" \"%s\" >/dev/null 2>&1", prop, desired);
    return system(result_cmd);
}

static void apply_cpu_online(void) {
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

static void apply_surfaceflinger(void) {
    set_prop_if_different("ro.surface_flinger.vsync_event_phase_offset_ns", "1300000");
    set_prop_if_different("ro.surface_flinger.vsync_sf_event_phase_offset_ns", "1300000");
    set_prop_if_different("ro.surface_flinger.set_touch_timer_ms", "200");
    set_prop_if_different("ro.surface_flinger.use_content_detection_for_refresh_rate", "true");
}

static void apply_hwui_general(void) {
    set_prop_if_different("debug.hwui.use_hint_manager", "true");
    set_prop_if_different("debug.hwui.use_buffer_age", "true");
    set_prop_if_different("debug.hwui.target_cpu_time_percent", "65");
}

static void apply_input(void) {
    set_prop_if_different("ro.max.fling_velocity", "20000");
    set_prop_if_different("ro.min.fling_velocity", "8000");
    set_prop_if_different("ro.min_pointer_dur", "8");
}

static void apply_hwui_cache(void) {
    set_prop_if_different("ro.hwui.texture_cache_size", "36");
    set_prop_if_different("ro.hwui.layer_cache_size", "20");
    set_prop_if_different("ro.hwui.r_buffer_cache_size", "10");
    set_prop_if_different("ro.hwui.path_cache_size", "14");
    set_prop_if_different("ro.hwui.gradient_cache_size", "2");
    set_prop_if_different("ro.hwui.drop_shadow_cache_size", "6");
    set_prop_if_different("ro.hwui.shape_cache_size", "4");
    set_prop_if_different("ro.hwui.text_small_cache_width", "1024");
    set_prop_if_different("ro.hwui.text_small_cache_height", "1024");
    set_prop_if_different("ro.hwui.text_large_cache_width", "2048");
    set_prop_if_different("ro.hwui.text_large_cache_height", "2048");
}

int main(void) {
    if (geteuid() != 0) return 1;

    if (access("/system/bin/getprop", F_OK) == 0) wait_for_boot();

    apply_cpu_online();
    apply_filesystem();
    apply_workqueue();
    apply_ged();
    apply_scheduler_light();
    apply_network_light();
    apply_sched_features();

    if (file_exists("/data/adb/ksu/bin/resetprop") ||
        file_exists("/data/adb/ap/bin/resetprop") ||
        system("command -v resetprop >/dev/null 2>&1") == 0) {
        apply_surfaceflinger();
        apply_hwui_general();
        apply_input();
        apply_hwui_cache();
    }

    sync();
    return 0;
}