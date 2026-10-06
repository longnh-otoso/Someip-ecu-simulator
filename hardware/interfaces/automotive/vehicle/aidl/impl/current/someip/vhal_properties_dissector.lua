-- ===================================================================
-- Wireshark Lua Dissector: APVP / VDDM Protocol
-- Fix triet de loi runtime TvbRange va them Heuristic Dissector
-- ===================================================================

local apvp_proto = Proto("apvp", "Automotive Property Value Protocol (APVP)")

-- 1. Cac truong trong header
local f_msg_id       = ProtoField.uint32("apvp.header.msg_id", "Message ID", base.HEX)
local f_length       = ProtoField.uint32("apvp.header.length", "Length", base.DEC)
local f_req_id       = ProtoField.uint32("apvp.header.req_id", "Request ID", base.HEX)
local f_proto_ver    = ProtoField.uint8("apvp.header.proto_ver", "Protocol Version", base.HEX)
local f_msg_type     = ProtoField.uint8("apvp.header.msg_type", "Message Type", base.HEX)

-- 2. Cac truong VDDM Signal
local f_speed        = ProtoField.float("apvp.vddm.speed", "PERF_VEHICLE_SPEED (VDDM_DATA)", base.NONE)
local f_rpm          = ProtoField.float("apvp.vddm.rpm", "ENGINE_RPM (VDDM_DATA)", base.NONE)
local f_gear         = ProtoField.int32("apvp.vddm.gear", "GEAR_SELECTION (VDDM_DATA)", base.DEC, {
    [1] = "NEUTRAL (1)",
    [2] = "REVERSE (2)",
    [4] = "PARK (4)",
    [8] = "DRIVE (8)"
})
local f_fuel         = ProtoField.float("apvp.vddm.fuel", "FUEL_LEVEL (VDDM_DATA)", base.NONE)
local f_hvac         = ProtoField.float("apvp.vddm.hvac_temp", "HVAC_TEMPERATURE_SET (VDDM_DATA)", base.NONE)
local f_door         = ProtoField.int32("apvp.vddm.door_pos", "DOOR_POS (VDDM_DATA)", base.DEC)
local f_night        = ProtoField.int32("apvp.vddm.night_mode", "NIGHT_MODE (VDDM_DATA)", base.DEC)
local f_batt_temp    = ProtoField.float("apvp.vddm.battery_temp", "VENDOR_BATTERY_TEMP (VDDM_DATA)", base.NONE)

-- Chi tiet tung thuoc tinh
local f_prop_id      = ProtoField.uint32("apvp.vddm.prop_id", "Property ID", base.HEX)
local f_area_id      = ProtoField.uint32("apvp.vddm.area_id", "Area ID", base.HEX)
local f_status       = ProtoField.uint8("apvp.vddm.status", "Status", base.DEC, {
    [0] = "AVAILABLE", [1] = "UNAVAILABLE", [2] = "ERROR"
})
local f_datatype     = ProtoField.uint8("apvp.vddm.data_type", "Data Type", base.DEC)
local f_datalen      = ProtoField.uint16("apvp.vddm.data_len", "Data Length", base.DEC)

apvp_proto.fields = {
    f_msg_id, f_length, f_req_id, f_proto_ver, f_msg_type,
    f_prop_id, f_area_id, f_status, f_datatype, f_datalen,
    f_speed, f_rpm, f_gear, f_fuel, f_hvac, f_door, f_night, f_batt_temp
}

local SIGNALS = {
    [0x11600207] = { name = "PERF_VEHICLE_SPEED",   field = f_speed,     unit = " m/s", is_float = true },
    [0x11600305] = { name = "ENGINE_RPM",           field = f_rpm,       unit = " RPM", is_float = true },
    [0x11400400] = { name = "GEAR_SELECTION",       field = f_gear,      unit = "",     is_float = false },
    [0x11600505] = { name = "FUEL_LEVEL",           field = f_fuel,      unit = " ml",  is_float = true },
    [0x15600503] = { name = "HVAC_TEMPERATURE_SET", field = f_hvac,      unit = " °C",  is_float = true },
    [0x16400B00] = { name = "DOOR_POS",             field = f_door,      unit = "",     is_float = false },
    [0x11200407] = { name = "NIGHT_MODE",           field = f_night,     unit = "",     is_float = false },
    [0x21600100] = { name = "VENDOR_BATTERY_TEMP",  field = f_batt_temp, unit = " °C",  is_float = true }
}

