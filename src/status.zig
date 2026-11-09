// SPDX-License-Identifier: GPL-2.0-only
const std = @import("std");
const A = std.mem.Allocator;
const control = @import("control.zig");
const telemetry = @import("telemetry.zig");
const Temp = struct { chip: []const u8, label: []const u8, celsius: f64 };
const Fan = struct { chip: []const u8, label: []const u8, rpm: i64 };
const Light = struct { id: []const u8, brightness: ?i64, maximum: ?i64 };
const Battery = struct { id: []const u8, percent: ?i64, status: ?[]const u8, charge_start: ?i64, charge_end: ?i64 };
const Vendor = struct {
    control: ?[]const u8,
    mode: ?[]const u8,
    fan_control: ?[]const u8,
    fan_curve: ?[]const u8,
    fan1_percent: ?i64,
    fan2_percent: ?i64,
    keyboard_brightness: ?i64,
    keyboard_rgb: ?[]const u8,
    charging_profile: ?[]const u8,
    last_error: ?i64,
};
const State = struct {
    schema_version: u8 = 1,
    cpu_temperature: ?Temp,
    cpu_power_watts: ?f64 = null,
    gpu: telemetry.Gpu = .{},
    temperatures: []const Temp,
    fans: []const Fan,
    keyboard_lights: []const Light,
    batteries: []const Battery,
    platform_profile: ?[]const u8,
    platform_profile_choices: []const []const u8,
    control_backend: []const u8 = "not-configured",
    vendor: ?Vendor = null,
};
fn read(a: A, dir: std.fs.Dir, path: []const u8) !?[]const u8 {
    const bytes = dir.readFileAlloc(a, path, 4096) catch |err| switch (err) {
        error.OutOfMemory => return err,
        else => return null,
    };
    const text = std.mem.trim(u8, bytes, " \t\r\n");
    if (!std.unicode.utf8ValidateSlice(text)) return null;
    return text;
}
fn num(a: A, dir: std.fs.Dir, path: []const u8) !?i64 {
    const text = try read(a, dir, path) orelse return null;
    return std.fmt.parseInt(i64, text, 10) catch null;
}
// All allocations belong to one request arena, reclaimed after JSON is sent.
pub fn snapshot(a: A, root: []const u8) ![]const u8 {
    var base = try std.fs.cwd().openDir(root, .{});
    defer base.close();
    var temps = std.ArrayList(Temp).init(a);
    var fans = std.ArrayList(Fan).init(a);
    var lights = std.ArrayList(Light).init(a);
    var batteries = std.ArrayList(Battery).init(a);
    var cpu: ?Temp = null;
    if (base.openDir("class/hwmon", .{ .iterate = true })) |opened| {
        var dir = opened;
        defer dir.close();
        var it = dir.iterate();
        while (try it.next()) |entry| {
            var hw = dir.openDir(entry.name, .{ .iterate = true }) catch continue;
            defer hw.close();
            const chip = try read(a, hw, "name") orelse try a.dupe(u8, entry.name);
            var sensors = hw.iterate();
            while (try sensors.next()) |sensor| {
                const is_temp = std.mem.startsWith(u8, sensor.name, "temp");
                if ((!is_temp and !std.mem.startsWith(u8, sensor.name, "fan")) or !std.mem.endsWith(u8, sensor.name, "_input")) continue;
                const value = try num(a, hw, sensor.name) orelse continue;
                const label_path = try std.fmt.allocPrint(a, "{s}_label", .{sensor.name[0 .. sensor.name.len - 6]});
                const label = try read(a, hw, label_path) orelse try a.dupe(u8, sensor.name);
                if (is_temp) {
                    const t = Temp{ .chip = chip, .label = label, .celsius = @as(f64, @floatFromInt(value)) / 1000.0 };
                    try temps.append(t);
                    if (std.mem.eql(u8, chip, "k10temp") and std.mem.eql(u8, label, "Tctl")) cpu = t;
                } else try fans.append(.{ .chip = chip, .label = label, .rpm = value });
            }
        }
    } else |_| {}
    if (base.openDir("class/leds", .{ .iterate = true })) |opened| {
        var dir = opened;
        defer dir.close();
        var it = dir.iterate();
        while (try it.next()) |entry| {
            if (std.mem.indexOf(u8, entry.name, "kbd_backlight") == null) continue;
            var led = dir.openDir(entry.name, .{}) catch continue;
            defer led.close();
            try lights.append(.{ .id = try a.dupe(u8, entry.name), .brightness = try num(a, led, "brightness"), .maximum = try num(a, led, "max_brightness") });
        }
    } else |_| {}
    if (base.openDir("class/power_supply", .{ .iterate = true })) |opened| {
        var dir = opened;
        defer dir.close();
        var it = dir.iterate();
        while (try it.next()) |entry| {
            var bat = dir.openDir(entry.name, .{}) catch continue;
            defer bat.close();
            const kind = try read(a, bat, "type") orelse continue;
            if (!std.mem.eql(u8, kind, "Battery")) continue;
            try batteries.append(.{ .id = try a.dupe(u8, entry.name), .percent = try num(a, bat, "capacity"), .status = try read(a, bat, "status"), .charge_start = try num(a, bat, "charge_control_start_threshold"), .charge_end = try num(a, bat, "charge_control_end_threshold") });
        }
    } else |_| {}
    var choices = std.ArrayList([]const u8).init(a);
    var words = std.mem.tokenizeAny(u8, try read(a, base, "firmware/acpi/platform_profile_choices") orelse "", " \t\n");
    while (words.next()) |word| try choices.append(word);
    var vendor: ?Vendor = null;
    if (try control.find(a, root)) |path| {
        var hw = try std.fs.cwd().openDir(path, .{});
        defer hw.close();
        vendor = .{
            .control = try read(a, hw, "control"),
            .mode = try read(a, hw, "mode"),
            .fan_control = try read(a, hw, "fan_control"),
            .fan_curve = try read(a, hw, "fan_curve"),
            .fan1_percent = try num(a, hw, "fan1_percent"),
            .fan2_percent = try num(a, hw, "fan2_percent"),
            .keyboard_brightness = try num(a, hw, "keyboard_brightness"),
            .keyboard_rgb = try read(a, hw, "keyboard_rgb"),
            .charging_profile = try read(a, hw, "charging_profile"),
            .last_error = try num(a, hw, "last_error"),
        };
    }
    return std.json.stringifyAlloc(a, State{ .cpu_power_watts = if (std.mem.eql(u8, root, "/sys")) telemetry.cpuPower(a) else null, .gpu = if (std.mem.eql(u8, root, "/sys")) telemetry.gpu(a) else .{}, .vendor = vendor, .control_backend = if (vendor != null) "mechrevo-sysfs-v1" else "not-configured", .cpu_temperature = cpu, .temperatures = temps.items, .fans = fans.items, .keyboard_lights = lights.items, .batteries = batteries.items, .platform_profile = try read(a, base, "firmware/acpi/platform_profile"), .platform_profile_choices = choices.items }, .{});
}

