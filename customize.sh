# Đặt biến môi trường cho Magisk
SKIPUNZIP=0

# Cấp quyền
ui_print "- Đang cấp quyền..."
set_perm_recursive $MODPATH 0 0 0755 0644

# Kiểm tra syntax file (chỉ in lỗi, không abort nếu không cần thiết)
ui_print "- Kiểm tra tính toàn vẹn..."
if ! sh -n $MODPATH/kernelenhance.sh; then
  ui_print "! Cảnh báo: Lỗi cú pháp trong kernelenhance.sh"
fi

# Chạy script (thêm dấu & hoặc bỏ qua lỗi để không làm treo tiến trình cài đặt)
ui_print "- Thực thi tối ưu hóa..."
sh $MODPATH/kernelenhance.sh > /dev/null 2>&1
sh $MODPATH/touchenhance.sh > /dev/null 2>&1

ui_print "- Hoàn tất cài đặt thành công!"
exit 0