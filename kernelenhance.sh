#!/system/bin/sh

# ==============================================
# KernelEnhancer – Android Kernel Tuning Script
# Dịch từ KernelEnhancer32/64.c, bỏ SHA‑256 check
# ==============================================

LOG_FILE="/data/local/tmp/KernelEnhancer.log"
LOG_SDCARD="/sdcard/KernelEnhancer.log"

# Hàm ghi log
log() {
    local msg="[$(date +'%H:%M')] $1"
    echo "$msg" >> "$LOG_FILE"
    echo "$msg" >> "$LOG_SDCARD"
}

# Hàm ghi giá trị vào file sysfs (tự động thử quyền)
write() {
    local file="$1"
    local value="$2"
    [ -f "$file" ] || return
    chmod 644 "$file" 2>/dev/null
    echo "$value" > "$file" 2>/dev/null
}

# ----- 1. Chờ hệ thống boot hoàn tất -----
log "KernelEnhancer Started"

# Đợi boot_completed = 1
while [ "$(getprop sys.boot_completed)" != "1" ]; do
    sleep 2
done

# Đợi boot animation dừng
while [ "$(getprop init.svc.bootanim)" != "stopped" ]; do
    sleep 2
done

sleep 8   # Đợi thêm cho ổn định

# ----- 2. Bật tất cả các nhân CPU (online) -----
for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
    online="$cpu/online"
    [ -f "$online" ] && write "$online" "1"
done
log "CPU Online"

# ----- 3. Đọc MemTotal và tính toán tham số VM -----
mem_kb=$(grep MemTotal /proc/meminfo | awk '{print $2}')
mem_mb=$((mem_kb / 1024))

# Đặt giá trị mặc định (theo logic trong code gốc)
if [ "$mem_mb" -lt 4096 ]; then
    swappiness=70
    dirty_ratio=18
    dirty_bg=5
    vfs_pressure=50
    watermark=149
elif [ "$mem_mb" -lt 6144 ]; then
    swappiness=60
    dirty_ratio=22
    dirty_bg=6
    vfs_pressure=60
    watermark=177
elif [ "$mem_mb" -lt 8192 ]; then
    swappiness=35
    dirty_ratio=25
    dirty_bg=8
    vfs_pressure=60
    watermark=191
else
    swappiness=35
    dirty_ratio=28
    dirty_bg=10
    vfs_pressure=55
    watermark=209
fi

# Ghi các tham số VM
write "/proc/sys/vm/swappiness" "$swappiness"
write "/proc/sys/vm/dirty_ratio" "$dirty_ratio"
write "/proc/sys/vm/dirty_background_ratio" "$dirty_bg"
write "/proc/sys/vm/dirty_expire_centisecs" "1250"
write "/proc/sys/vm/dirty_writeback_centisecs" "850"
write "/proc/sys/vm/page-cluster" "0"
write "/proc/sys/vm/vfs_cache_pressure" "$vfs_pressure"
write "/proc/sys/vm/stat_interval" "21"
write "/proc/sys/vm/watermark_scale_factor" "$watermark"
write "/proc/sys/vm/zone_reclaim_mode" "0"
log "VM tweaks applied"

# ----- 4. Scheduler -----
write "/proc/sys/kernel/sched_downmigrate" "35 45"
write "/proc/sys/kernel/sched_upmigrate" "50 60"
write "/proc/sys/kernel/sched_util_clamp_min" "384"
write "/proc/sys/kernel/sched_util_clamp_min_rt_default" "512"
log "Scheduler"

# ----- 5. STUNE (nếu có) -----
stune_path=""
[ -d "/dev/stune" ] && stune_path="/dev/stune"
[ -z "$stune_path" ] && [ -d "/sys/fs/cgroup/stune" ] && stune_path="/sys/fs/cgroup/stune"
[ -z "$stune_path" ] && [ -d "/sys/fs/cgroup/cpu/stune" ] && stune_path="/sys/fs/cgroup/cpu/stune"

if [ -n "$stune_path" ]; then
    write "$stune_path/top-app/schedtune.boost" "3"
    write "$stune_path/top-app/schedtune.prefer_idle" "0"
    write "$stune_path/foreground/schedtune.boost" "0"
    write "$stune_path/background/schedtune.boost" "-10"
    log "STUNE Boost (Top App)"
fi