test "missing interfaces stay null and empty" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const json = try snapshot(a, try tmp.dir.realpathAlloc(a, "."));
    const parsed = try std.json.parseFromSlice(std.json.Value, a, json, .{});
    try std.testing.expect(parsed.value.object.get("cpu_temperature").? == .null);
    try std.testing.expectEqual(@as(usize, 0), parsed.value.object.get("fans").?.array.items.len);
}

test "invalid sensor, stopped fan, keyboard filtering and missing threshold" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const entries = .{
        .{ "class/hwmon/hwmon0/name", "k10temp" },
        .{ "class/hwmon/hwmon0/temp1_label", "Tctl" },
        .{ "class/hwmon/hwmon0/temp1_input", "53800" },
        .{ "class/hwmon/hwmon0/temp2_input", "bad" },
        .{ "class/hwmon/hwmon0/fan1_input", "0" },
        .{ "class/leds/input0::capslock/brightness", "1" },
        .{ "class/leds/vendor::kbd_backlight/brightness", "2" },
        .{ "class/power_supply/BAT0/type", "Battery" },
    };
    inline for (entries) |pair| {
        try tmp.dir.makePath(std.fs.path.dirname(pair[0]).?);
        try tmp.dir.writeFile(.{ .sub_path = pair[0], .data = pair[1] });
    }
    const parsed = try std.json.parseFromSlice(std.json.Value, a, try snapshot(a, try tmp.dir.realpathAlloc(a, ".")), .{});
    const obj = parsed.value.object;
    try std.testing.expectApproxEqAbs(@as(f64, 53.8), obj.get("cpu_temperature").?.object.get("celsius").?.float, 0.001);
    try std.testing.expectEqual(@as(usize, 1), obj.get("temperatures").?.array.items.len);
    try std.testing.expectEqual(@as(i64, 0), obj.get("fans").?.array.items[0].object.get("rpm").?.integer);
    try std.testing.expectEqual(@as(usize, 1), obj.get("keyboard_lights").?.array.items.len);
    try std.testing.expect(obj.get("batteries").?.array.items[0].object.get("charge_end").? == .null);
}

test "vendor discovery preserves actual settings and control state" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const entries = .{
        .{ "class/hwmon/hwmon9/name", "mechrevo\n" },
        .{ "class/hwmon/hwmon9/control", "firmware\n" },
        .{ "class/hwmon/hwmon9/mode", "turbo\n" },
        .{ "class/hwmon/hwmon9/fan1_input", "1980" },
        .{ "class/hwmon/hwmon9/keyboard_brightness", "4" },
        .{ "class/hwmon/hwmon9/charging_profile", "stationary\n" },
    };
    inline for (entries) |pair| {
        try tmp.dir.makePath(std.fs.path.dirname(pair[0]).?);
        try tmp.dir.writeFile(.{ .sub_path = pair[0], .data = pair[1] });
    }
    const parsed = try std.json.parseFromSlice(std.json.Value, a, try snapshot(a, try tmp.dir.realpathAlloc(a, ".")), .{});
    const obj = parsed.value.object;
    try std.testing.expectEqualStrings("mechrevo-sysfs-v1", obj.get("control_backend").?.string);
    const vendor = obj.get("vendor").?.object;
    try std.testing.expectEqualStrings("firmware", vendor.get("control").?.string);
    try std.testing.expectEqualStrings("turbo", vendor.get("mode").?.string);
    try std.testing.expectEqualStrings("stationary", vendor.get("charging_profile").?.string);
    try std.testing.expectEqual(@as(i64, 1980), obj.get("fans").?.array.items[0].object.get("rpm").?.integer);
}
