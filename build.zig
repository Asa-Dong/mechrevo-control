// SPDX-License-Identifier: GPL-2.0-only
const std = @import("std");
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const exe = b.addExecutable(.{ .name = "mechrevo-control", .root_source_file = b.path("src/main.zig"), .target = target, .optimize = optimize });
    const version = std.mem.trim(u8, @embedFile("VERSION"), " \r\n");
    const options = b.addOptions();
    options.addOption([]const u8, "version", version);
    exe.root_module.addOptions("build_options", options);
    exe.linkLibC();
    exe.linkSystemLibrary("systemd");
    b.installArtifact(exe);
    const tests = b.addTest(.{ .root_source_file = b.path("src/status.zig"), .target = target, .optimize = optimize });
    tests.linkLibC();
    const test_step = b.step("test", "Test discovery, control and preferences");
    test_step.dependOn(&b.addRunArtifact(tests).step);
    const preferences_tests = b.addTest(.{ .root_source_file = b.path("src/preferences.zig"), .target = target, .optimize = optimize });
    test_step.dependOn(&b.addRunArtifact(preferences_tests).step);
}
