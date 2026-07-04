#!/system/bin/sh

MODDIR=/data/adb/modules/$(basename $(dirname $0))

sh $MODDIR/kernelenhance.sh
echo "Manual optimize completed at $(date)" >> /data/local/tmp/status.log
ui_print "✅ Kernel & Touch Optimized!"
