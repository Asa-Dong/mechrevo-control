// SPDX-License-Identifier: GPL-2.0-only
const std = @import("std");
const A = std.mem.Allocator;

// Discover only the fixed kernel driver; caller input never becomes a path.
pub fn find(a: A, root: []const u8) !?[]const u8 {
    const parent = try std.fs.path.join(a, &.{ root, "class/hwmon" });
    var dir = std.fs.cwd().openDir(parent, .{ .iterate = true }) catch return null;
    defer dir.close();
    var it = dir.iterate();
    while (try it.next()) |entry| {
        var hw = dir.openDir(entry.name, .{}) catch continue;
        defer hw.close();
        const name = hw.readFileAlloc(a, "name", 64) catch continue;
        if (std.mem.eql(u8, std.mem.trim(u8, name, "\n\r "), "mechrevo"))
            return try std.fs.path.join(a, &.{ parent, entry.name });
    }
    return null;
}
pub fn valid(key: []const u8, value: []const u8) bool {
    if (std.mem.eql(u8, key, "control")) return std.mem.eql(u8, value, "release");
    if (std.mem.eql(u8, key, "mode")) return oneOf(value, &.{ "office", "balanced", "turbo" });
    if (std.mem.eql(u8, key, "charging_profile")) return oneOf(value, &.{ "high_capacity", "balanced", "stationary" });
    if (std.mem.eql(u8, key, "fan_control")) return oneOf(value, &.{ "auto", "manual", "curve", "boost" });
    if (std.mem.eql(u8, key, "fan1_percent") or std.mem.eql(u8, key, "fan2_percent")) {
        if (value.len == 0 or value.len > 3) return false;
        for (value) |ch| if (ch < '0' or ch > '9') return false;
        const n = std.fmt.parseInt(u8, value, 10) catch return false;
        return n <= 100;
    }
    if (std.mem.eql(u8, key, "fan_curve")) {
        var it = std.mem.tokenizeScalar(u8, value, ' ');
        var count: usize = 0;
        var previous: u8 = 0;
        while (it.next()) |word| {
            for (word) |ch| if (ch < '0' or ch > '9') return false;
            const n = std.fmt.parseInt(u8, word, 10) catch return false;
            if (n > 100 or (count % 5 != 0 and n < previous)) return false;
            previous = n;
            count += 1;
        }
        return count == 10;
    }
    if (std.mem.eql(u8, key, "keyboard_brightness")) return value.len == 1 and value[0] >= '0' and value[0] <= '4';
    if (std.mem.eql(u8, key, "keyboard_rgb")) {
        var it = std.mem.tokenizeScalar(u8, value, ' ');
        var count: usize = 0;
        while (it.next()) |word| {
            for (word) |ch| if (ch < '0' or ch > '9') return false;
            const n = std.fmt.parseInt(u8, word, 10) catch return false;
            if (n > 50) return false;
            count += 1;
        }
        return count == 3;
    }
    return false;
}
fn oneOf(v: []const u8, choices: []const []const u8) bool {
    for (choices) |choice| if (std.mem.eql(u8, v, choice)) return true;
    return false;
}
pub fn write(dir: std.fs.Dir, key: []const u8, value: []const u8) !void {
    const file = try dir.openFile(key, .{ .mode = .write_only });
    defer file.close();
    try file.writeAll(value);
}
pub const defaults = [_]struct { key: []const u8, value: []const u8 }{
    .{ .key = "fan_control", .value = "auto" },
    .{ .key = "mode", .value = "office" },
    .{ .key = "keyboard_brightness", .value = "1" },
    .{ .key = "keyboard_rgb", .value = "50 50 50" },
    .{ .key = "charging_profile", .value = "high_capacity" },
};
pub fn apply(a: A, key: []const u8, value: []const u8) !void {
    if (!valid(key, value)) return error.InvalidSetting;
    const path = try find(a, "/sys") orelse return error.DriverUnavailable;
    var dir = try std.fs.cwd().openDir(path, .{});
    defer dir.close();
    if (std.mem.eql(u8, key, "control")) {
        // Release clears any previous failed control session before applying the preset.
        try write(dir, "control", "release");
        try write(dir, "control", "acquire");
        for (defaults) |setting| try write(dir, setting.key, setting.value);
    } else {
        try write(dir, "control", "acquire");
        try write(dir, key, value);
    }
}
pub fn release(a: A) !void {
    const path = try find(a, "/sys") orelse return error.DriverUnavailable;
    var dir = try std.fs.cwd().openDir(path, .{});
    defer dir.close();
    try write(dir, "control", "release");
}
test "control inputs cannot address arbitrary files or unverified modes" {
    try std.testing.expect(!valid("../control", "acquire"));
    try std.testing.expect(!valid("control", "acquire"));
    try std.testing.expect(!valid("fan_boost", "1"));
    try std.testing.expect(valid("charging_profile", "balanced"));
    try std.testing.expect(!valid("keyboard_brightness", "5"));
    try std.testing.expect(!valid("keyboard_rgb", "50 0 0 extra"));
    try std.testing.expect(valid("keyboard_rgb", "0 0 0"));
    try std.testing.expect(valid("keyboard_rgb", "0 50 50"));
    try std.testing.expect(valid("mode", "turbo"));
}

test "manual fan percent bounds and separate channel names" {
    try std.testing.expect(valid("fan1_percent", "30"));
    try std.testing.expect(valid("fan2_percent", "100"));
    try std.testing.expect(valid("fan1_percent", "0"));
    try std.testing.expect(!valid("fan2_percent", "101"));
    try std.testing.expect(!valid("fan3_percent", "50"));
    try std.testing.expect(!valid("fan2_percent", "-50"));
}

test "firmware curve rejects nonmonotonic, short and unbounded tables" {
    try std.testing.expect(valid("fan_curve", "25 25 25 80 100 25 25 25 80 100"));
    try std.testing.expect(valid("fan_curve", "0 0 0 80 100 0 0 0 80 100"));
    try std.testing.expect(!valid("fan_curve", "-1 0 0 80 100 0 0 0 80 100"));
    try std.testing.expect(valid("fan_curve", "30 40 60 80 100 30 40 60 80 100"));
    try std.testing.expect(!valid("fan_curve", "30 40 60 80 100"));
    try std.testing.expect(valid("fan_curve", "30 40 60 80 90 30 40 60 80 100"));
    try std.testing.expect(!valid("fan_curve", "40 30 60 80 100 30 40 60 80 100"));
}

test "curve accepts zero and non-full-speed last bins" {
    try std.testing.expect(valid("fan_curve", "0 0 0 0 0 10 10 10 10 10"));
    try std.testing.expect(!valid("fan_curve", "0 0 0 0 101 10 10 10 10 10"));
}

test "fixed defaults are valid and independent of the acquisition snapshot" {
    try std.testing.expectEqual(@as(usize, 5), defaults.len);
    for (defaults) |setting| try std.testing.expect(valid(setting.key, setting.value));
    try std.testing.expectEqualStrings("auto", defaults[0].value);
    try std.testing.expectEqualStrings("office", defaults[1].value);
    try std.testing.expectEqualStrings("1", defaults[2].value);
    try std.testing.expectEqualStrings("50 50 50", defaults[3].value);
    try std.testing.expectEqualStrings("high_capacity", defaults[4].value);
}