# ----- 6. CPU Boost (input_boost) -----
if [ -d "/sys/module/cpu_boost/parameters" ]; then
    # Tạo chuỗi "cpuX:max_freq/2" cho tất cả CPU
    boost_freq=""
    for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
        freq_file="$cpu/cpufreq/cpuinfo_max_freq"
        if [ -f "$freq_file" ]; then
            max_freq=$(cat "$freq_file" 2>/dev/null)
            if [ -n "$max_freq" ]; then
                half=$((max_freq / 2))
                cpu_id=${cpu##*/cpu}
                boost_freq="$boost_freq cpu$cpu_id:$half"
            fi
        fi
    done
    boost_freq=${boost_freq# }  # bỏ khoảng trắng đầu
    write "/sys/module/cpu_boost/parameters/input_boost_freq" "$boost_freq"
    write "/sys/module/cpu_boost/parameters/sched_boost_on_input" "1"
    write "/sys/module/cpu_boost/parameters/input_boost_ms" "50"
    write "/sys/module/cpu_boost/parameters/input_boost_duration" "50"
    log "Input Boost"
fi

# ----- 7. I/O Scheduler và Queue -----
for block in /sys/block/*; do
    dev=$(basename "$block")
    queue="$block/queue"
    [ -d "$queue" ] || continue

    scheduler="$queue/scheduler"
    if [ -f "$scheduler" ]; then
        # Ưu tiên mq‑deadline, nếu không thì cfq
        if grep -q "mq-deadline" "$scheduler"; then
            write "$scheduler" "mq-deadline"
            iosched="$queue/iosched"
            if [ -d "$iosched" ]; then
                write "$iosched/read_expire" "50"
                write "$iosched/write_expire" "150"
                write "$iosched/writes_starved" "1"
                write "$iosched/front_merges" "0"
            fi
        elif grep -q "cfq" "$scheduler"; then
            write "$scheduler" "cfq"
            iosched="$queue/iosched"
            if [ -d "$iosched" ]; then
                write "$iosched/slice_idle" "0"
                write "$iosched/low_latency" "1"
                write "$iosched/quantum" "8"
                write "$iosched/group_idle" "0"
                write "$iosched/back_seek_penalty" "1"
                write "$iosched/back_seek_max" "1000000000"
                write "$iosched/slice_sync" "85"
                write "$iosched/slice_async" "85"
                write "$iosched/slice_async_rq" "2"
                write "$iosched/slice_async_us" "75000"
                write "$iosched/target_latency_us" "20000"
                write "$iosched/fifo_expire_sync" "100"
                write "$iosched/fifo_expire_async" "250"
            fi
        fi
    fi

    # Các tham số chung của queue
    write "$queue/read_ahead_kb" "256"
    write "$queue/nr_requests" "64"
    write "$queue/rq_affinity" "2"
    write "$queue/iostats" "0"
    write "$queue/add_random" "0"
done

# ZRAM riêng biệt
for block in /sys/block/zram*; do
    queue="$block/queue"
    [ -d "$queue" ] && write "$queue/read_ahead_kb" "32"
done
log "IO tweaks"

# ----- 8. Filesystem -----
write "/proc/sys/fs/lease-break-time" "10"
write "/proc/sys/fs/dir-notify-enable" "1"
write "/proc/sys/fs/inotify/max_user_watches" "1048576"
write "/proc/sys/fs/aio-max-nr" "1048576"
log "Filesystem"

# ----- 9. Workqueue -----
write "/sys/module/workqueue/parameters/disable_numa" "N"
write "/sys/module/workqueue/parameters/debug_force_rr_cpu" "0"
log "Miscellaneous"

# ----- 10. GED (nếu có) -----
write "/sys/kernel/ged/hal/loading_base_dvfs_step" "1"

# ----- 11. CPU Governor: chuyển sang schedutil và tinh chỉnh -----
for policy in /sys/devices/system/cpu/cpufreq/policy*; do
    [ -d "$policy" ] || continue
    avail="$policy/scaling_available_governors"
    gov="$policy/scaling_governor"
    [ -f "$avail" ] || continue

    if grep -q "schedutil" "$avail"; then
        current=$(cat "$gov" 2>/dev/null)
        if [ "$current" != "schedutil" ]; then
            write "$gov" "schedutil"
            sleep 1
        fi
        # Đặt các tham số schedutil (nếu có thư mục con)
        if [ -d "$policy/schedutil" ]; then
            sdir="$policy/schedutil"
        else
            sdir="$policy"   # một số thiết bị để trực tiếp trong policy
        fi
        # Dùng giá trị mặc định (có thể điều chỉnh theo số policy nếu muốn)
        write "$sdir/rate_limit_us" "500"
        write "$sdir/up_rate_limit_us" "500"
        write "$sdir/down_rate_limit_us" "500"
        write "$sdir/hispeed_load" "90"
    fi
done
log "CPU tweaks applied (Harmonized Profile)"

log "KernelEnhancer Completed"
