#!/system/bin/sh

MODDIR=${0%/*}

# Detect architecture
ARCH=$(getprop ro.product.cpu.abi)
case "$ARCH" in
    arm64-v8a|arm64)
        BIN="$MODDIR/system/bin/kernelenhancer_64"
        ;;
    armeabi-v7a|armeabi)
        BIN="$MODDIR/system/bin/kernelenhancer_32"
        ;;
    x86_64|x86_64-v2|x86_64-v3)
        BIN="$MODDIR/system/bin/kernelenhancer_x86_64"
        ;;
    x86|i686|i586|i486|i386)
        BIN="$MODDIR/system/bin/kernelenhancer_x86"
        ;;
    *)
        # Fallback: try all
        if [ -f "$MODDIR/system/bin/kernelenhancer_64" ]; then
            BIN="$MODDIR/system/bin/kernelenhancer_64"
        elif [ -f "$MODDIR/system/bin/kernelenhancer_x64" ]; then
            BIN="$MODDIR/system/bin/kernelenhancer_x64"
        elif [ -f "$MODDIR/system/bin/kernelenhancer_32" ]; then
            BIN="$MODDIR/system/bin/kernelenhancer_32"
        elif [ -f "$MODDIR/system/bin/kernelenhancer_x86" ]; then
            BIN="$MODDIR/system/bin/kernelenhancer_x86"
        else
            echo "KernelEnhancer: No binary found for architecture $ARCH" > /data/local/tmp/status.log
            exit 1
        fi
        ;;
esac


echo "KernelEnhancer started at $(date) on $ARCH" > /data/local/tmp/status.log