-- Ham Dissector chinh
function apvp_proto.dissector(buffer, pinfo, tree)
    local buf_len = buffer:len()
    if buf_len < 4 then return end

    pinfo.cols.protocol = "APVP"

    -- Tinh toan offset tuyet doi tu Tvb goc (tranh goi tren TvbRange gay crash)
    local header_len = 16
    local vddm_header_offset = 16

    -- Neu goi tin bat dau truc tiep bang VDDM (khong co SOME/IP header)
    if buf_len >= 4 and buffer(0, 4):string() == "VDDM" then
        header_len = 0
        vddm_header_offset = 0
    end

    local apvp_tree = tree:add(apvp_proto, buffer(), string.format("APVP Protocol, %d bytes", buf_len))

    -- 1. Nhanh > header
    if header_len == 16 and buf_len >= 16 then
        local header_tree = apvp_tree:add(buffer(0, 16), "header")
        header_tree:add(f_msg_id, buffer(0, 4))
        header_tree:add(f_length, buffer(4, 4))
        header_tree:add(f_req_id, buffer(8, 4))
        header_tree:add(f_proto_ver, buffer(12, 1))
        header_tree:add(f_msg_type, buffer(14, 1))
    end

    -- 2. Nhanh v payload
    local payload_len = buf_len - header_len
    if payload_len <= 0 then return end

    local payload_tree = apvp_tree:add(buffer(header_len, payload_len), "payload")

    -- 3. Nhanh > VDDM ben trong payload
    local vddm_tree = payload_tree:add(buffer(header_len, payload_len), "VDDM")

    -- Kiem tra Header VDDM (12 bytes)
    local prop_start_offset = vddm_header_offset
    if (buf_len >= vddm_header_offset + 4) and buffer(vddm_header_offset, 4):string() == "VDDM" then
        prop_start_offset = vddm_header_offset + 12
    end

    -- Duyet tung Signal trong VDDM (16 bytes / property)
    local cur = prop_start_offset
    while cur + 16 <= buf_len do
        local prop_id   = buffer(cur, 4):le_uint()
        local area_id   = buffer(cur + 4, 4):le_uint()
        local status    = buffer(cur + 8, 1):uint()
        local data_type = buffer(cur + 9, 1):uint()
        local data_len  = buffer(cur + 10, 2):le_uint()

        local sig = SIGNALS[prop_id]
        local sig_name = sig and sig.name or string.format("Signal_0x%08X", prop_id)

        -- Tao item giong y het anh 1: "SignalName (VDDM_DATA), 4 bytes"
        local sig_label = string.format("%s (VDDM_DATA), 4 bytes", sig_name)
        local sig_tree = vddm_tree:add(buffer(cur, 16), sig_label)

        sig_tree:add_le(f_prop_id, buffer(cur, 4))
        sig_tree:add_le(f_area_id, buffer(cur + 4, 4))
        sig_tree:add(f_status,  buffer(cur + 8, 1))
        sig_tree:add(f_datatype, buffer(cur + 9, 1))
        sig_tree:add_le(f_datalen, buffer(cur + 10, 2))

        if sig then
            if sig.is_float then
                local val = buffer(cur + 12, 4):le_float()
                local val_node = sig_tree:add_le(sig.field, buffer(cur + 12, 4)):append_text(sig.unit)
                if prop_id == 0x11600207 then
                    val_node:append_text(string.format(" (~%.1f km/h)", val * 3.6))
                    sig_tree:append_text(string.format(": %.1f m/s (~%.1f km/h)", val, val * 3.6))
                else
                    sig_tree:append_text(string.format(": %.1f%s", val, sig.unit))
                end
            else
                local val = buffer(cur + 12, 4):le_int()
                sig_tree:add_le(sig.field, buffer(cur + 12, 4))
                sig_tree:append_text(string.format(": %d", val))
            end
        end

        cur = cur + 16
    end

    -- Fallback: goi tin 4 byte tho
    if payload_len == 4 or (payload_len == 20 and header_len == 16) then
        local off = (header_len == 16) and 16 or 0
        local raw_tree = vddm_tree:add(buffer(off, 4), "VENDOR_BATTERY_TEMP (VDDM_DATA), 4 bytes")
        raw_tree:add_le(f_batt_temp, buffer(off, 4)):append_text(" °C")
    end
end

-- 4. Dang ky vao Bang Cong UDP 30509 va SOME/IP
local udp_table = DissectorTable.get("udp.port")
if udp_table then
    udp_table:add(30509, apvp_proto)
    udp_table:add(52135, apvp_proto)
end

local someip_table = DissectorTable.get("someip.message_id")
if someip_table then
    someip_table:add(0x12348001, apvp_proto)
end

-- 5. HEURISTIC DISSECTOR: Tu dong bat moi goi UDP co chua chu "VDDM"
apvp_proto:register_heuristic("udp", function(buffer, pinfo, tree)
    local len = buffer:len()
    if (len >= 20 and buffer(16, 4):string() == "VDDM") or (len >= 4 and buffer(0, 4):string() == "VDDM") then
        apvp_proto.dissector(buffer, pinfo, tree)
        return true
    end
    return false
end)
