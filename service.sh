#!/system/sh
MODDIR=${0%/*}

# Chờ máy khởi động xong hẳn
while [ "$(getprop sys.boot_completed)" != "1" ]; do sleep 5; done
sleep 10

# Gọi các file script
sh $MODDIR/kernelenhance.sh
sh $MODDIR/touchenhance.sh

echo "Xong: $(date)" > /data/local/tmp/status.log