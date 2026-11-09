// SPDX-License-Identifier: GPL-2.0-only
const std = @import("std");
const A = std.mem.Allocator;
pub const Gpu = struct { state: enum { unavailable, suspended, transitioning, active } = .unavailable, celsius: ?f64 = null, watts: ?f64 = null };
const Sample = struct { energy: u64, range: u64, time: std.time.Instant };
var previous: ?Sample = null;
var last_watts: ?f64 = null;

fn number(a: A, dir: std.fs.Dir, file: []const u8) ?u64 {
    const text = dir.readFileAlloc(a, file, 128) catch return null;
    return std.fmt.parseInt(u64, std.mem.trim(u8, text, " \r\n"), 10) catch null;
}
pub fn averageWatts(before: u64, after: u64, range: u64, nanos: u64) ?f64 {
    if (range == 0 or before >= range or after >= range or nanos == 0 or nanos > 30 * std.time.ns_per_s) return null;
    const delta = if (after >= before) after - before else range - before + after;
    const watts = @as(f64, @floatFromInt(delta)) * 1000 / @as(f64, @floatFromInt(nanos));
    return if (watts <= 1000) watts else null;
}
pub fn cpuPower(a: A) ?f64 {
    // This machine exposes AMD package energy via the Linux RAPL powercap ABI.
    var dir = std.fs.openDirAbsolute("/sys/class/powercap/intel-rapl:0", .{}) catch {
        previous = null;
        last_watts = null;
        return null;
    };
    defer dir.close();
    const name = dir.readFileAlloc(a, "name", 64) catch return null;
    if (!std.mem.eql(u8, std.mem.trim(u8, name, "\n"), "package-0")) return null;
    const energy = number(a, dir, "energy_uj") orelse {
        previous = null;
        last_watts = null;
        return null;
    };
    const range = number(a, dir, "max_energy_range_uj") orelse return null;
    const now = std.time.Instant.now() catch return null;
    if (previous) |old| {
        const elapsed = now.since(old.time);
        if (elapsed < 200 * std.time.ns_per_ms) return last_watts;
        last_watts = if (range == old.range) averageWatts(old.energy, energy, range, elapsed) else null;
    } else last_watts = null;
    previous = .{ .energy = energy, .range = range, .time = now };
    return last_watts;
}

pub fn gpu(a: A) Gpu {
    return gpuAt(a, "/sys/bus/pci/devices");
}
fn gpuAt(a: A, pci_path: []const u8) Gpu {
    // Check PCI runtime state before NVML, so polling does not wake a suspended GPU.
    var pci = std.fs.openDirAbsolute(pci_path, .{ .iterate = true }) catch return .{};
    defer pci.close();
    var it = pci.iterate();
    while (it.next() catch return .{}) |entry| {
        var dev = pci.openDir(entry.name, .{}) catch continue;
        defer dev.close();
        const vendor = dev.readFileAlloc(a, "vendor", 32) catch continue;
        if (!std.mem.eql(u8, std.mem.trim(u8, vendor, "\n"), "0x10de")) continue;
        const class = dev.readFileAlloc(a, "class", 32) catch continue;
        if (!std.mem.startsWith(u8, class, "0x03")) continue;
        const runtime = dev.readFileAlloc(a, "power/runtime_status", 32) catch return .{};
        const state = std.mem.trim(u8, runtime, "\n");
        if (std.mem.eql(u8, state, "suspended")) return .{ .state = .suspended };
        if (!std.mem.eql(u8, state, "active")) return .{ .state = .transitioning };
        const bus = a.dupeZ(u8, entry.name) catch return .{};
        return queryNvml(bus);
    }
    return .{};
}
const Nvml = struct {
    const Init = *const fn () callconv(.c) c_uint;
    const Handle = *const fn ([*:0]const u8, *?*anyopaque) callconv(.c) c_uint;
    const Temp = *const fn (?*anyopaque, c_uint, *c_uint) callconv(.c) c_uint;
    const Power = *const fn (?*anyopaque, *c_uint) callconv(.c) c_uint;

    lib: ?std.DynLib,
    shutdown: Init,
    get_temp: Temp,
    get_power: Power,
    device: ?*anyopaque,
    bus: [32]u8,
    bus_len: usize,

    fn open(bus: [:0]const u8) !Nvml {
        if (bus.len > 32) return error.InvalidBus;
        var lib = try std.DynLib.open("libnvidia-ml.so.1");
        errdefer lib.close();
        const init = lib.lookup(Init, "nvmlInit_v2") orelse return error.MissingSymbol;
        const shutdown = lib.lookup(Init, "nvmlShutdown") orelse return error.MissingSymbol;
        const get_handle = lib.lookup(Handle, "nvmlDeviceGetHandleByPciBusId_v2") orelse return error.MissingSymbol;
        const get_temp = lib.lookup(Temp, "nvmlDeviceGetTemperature") orelse return error.MissingSymbol;
        const get_power = lib.lookup(Power, "nvmlDeviceGetPowerUsage") orelse return error.MissingSymbol;
        if (init() != 0) return error.NvmlInit;
        errdefer _ = shutdown();
        var device: ?*anyopaque = null;
        if (get_handle(bus.ptr, &device) != 0 or device == null) return error.NvmlDevice;
        var result = Nvml{ .lib = lib, .shutdown = shutdown, .get_temp = get_temp, .get_power = get_power, .device = device, .bus = undefined, .bus_len = bus.len };
        @memcpy(result.bus[0..bus.len], bus);
        return result;
    }

    fn close(self: *Nvml) void {
        _ = self.shutdown();
        if (self.lib) |*lib| lib.close();
    }
};

