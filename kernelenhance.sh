#!/system/sh
# Chuyển thể từ KernelEnhancer64.c - Đã loại bỏ check SHA256 chống sửa đổi

# 1. Chờ máy boot xong hoàn toàn
while [ "$(getprop sys.boot_completed)" != "1" ]; do sleep 2; done
while [ "$(getprop init.svc.bootanim)" != "stopped" ]; do sleep 2; done
sleep 8

# Tạo log
echo "KernelEnhancer Shell Version Started" > /data/local/tmp/KernelEnhancer.log
echo "KernelEnhancer Shell Version Started" > /sdcard/KernelEnhancer.log

# 2. Ép tất cả các nhân CPU đang có phải Online
for cpu_online in /sys/devices/system/cpu/cpu*/online; do
    if [ -f "$cpu_online" ]; then
        chmod +w "$cpu_online" 2>/dev/null
        echo 1 > "$cpu_online"
    fi
done

# 3. Tự động tính toán Virtual Memory (VM) theo mức RAM giống hệt file C
RAM_KB=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
RAM_MB=$((RAM_KB / 1024))

if [ "$RAM_MB" -lt 4100 ]; then # RAM <= 4GB
    SWAP=70 && DIRTY=18 && BG_DIRTY=5 && VFS=50 && WATERMARK=149
elif [ "$RAM_MB" -lt 6150 ]; then # RAM <= 6GB
    SWAP=60 && DIRTY=22 && BG_DIRTY=6 && VFS=60 && WATERMARK=177
else # RAM >= 8GB
    SWAP=35 && VFS=55
    if [ "$RAM_MB" -gt 8200 ]; then
        DIRTY=28 && BG_DIRTY=10 && WATERMARK=209 && VFS=55
    else
        DIRTY=25 && BG_DIRTY=8 && WATERMARK=191
    fi
fi

# Áp dụng thông số VM
echo $SWAP > /proc/sys/vm/swappiness
echo $DIRTY > /proc/sys/vm/dirty_ratio
echo $BG_DIRTY > /proc/sys/vm/dirty_background_ratio
echo 1250 > /proc/sys/vm/dirty_expire_centisecs
echo 850 > /proc/sys/vm/dirty_writeback_centisecs
echo 0 > /proc/sys/vm/page-cluster
echo $VFS > /proc/sys/vm/vfs_cache_pressure
echo 21 > /proc/sys/vm/stat_interval
echo $WATERMARK > /proc/sys/vm/watermark_scale_factor
echo 0 > /proc/sys/vm/zone_reclaim_mode

# 4. Tối ưu Bộ lập lịch (Scheduler) & Vòng lặp cưỡng bức chống ROM ghi đè
echo "35 45" > /proc/sys/kernel/sched_downmigrate

# Vòng lặp ghi đè sched_upmigrate kĩ càng giống file C ban đầu
for i in 1 2 3; do
    echo "50 60" > /proc/sys/kernel/sched_upmigrate
    sleep 2
    # Nếu đã nhận cấu hình thành công thì thoát vòng lặp sớm
    grep -q "50" /proc/sys/kernel/sched_upmigrate && break
done

echo 384 > /proc/sys/kernel/sched_util_clamp_min
echo 512 > /proc/sys/kernel/sched_util_clamp_min_rt_default

# 5. Tối ưu hóa Schedtune cho các phân vùng nhóm (EAS)
for stune_path in /dev/stune /sys/fs/cgroup/stune /sys/fs/cgroup/cpu/stune; do
    if [ -d "$stune_path" ]; then
        # Bạn có thể đổi số 3 này thành số 1, và số 0 thành số 1 theo ý bạn ở câu trước nhé!
        echo 3 > "$stune_path/top-app/schedtune.boost"
        echo 0 > "$stune_path/top-app/schedtune.prefer_idle"
        
        echo 0 > "$stune_path/foreground/schedtune.boost"
        echo -10 > "$stune_path/background/schedtune.boost"
    fi
done

echo "=== KERNEL ENHANCER APPLIED SUCCESSFULLY ==="