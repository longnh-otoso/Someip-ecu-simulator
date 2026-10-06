# SOME/IP VHAL cho AOSP 16 (Cuttlefish Automotive)

Repo này **giữ nguyên cấu trúc đường dẫn AOSP**. Thư mục gốc của repo tương ứng với gốc cây AOSP,
nên có thể copy đè trực tiếp vào cây source.

## Đường dẫn file

### `hardware/interfaces/automotive/vehicle/aidl/impl/`
| Đường dẫn | Loại | Ghi chú |
|---|---|---|
| `current/someip/Android.bp` | mới | Build: lib static, 2 binary, 2 json |
| `current/someip/ApvpProtocol.h` | mới | Định nghĩa frame APVP/VDDM |
| `current/someip/EcuBatterySimulator.cpp` | mới | ECU giả lập (server) |
| `current/someip/SomeipVehicleHardware.cpp/.h` | mới | `IVehicleHardware` qua SOME/IP |
| `current/someip/SomeipVhalClientMain.cpp` | mới | Binary test client |
| `current/someip/vsomeip-ecu.json` | mới | Cấu hình node ECU (192.168.97.1) |
| `current/someip/vsomeip-vhal.json` | mới | Cấu hình node Android (192.168.97.2) |
| `current/someip/vhal_properties_dissector.lua` | mới | Wireshark dissector |
| `current/someip/README.md` | mới | Ghi chú kỹ thuật |
| `current/vhal/Android.bp` | sửa | Link `SomeipVehicleHardware`, `libvsomeip3*` |
| `current/vhal/src/VehicleService.cpp` | sửa | `HybridVehicleHardware` (Fake + SOME/IP) |
| `current/fake_impl/hardware/src/FakeVehicleHardware.cpp` | sửa | |
| `current/default_config/config/DefaultProperties.json` | sửa | Thêm prop vendor |
| `current/utils/test_vendor_properties/.../TestVendorProperty.aidl` | sửa | |
| `3/default_config/config/DefaultProperties.json` | sửa | |
| `3/utils/test_vendor_properties/.../TestVendorProperty.aidl` | sửa | |

### `packages/services/Car/`
| Đường dẫn | Loại |
|---|---|
| `car-lib/src/android/car/custom/CarBatteryManager.java` | mới |
| `car-lib/src/android/car/custom/ICarBattery.aidl` | mới |
| `service/src/com/android/car/CarBatteryService.java` | mới |
| `car-lib/src/android/car/Car.java` | sửa |
| `service/src/com/android/car/ICarImpl.java` | sửa |
| `service/src/com/android/car/CarFeatureController.java` | sửa |
| `service/AndroidManifest.xml` | sửa |
| `service/res/values/strings.xml` | sửa |

## Áp dụng vào AOSP
Yêu cầu: cây AOSP 16 đã có `external/sdv/vsomeip` (module `libvsomeip3`, `libvsomeip3-cfg`, `libvsomeip3-sd`).

```bash
git clone <url-repo-này> someip-vhal
cp -r someip-vhal/hardware someip-vhal/packages /path/to/android-16/
```
Các file "sửa" sẽ ghi đè bản gốc. Nên kiểm tra `git diff` trong từng repo AOSP sau khi copy.

## Build
```bash
cd /path/to/android-16
source build/envsetup.sh
lunch <target>          # ví dụ vsoc_x86_64_only
m libvsomeip3 libvsomeip3-cfg libvsomeip3-sd someip_vhal_client ecu_battery_simulator \
  vsomeip-vhal.json vsomeip-ecu.json
m                       # build image đầy đủ để nạp VHAL mới
```

## Chạy
```bash
launch_cvd
ip addr | grep 192.168.97      # host cần có 192.168.97.1

# ECU (host)
VSOMEIP_CONFIGURATION=<path>/vsomeip-ecu.json VSOMEIP_APPLICATION_NAME=EcuBatteryService ./ecu_battery_simulator

# Android
adb root
adb logcat -s SomeipVehicleHardware vsomeip
```
Log đúng: `Service [0x1234.0x5678] is AVAILABLE` và ECU in `[ECU VDDM FRAME #n]`.

## Giao thức
- Service `0x1234`, Instance `0x5678`
- Event `0x8001` (eventgroup `0x01`): header 12B + 8 property x 16B
- Method `0x0421` Battery Health; `0x0502` Set Fan Speed
- Prop vendor: `0x21600100` (nhiệt độ pin), `0x21400101` (tốc độ quạt)

## Vấn đề đã biết
- Prop ID trùng giữa Fake và SOME/IP trong `getAllPropertyConfigs()` có thể gây lỗi lúc VHAL khởi động.
- `getValues/setValues` của Hybrid có thể gọi callback hai lần khi request lẫn prop.
- `setValues` gửi lệnh quạt mà không kiểm tra ECU đã online.