// Keep the NVML session and device handle across samples AND runtime suspend.
// Repeated init/handle lookup/shutdown kept this laptop awake in hardware tests.
// No NVML calls are made while sysfs reports suspended or transitioning.
var nvml: ?Nvml = null;
var nvml_retry_after: ?std.time.Instant = null;

pub fn deinit() void {
    if (nvml) |*session| session.close();
    nvml = null;
    nvml_retry_after = null;
}

fn queryNvml(bus: [:0]const u8) Gpu {
    if (nvml) |*session| {
        if (!std.mem.eql(u8, session.bus[0..session.bus_len], bus)) deinit();
    }
    if (nvml == null) {
        const now = std.time.Instant.now() catch return .{ .state = .active };
        if (nvml_retry_after) |last_failure| {
            if (now.since(last_failure) < 30 * std.time.ns_per_s) return .{ .state = .active };
        }
        nvml = Nvml.open(bus) catch {
            nvml_retry_after = now;
            return .{ .state = .active };
        };
        nvml_retry_after = null;
    }
    const session = &nvml.?;
    var t: c_uint = 0;
    var p: c_uint = 0;
    const tr = session.get_temp(session.device, 0, &t);
    const pr = session.get_power(session.device, &p);
    var result = Gpu{ .state = .active };
    if (tr == 0 and t <= 150) result.celsius = @floatFromInt(t);
    if (pr == 0 and p <= 1000000) result.watts = @as(f64, @floatFromInt(p)) / 1000;
    // Reconnect after a lost GPU/driver or invalid session, with a cooldown.
    // Unsupported individual metrics do not cause repeated initialization.
    for ([_]c_uint{ tr, pr }) |rc| {
        if (rc == 1 or rc == 9 or rc == 15 or rc == 999) {
            deinit();
            nvml_retry_after = std.time.Instant.now() catch null;
            return .{ .state = .active };
        }
    }
    return result;
}
test "energy delta units, wraparound and invalid sampling intervals" {
    try std.testing.expectEqual(@as(?f64, 20), averageWatts(1000000, 21000000, 100000000, std.time.ns_per_s));
    try std.testing.expectEqual(@as(?f64, 10), averageWatts(95000000, 5000000, 100000000, std.time.ns_per_s));
    try std.testing.expect(averageWatts(0, 1, 100, 0) == null);
    try std.testing.expect(averageWatts(0, 1, 100, 31 * std.time.ns_per_s) == null);
    try std.testing.expect(averageWatts(100, 1, 100, std.time.ns_per_s) == null);
}

