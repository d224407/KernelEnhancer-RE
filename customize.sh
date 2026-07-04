#!/system/bin/sh

SKIPUNZIP=0

ui_print() { echo "$1"; }

ui_print "- Setting permissions..."
set_perm_recursive $MODPATH 0 0 0755 0644
set_perm $MODPATH/service.sh 0 0 0755
set_perm $MODPATH/action.sh 0 0 0755
set_perm $MODPATH/kernelenhance.sh 0 0 0755

ui_print "- Checking script..."
sh -n $MODPATH/kernelenhance.sh 2>/dev/null || ui_print "⚠️ Warning: Syntax error"

ui_print "✅ Android Kernel & Touch Optimizer installed!"
