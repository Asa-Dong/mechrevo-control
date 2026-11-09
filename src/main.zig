// SPDX-License-Identifier: GPL-2.0-only
const std = @import("std");
const status = @import("status.zig");
const control = @import("control.zig");
const c = @cImport({
    @cInclude("systemd/sd-bus.h");
    @cInclude("signal.h");
    @cInclude("systemd/sd-login.h");
    @cInclude("stdlib.h");
});
const name = "io.github.mechrevo.Control1";
const path = "/io/github/mechrevo/Control1";
const xml = "<node><interface name='io.github.mechrevo.Control1'><method name='GetStatus'><arg name='json' type='s' direction='out'/></method><method name='Set'><arg name='setting' type='s' direction='in'/><arg name='value' type='s' direction='in'/></method></interface><interface name='org.freedesktop.DBus.Introspectable'><method name='Introspect'><arg name='xml' type='s' direction='out'/></method></interface></node>";
var system_mode = false;
var stopping = std.atomic.Value(bool).init(false);
fn stop(_: c_int) callconv(.c) void {
    stopping.store(true, .release);
}

// Bind authorization to the D-Bus sender's unique name, never caller-supplied UID/PID.
fn authorized(m: ?*c.sd_bus_message) bool {
    var creds: ?*c.sd_bus_creds = null;
    if (c.sd_bus_query_sender_creds(m, c.SD_BUS_CREDS_EUID, &creds) < 0) return false;
    defer _ = c.sd_bus_creds_unref(creds);
    var uid: c.uid_t = undefined;
    if (c.sd_bus_creds_get_euid(creds, &uid) < 0) return false;
    if (uid == 0) return true;
    // Require a real local desktop session for this UID before consulting Polkit.
    // A runuser process can inherit someone else's session; UID ownership matters.
    var session: [*c]u8 = null;
    if (c.sd_uid_get_display(uid, &session) < 0 or session == null) return false;
    defer c.free(session);
    var session_uid: c.uid_t = undefined;
    if (c.sd_session_get_uid(session, &session_uid) < 0 or session_uid != uid or
        c.sd_session_is_active(session) <= 0 or c.sd_session_is_remote(session) != 0) return false;
    var reply: ?*c.sd_bus_message = null;
    var err: c.sd_bus_error = .{ .name = null, .message = null, ._need_free = 0 };
    defer c.sd_bus_error_free(&err);
    defer _ = c.sd_bus_message_unref(reply);
    const sender = c.sd_bus_message_get_sender(m);
    if (sender == null) return false;
    const rc = c.sd_bus_call_method(c.sd_bus_message_get_bus(m), "org.freedesktop.PolicyKit1", "/org/freedesktop/PolicyKit1/Authority", "org.freedesktop.PolicyKit1.Authority", "CheckAuthorization", &err, &reply, "(sa{sv})sa{ss}us", @as([*:0]const u8, "system-bus-name"), @as(c_int, 1), @as([*:0]const u8, "name"), @as([*:0]const u8, "s"), sender, @as([*:0]const u8, "io.github.mechrevo.control.manage"), @as(c_int, 0), @as(u32, 1), @as([*:0]const u8, ""));
    if (rc < 0) {
        std.log.warn("Polkit authorization call failed: {s}", .{if (err.name != null) std.mem.span(err.name) else "unknown"});
        return false;
    }
    if (c.sd_bus_message_enter_container(reply, 'r', "bba{ss}") <= 0) return false;
    var allowed: c_int = 0;
    var challenge: c_int = 0;
    return c.sd_bus_message_read(reply, "bb", &allowed, &challenge) >= 0 and allowed != 0;
}
fn handler(m: ?*c.sd_bus_message, _: ?*anyopaque, _: ?*c.sd_bus_error) callconv(.c) c_int {
    if (c.sd_bus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect") > 0)
        return c.sd_bus_reply_method_return(m, "s", @as([*:0]const u8, xml));
    const is_status = c.sd_bus_message_is_method_call(m, name, "GetStatus") > 0;
    const is_set = c.sd_bus_message_is_method_call(m, name, "Set") > 0;
    if (!is_status and !is_set) return 0;
    if (c.sd_bus_message_has_signature(m, if (is_status) "" else "ss") <= 0)
        return c.sd_bus_reply_method_errorf(m, "org.freedesktop.DBus.Error.InvalidArgs", "Invalid method arguments");
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const a = arena.allocator();
    if (is_set) {
        var key: [*c]const u8 = null;
        var value: [*c]const u8 = null;
        if (c.sd_bus_message_read(m, "ss", &key, &value) < 0 or key == null or value == null)
            return c.sd_bus_reply_method_errorf(m, "org.freedesktop.DBus.Error.InvalidArgs", "Expected setting and value");
        const k = std.mem.span(key);
        const v = std.mem.span(value);
        if (!control.valid(k, v))
            return c.sd_bus_reply_method_errorf(m, "org.freedesktop.DBus.Error.InvalidArgs", "Unsupported setting or value");
        if (!system_mode or !authorized(m))
            return c.sd_bus_reply_method_errorf(m, "org.freedesktop.DBus.Error.AccessDenied", "Hardware control authorization required");
        control.apply(a, k, v) catch |err| {
            const msg = a.dupeZ(u8, @errorName(err)) catch return -12;
            return c.sd_bus_reply_method_errorf(m, "io.github.mechrevo.Error.WriteFailed", "%s", msg.ptr);
        };
        return c.sd_bus_reply_method_return(m, "");
    }
    const json = status.snapshot(a, "/sys") catch return c.sd_bus_reply_method_errorf(m, "io.github.mechrevo.Error.ReadFailed", "Hardware status unavailable");
    const text = a.dupeZ(u8, json) catch return -12;
    return c.sd_bus_reply_method_return(m, "s", text.ptr);
}
pub fn main() !void {
    defer @import("telemetry.zig").deinit();
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const args = try std.process.argsAlloc(a);
    if (args.len == 2 and std.mem.eql(u8, args[1], "--version")) {
        try std.io.getStdOut().writer().print("mechrevo-control {s}\n", .{@import("build_options").version});
        return;
    }
    if (args.len == 2 and std.mem.eql(u8, args[1], "--once")) {
        try std.io.getStdOut().writer().print("{s}\n", .{try status.snapshot(a, "/sys")});
        return;
    }
    system_mode = args.len == 2 and std.mem.eql(u8, args[1], "--system");
    if (args.len > 1 and !system_mode) return error.InvalidArguments;
    var bus: ?*c.sd_bus = null;
    if ((if (system_mode) c.sd_bus_open_system(&bus) else c.sd_bus_open_user(&bus)) < 0) return error.BusUnavailable;
    defer _ = c.sd_bus_flush_close_unref(bus);
    if (c.sd_bus_add_object(bus, null, path, handler, null) < 0) return error.ObjectRegistrationFailed;
    if (c.sd_bus_request_name(bus, name, 0) < 0) return error.BusNameUnavailable;
    _ = c.signal(c.SIGTERM, stop);
    _ = c.signal(c.SIGINT, stop);
    while (!stopping.load(.acquire)) {
        const r = c.sd_bus_process(bus, null);
        if (r < 0) return error.BusProcessingFailed;
        if (r > 0) continue;
        const waited = c.sd_bus_wait(bus, 1_000_000);
        if (waited < 0 and waited != -4) return error.BusWaitFailed;
    }
}