test "GPU discovery distinguishes absent, sleeping and transitioning devices without waking them" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const root = try tmp.dir.realpathAlloc(a, ".");
    try std.testing.expectEqual(.unavailable, gpuAt(a, root).state);
    try tmp.dir.makePath("0000:01:00.0/power");
    try tmp.dir.writeFile(.{ .sub_path = "0000:01:00.0/vendor", .data = "0x10de\n" });
    try tmp.dir.writeFile(.{ .sub_path = "0000:01:00.0/class", .data = "0x030000\n" });
    try tmp.dir.writeFile(.{ .sub_path = "0000:01:00.0/power/runtime_status", .data = "suspended\n" });
    const sleeping = gpuAt(a, root);
    try std.testing.expectEqual(.suspended, sleeping.state);
    try std.testing.expect(sleeping.celsius == null and sleeping.watts == null);
    try tmp.dir.writeFile(.{ .sub_path = "0000:01:00.0/power/runtime_status", .data = "suspending\n" });
    try std.testing.expectEqual(.transitioning, gpuAt(a, root).state);
    try tmp.dir.writeFile(.{ .sub_path = "0000:01:00.0/class", .data = "0x040300\n" });
    try std.testing.expectEqual(.unavailable, gpuAt(a, root).state);
}

test "NVML session survives samples and suspend, closes once, and backs off on GPU loss" {
    const Fake = struct {
        var reads: usize = 0;
        var closes: usize = 0;
        var power_error: c_uint = 0;
        fn shutdown() callconv(.c) c_uint {
            closes += 1;
            return 0;
        }
        fn temp(_: ?*anyopaque, _: c_uint, out: *c_uint) callconv(.c) c_uint {
            reads += 1;
            out.* = 42;
            return 0;
        }
        fn power(_: ?*anyopaque, out: *c_uint) callconv(.c) c_uint {
            reads += 1;
            out.* = 5500;
            return power_error;
        }
    };
    deinit();
    defer deinit();
    Fake.reads = 0;
    Fake.closes = 0;
    Fake.power_error = 0;
    const bus = "0000:01:00.0";
    nvml = .{ .lib = null, .shutdown = Fake.shutdown, .get_temp = Fake.temp, .get_power = Fake.power, .device = null, .bus = undefined, .bus_len = bus.len };
    @memcpy(nvml.?.bus[0..bus.len], bus);
    try std.testing.expectEqual(@as(?f64, 5.5), queryNvml(bus).watts);
    try std.testing.expectEqual(@as(?f64, 42), queryNvml(bus).celsius);
    try std.testing.expectEqual(@as(usize, 4), Fake.reads);
    try std.testing.expectEqual(@as(usize, 0), Fake.closes);

    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const root = try tmp.dir.realpathAlloc(a, ".");
    try tmp.dir.makePath(bus ++ "/power");
    try tmp.dir.writeFile(.{ .sub_path = bus ++ "/vendor", .data = "0x10de\n" });
    try tmp.dir.writeFile(.{ .sub_path = bus ++ "/class", .data = "0x030000\n" });
    for ([_][]const u8{ "suspended\n", "suspending\n", "resuming\n" }) |state| {
        try tmp.dir.writeFile(.{ .sub_path = bus ++ "/power/runtime_status", .data = state });
        const result = gpuAt(a, root);
        try std.testing.expect(result.watts == null and result.celsius == null);
        try std.testing.expectEqual(@as(usize, 4), Fake.reads);
        try std.testing.expectEqual(@as(usize, 0), Fake.closes);
    }
    try tmp.dir.writeFile(.{ .sub_path = bus ++ "/power/runtime_status", .data = "active\n" });
    try std.testing.expectEqual(@as(?f64, 5.5), gpuAt(a, root).watts);
    // A permanently unsupported metric must not churn the session.
    Fake.power_error = 3;
    try std.testing.expect(queryNvml(bus).watts == null);
    try std.testing.expectEqual(@as(usize, 0), Fake.closes);
    Fake.power_error = 15;
    try std.testing.expect(queryNvml(bus).watts == null);
    try std.testing.expect(nvml == null);
    try std.testing.expectEqual(@as(usize, 1), Fake.closes);
    const reads = Fake.reads;
    try std.testing.expectEqual(.active, queryNvml(bus).state);
    try std.testing.expectEqual(reads, Fake.reads);
    deinit();
    try std.testing.expectEqual(@as(usize, 1), Fake.closes);
}
