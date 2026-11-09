// SPDX-License-Identifier: GPL-2.0-only
const std = @import("std");
const control = @import("control.zig");
const A = std.mem.Allocator;
pub const directory = "/var/lib/mechrevo-control";
const filename = "preferences.json";
pub const Fan = struct {
    mode: []const u8,
    percent1: ?[]const u8 = null,
    percent2: ?[]const u8 = null,
    curve: ?[]const u8 = null,
};
pub const Preferences = struct {
    version: u8 = 1,
    keyboard_rgb: ?[]const u8 = null,
    charging_profile: ?[]const u8 = null,
    fan: ?Fan = null,
};
pub const Setting = struct { key: []const u8, value: []const u8 };

pub fn validate(p: Preferences) !void {
    if (p.version != 1) return error.InvalidPreferences;
    if (p.keyboard_rgb) |v| if (!control.valid("keyboard_rgb", v)) return error.InvalidPreferences;
    if (p.charging_profile) |v| if (!control.valid("charging_profile", v)) return error.InvalidPreferences;
    if (p.fan) |f| {
        if (!control.valid("fan_control", f.mode)) return error.InvalidPreferences;
        if (std.mem.eql(u8, f.mode, "manual")) {
            if (!control.valid("fan1_percent", f.percent1 orelse return error.InvalidPreferences) or
                !control.valid("fan2_percent", f.percent2 orelse return error.InvalidPreferences)) return error.InvalidPreferences;
        } else if (std.mem.eql(u8, f.mode, "curve")) {
            if (!control.valid("fan_curve", f.curve orelse return error.InvalidPreferences)) return error.InvalidPreferences;
        }
    }
}
pub fn load(a: A, dir: std.fs.Dir) !Preferences {
    const bytes = dir.readFileAlloc(a, filename, 4096) catch |err| switch (err) {
        error.FileNotFound => return .{},
        else => return err,
    };
    const parsed = try std.json.parseFromSlice(Preferences, a, bytes, .{ .allocate = .alloc_always });
    try validate(parsed.value);
    return parsed.value; // Caller owns the request/startup arena.
}
pub fn save(a: A, dir: std.fs.Dir, p: Preferences) !void {
    try validate(p);
    const json = try std.json.stringifyAlloc(a, p, .{ .whitespace = .indent_2 });
    var file = try dir.createFile("preferences.tmp", .{ .mode = 0o600 });
    defer file.close();
    errdefer dir.deleteFile("preferences.tmp") catch {};
    try file.writeAll(json);
    try file.sync();
    try dir.rename("preferences.tmp", filename);
    // Non-iterable Zig directory handles use O_PATH and cannot be fsynced.
    var readable = try dir.openDir(".", .{ .iterate = true });
    defer readable.close();
    try std.posix.fsync(readable.fd);
}
fn read(a: A, hw: std.fs.Dir, key: []const u8) ![]const u8 {
    return std.mem.trim(u8, try hw.readFileAlloc(a, key, 256), " \r\n");
}
pub fn captureFan(a: A, hw: std.fs.Dir) !Fan {
    var fan = Fan{ .mode = try read(a, hw, "fan_control") };
    if (std.mem.eql(u8, fan.mode, "manual")) {
        fan.percent1 = try read(a, hw, "fan1_percent");
        fan.percent2 = try read(a, hw, "fan2_percent");
    } else if (std.mem.eql(u8, fan.mode, "curve")) fan.curve = try read(a, hw, "fan_curve");
    try validate(.{ .fan = fan });
    return fan;
}
// Save only after a successful authorized write, using the driver's actual result.
pub fn remember(a: A, key: []const u8) !void {
    if (!std.mem.eql(u8, key, "keyboard_rgb") and !std.mem.eql(u8, key, "charging_profile") and
        !std.mem.startsWith(u8, key, "fan") and !std.mem.eql(u8, key, "mode") and !std.mem.eql(u8, key, "control")) return;
    var dir = try std.fs.openDirAbsolute(directory, .{});
    defer dir.close();
    const resetting = std.mem.eql(u8, key, "control");
    var p = if (resetting) Preferences{} else try load(a, dir);
    if (std.mem.eql(u8, key, "mode") and p.fan == null) return;
    const path = try control.find(a, "/sys") orelse return error.DriverUnavailable;
    var hw = try std.fs.cwd().openDir(path, .{});
    defer hw.close();
    if (resetting) {
        p.keyboard_rgb = try read(a, hw, "keyboard_rgb");
        p.charging_profile = try read(a, hw, "charging_profile");
        p.fan = try captureFan(a, hw);
    } else if (std.mem.eql(u8, key, "keyboard_rgb")) p.keyboard_rgb = try read(a, hw, key) else if (std.mem.eql(u8, key, "charging_profile")) p.charging_profile = try read(a, hw, key) else p.fan = try captureFan(a, hw);
    try save(a, dir, p);
}
// A validated replay plan makes it impossible to restore brightness/performance.
pub fn plan(a: A, p: Preferences) ![]Setting {
    try validate(p);
    var settings = std.ArrayList(Setting).init(a);
    if (p.keyboard_rgb) |v| try settings.append(.{ .key = "keyboard_rgb", .value = v });
    if (p.charging_profile) |v| try settings.append(.{ .key = "charging_profile", .value = v });
    if (p.fan) |f| {
        if (std.mem.eql(u8, f.mode, "manual")) {
            try settings.append(.{ .key = "fan1_percent", .value = f.percent1.? });
            try settings.append(.{ .key = "fan2_percent", .value = f.percent2.? });
        } else if (std.mem.eql(u8, f.mode, "curve")) {
            try settings.append(.{ .key = "fan_curve", .value = f.curve.? });
        } else try settings.append(.{ .key = "fan_control", .value = f.mode });
    }
    return settings.toOwnedSlice();
}
pub fn restore(a: A) !void {
    var dir = std.fs.openDirAbsolute(directory, .{}) catch |err| switch (err) {
        error.FileNotFound => return,
        else => return err,
    };
    defer dir.close();
    const p = load(a, dir) catch |err| {
        // Keep D-Bus available so an explicit reset can clear a corrupt file.
        std.log.err("Cannot load saved preferences: {s}; no settings restored", .{@errorName(err)});
        return;
    };
    for (try plan(a, p)) |setting| try control.apply(a, setting.key, setting.value);
}

