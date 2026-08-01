const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // The public module; depend on it with `@import("hayahash")`.
    const mod = b.addModule("hayahash", .{
        .root_source_file = b.path("src/root.zig"),
        .target = target,
        .optimize = optimize,
    });

    // Decl tests in the module root, plus the conformance tests
    // against the C reference (tests/kat.zig).
    const mod_tests = b.addTest(.{ .root_module = mod });
    const kat_tests = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("tests/kat.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{
                .{ .name = "hayahash", .module = mod },
            },
        }),
    });
    const differential_tests = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("tests/differential.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{
                .{ .name = "hayahash", .module = mod },
            },
        }),
    });

    const test_step = b.step("test", "Run tests");
    test_step.dependOn(&b.addRunArtifact(mod_tests).step);
    test_step.dependOn(&b.addRunArtifact(kat_tests).step);
    test_step.dependOn(&b.addRunArtifact(differential_tests).step);
}
