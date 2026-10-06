# SOME/IP VHAL (ECU Battery <-> Android VHAL)

ECU giả lập (`ecu_battery_simulator`) gửi telemetry APVP/VDDM qua SOME/IP (vsomeip)
tới Android VHAL (`SomeipVehicleHardware`, được ghép với Fake HAL trong `HybridVehicleHardware`).

- Service `0x1234`, Instance `0x5678`
- Event `0x8001` (eventgroup `0x01`): frame APVP = header 12B + 8 property x 16B
- Method `0x0421`: Battery Health; Method `0x0502`: Set Fan Speed
- Prop vendor: `0x21600100` (battery temp), `0x21400101` (fan speed)

## Files
| File | Vai trò |
|---|---|
| `ApvpProtocol.h` | Định nghĩa frame |
| `EcuBatterySimulator.cpp` | Phía ECU |
| `SomeipVehicleHardware.cpp/.h` | Phía Android (IVehicleHardware) |
| `SomeipVhalClientMain.cpp` | Binary test client |
| `vsomeip-ecu.json` / `vsomeip-vhal.json` | Cấu hình 2 node (192.168.97.1 / .2) |
| `../vhal/src/VehicleService.cpp` | `HybridVehicleHardware` (Fake + SOME/IP) |

## Build
```bash
source build/envsetup.sh
lunch <target>          # ví dụ vsoc_x86_64_only
m libvsomeip3 libvsomeip3-cfg libvsomeip3-sd someip_vhal_client ecu_battery_simulator \
  vsomeip-vhal.json vsomeip-ecu.json
# build cả image để VHAL service mới được nạp:
m
```

## Run
```bash
launch_cvd
ip addr | grep 192.168.97      # host phải có 192.168.97.1

# ECU
VSOMEIP_CONFIGURATION=<path>/vsomeip-ecu.json VSOMEIP_APPLICATION_NAME=EcuBatteryService ./ecu_battery_simulator

# Android
adb root
adb logcat -s SomeipVehicleHardware vsomeip
adb shell dumpsys android.hardware.automotive.vehicle.IVehicle/default
```
Log mong đợi: `Service [0x1234.0x5678] is AVAILABLE` và ECU in `[ECU VDDM FRAME #n]`.

## Known issues
- Prop ID bị trùng giữa Fake và SOME/IP trong `getAllPropertyConfigs()` -> có thể lỗi lúc VHAL khởi động.
- `getValues`/`setValues` của Hybrid có thể gọi callback 2 lần với request lẫn prop.