test "atomic save, reopen, validation and explicit reset" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    try std.testing.expect((try load(a, tmp.dir)).fan == null);
    const p = Preferences{ .keyboard_rgb = "50 50 50", .charging_profile = "stationary", .fan = .{ .mode = "manual", .percent1 = "25", .percent2 = "35" } };
    try save(a, tmp.dir, p);
    var reopened = try tmp.dir.openDir(".", .{});
    defer reopened.close();
    const steps = try plan(a, try load(a, reopened));
    try std.testing.expectEqual(@as(usize, 4), steps.len);
    try std.testing.expectEqualStrings("fan1_percent", steps[2].key);
    try std.testing.expectEqualStrings("35", steps[3].value);
    try std.testing.expectError(error.InvalidPreferences, save(a, tmp.dir, .{ .fan = .{ .mode = "manual", .percent1 = "101", .percent2 = "25" } }));
    try std.testing.expectEqualStrings("stationary", (try load(a, reopened)).charging_profile.?);
    try save(a, tmp.dir, .{});
    try std.testing.expectEqual(@as(usize, 0), (try plan(a, try load(a, reopened))).len);
    try tmp.dir.writeFile(.{ .sub_path = filename, .data = "{broken" });
    try std.testing.expectError(error.SyntaxError, load(a, reopened));
}
test "curve replay and invalid or incomplete preferences" {
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const steps = try plan(arena.allocator(), .{ .fan = .{ .mode = "curve", .curve = "30 40 60 80 100 35 45 65 85 100" } });
    try std.testing.expectEqual(@as(usize, 1), steps.len);
    try std.testing.expectEqualStrings("fan_curve", steps[0].key);
    try std.testing.expectError(error.InvalidPreferences, validate(.{ .fan = .{ .mode = "manual" } }));
    try std.testing.expectError(error.InvalidPreferences, validate(.{ .fan = .{ .mode = "curve", .curve = "90 40 60 80 100 30 40 60 80 100" } }));
    try std.testing.expectError(error.InvalidPreferences, validate(.{ .keyboard_rgb = "51 0 0" }));
    try std.testing.expectError(error.InvalidPreferences, validate(.{ .version = 2 }));
}

test "readback captures fan parameters and automatic mode drops stale curve" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    try tmp.dir.writeFile(.{ .sub_path = "fan_control", .data = "manual\n" });
    try tmp.dir.writeFile(.{ .sub_path = "fan1_percent", .data = "25\n" });
    try tmp.dir.writeFile(.{ .sub_path = "fan2_percent", .data = "40\n" });
    const manual = try captureFan(a, tmp.dir);
    try std.testing.expectEqualStrings("25", manual.percent1.?);
    try std.testing.expectEqualStrings("40", manual.percent2.?);
    try tmp.dir.writeFile(.{ .sub_path = "fan_control", .data = "auto\n" });
    const automatic = try captureFan(a, tmp.dir);
    try std.testing.expect(automatic.curve == null and automatic.percent1 == null);
    const steps = try plan(a, .{ .fan = automatic });
    try std.testing.expectEqual(@as(usize, 1), steps.len);
    try std.testing.expectEqualStrings("auto", steps[0].value);
}
