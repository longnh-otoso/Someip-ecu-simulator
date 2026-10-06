#pragma once

#include <cstdint>

// Magic 4 bytes đại diện cho "VDDM"
#define VDDM_MAGIC_HEADER 0x4D444456 // 'V', 'D', 'D', 'M' trong little-endian

#pragma pack(push, 1)

// 1. APVP / VDDM Frame Header (12 bytes)
struct ApvpFrameHeader {
    uint32_t magic;         // 4B: 'V','D','D','M' (0x4D444456)
    uint16_t seqCounter;    // 2B: Số thứ tự frame (0, 1, 2...) để phát hiện mất gói
    uint16_t numOfProps;    // 2B: Số lượng thuộc tính có trong gói
    uint32_t timestamp;     // 4B: Thời gian đồng bộ của xe (ms/uptime)
};

// 2. Kiểu dữ liệu chuẩn VDDM
enum class VddmDataType : uint8_t {
    INT32   = 1,
    FLOAT   = 2,
    BOOLEAN = 3,
};

// 3. Trạng thái tín hiệu chuẩn VDDM
enum class VddmStatus : uint8_t {
    AVAILABLE   = 0,
    UNAVAILABLE = 1,
    ERROR       = 2,
};

// 4. Cấu trúc mỗi thuộc tính VDDM (16 bytes)
struct VddmPropertyEntry {
    uint32_t propId;        // 4B: Mã thuộc tính chuẩn AOSP (vd: 0x11600207)
    uint32_t areaId;        // 4B: Vị trí vùng xe (0 = Global, 1 = Cửa trước lái...)
    uint8_t  status;        // 1B: Trạng thái (0 = AVAILABLE)
    uint8_t  dataType;      // 1B: Kiểu dữ liệu (1=INT32, 2=FLOAT, 3=BOOLEAN)
    uint16_t dataLen;       // 2B: Kích thước dữ liệu value (thường là 4 bytes)
    union {
        float    floatVal;
        int32_t  intVal;
        uint32_t rawVal;
    } value;                // 4B: Giá trị thực tế
};

#pragma pack(pop)
