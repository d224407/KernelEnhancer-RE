#!/system/sh
# Siêu Script: Kết hợp TouchEnhancer v3 & Tối ưu EAS Kernel (KTweak Concept)

# 1. Chờ máy khởi động xong hẳn
while [ "$(getprop sys.boot_completed)" != "1" ]; do
    sleep 5
done
sleep 10

# 2. Lấy thông số phần cứng của máy
SDK=$(getprop ro.build.version.sdk)
REFRESH=$(wm refresh-rate 2>/dev/null | grep -oE '[0-9]+' | head -1)
[ -z "$REFRESH" ] && REFRESH=60

WIDTH=$(wm size 2>/dev/null | grep -oE '[0-9]+' | head -1)
[ -z "$WIDTH" ] && WIDTH=1080

RAM_KB=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
RAM_GB=$((RAM_KB / 1024 / 1024))
[ -z "$RAM_GB" ] && RAM_GB=4

# 3. Tính toán thông số theo Tần số quét (Refresh Rate)
if [ "$REFRESH" -lt 120 ]; then
    VSYNC=1700000
    TIMER=200
    MAX_FLING=16000
    MIN_FLING=50
    CPU_TARGET=65
    if [ "$REFRESH" -gt 89 ]; then # Màn 90Hz
        VSYNC=1179648
        TIMER=180
        MAX_FLING=18000
        MIN_FLING=60
        CPU_TARGET=70
    fi
elif [ "$REFRESH" -lt 144 ]; then # Màn 120Hz
    VSYNC=900000
    TIMER=150
    MAX_FLING=20000
    MIN_FLING=80
    CPU_TARGET=75
else # Màn 144Hz trở lên
    VSYNC=700000
    TIMER=100
    MAX_FLING=22000
    MIN_FLING=100
    CPU_TARGET=80
fi

# 4. Áp dụng các Tweaks hệ thống (Tối ưu phản hồi Đồ họa & Cảm ứng)
resetprop ro.surface_flinger.vsync_event_phase_offset_ns $VSYNC
resetprop ro.surface_flinger.vsync_sf_event_phase_offset_ns $VSYNC
resetprop ro.surface_flinger.set_touch_timer_ms $TIMER

if [ "$SDK" -gt 30 ]; then
    resetprop ro.surface_flinger.use_content_detection_for_refresh_rate 1
fi

resetprop debug.hwui.use_hint_manager 1
resetprop debug.hwui.use_buffer_age 1
resetprop debug.hwui.target_cpu_time_percent $CPU_TARGET

# Tối ưu vận tốc phản hồi vuốt chạm
resetprop ro.max.fling_velocity $MAX_FLING
resetprop ro.min.fling_velocity $MIN_FLING
resetprop ro.min_pointer_dur 8
resetprop ro.product.multi_touch_enabled 1
resetprop ro.product.max_num_touch 10
resetprop touch.gestureMode default

# 5. Cấu hình bộ nhớ đệm đồ họa HWUI theo dung lượng RAM
if [ "$RAM_GB" -lt 4 ]; then
    resetprop ro.hwui.texture_cache_size 20
    resetprop ro.hwui.layer_cache_size 14
    resetprop ro.hwui.r_buffer_cache_size 6
    resetprop ro.hwui.path_cache_size 10
    resetprop ro.hwui.gradient_cache_size 1
    resetprop ro.hwui.drop_shadow_cache_size 4
    resetprop ro.hwui.shape_cache_size 2
elif [ "$RAM_GB" -lt 7 ]; then
    resetprop ro.hwui.texture_cache_size 36
    resetprop ro.hwui.layer_cache_size 20
    resetprop ro.hwui.r_buffer_cache_size 10
    resetprop ro.hwui.path_cache_size 14
    resetprop ro.hwui.gradient_cache_size 2
    resetprop ro.hwui.drop_shadow_cache_size 6
    resetprop ro.hwui.shape_cache_size 4
else # RAM >= 8GB
    resetprop ro.hwui.texture_cache_size 52
    resetprop ro.hwui.layer_cache_size 28
    resetprop ro.hwui.r_buffer_cache_size 14
    resetprop ro.hwui.path_cache_size 20
    resetprop ro.hwui.gradient_cache_size 2
    resetprop ro.hwui.drop_shadow_cache_size 8
    resetprop ro.hwui.shape_cache_size 4
fi

# Tối ưu hiển thị Font chữ theo độ phân giải màn hình
TEXT_SMALL=512 && TEXT_LARGE=1024
if [ "$WIDTH" -gt 1079 ]; then TEXT_SMALL=1024 && TEXT_LARGE=2048; fi
if [ "$WIDTH" -gt 1439 ]; then TEXT_SMALL=2048 && TEXT_LARGE=4096; fi

resetprop ro.hwui.text_small_cache_width $TEXT_SMALL
resetprop ro.hwui.text_large_cache_width $TEXT_LARGE


# 6. TỐI ƯU TẦNG KERNEL EAS (Bơm máu cho Foreground App)
# Check xem Kernel có hỗ trợ đường dẫn Schedtune của EAS không trước khi ghi
if [ -d "/dev/stune/top-app" ]; then
    chmod +w /dev/stune/top-app/schedtune.boost 2>/dev/null
    chmod +w /dev/stune/top-app/schedtune.prefer_idle 2>/dev/null
    
    echo 1 > /dev/stune/top-app/schedtune.boost
    echo 1 > /dev/stune/top-app/schedtune.prefer_idle
    
    echo "EAS Kernel Tweaks Applied!"
fi

echo "=== ALL TWEAKS OPTIMIZED SUCCESSFULLY ==="