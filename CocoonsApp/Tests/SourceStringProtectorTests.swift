import Foundation
import XCTest
@testable import Cocoons

final class SourceStringProtectorTests: XCTestCase {
    func testTransformsAnnotatedCStringWithoutLeavingPlaintext() throws {
        let source = """
        #include <stdio.h>
        __attribute__((annotate("obfuscate")))
        const char secret[] = "COCOONS_TOP_SECRET";
        int main(void) { puts(secret); return 0; }
        """
        let protector = SourceStringProtector(
            options: ProcessingOptions(
                protectAnnotatedStrings: true,
                deterministicSeed: 42
            )
        )

        let result = try protector.transform(source: source, relativePath: "main.c")

        XCTAssertEqual(result.protectedCount, 1)
        XCTAssertFalse(result.source.contains("\"COCOONS_TOP_SECRET\""))
        XCTAssertTrue(result.source.contains("cocoons_decode_"))
    }

    func testUnmarkedStringsAreNotChanged() throws {
        let source = #"const char message[] = "PUBLIC";"#
        let protector = SourceStringProtector(options: ProcessingOptions())

        let result = try protector.transform(source: source, relativePath: "main.c")

        XCTAssertEqual(result.protectedCount, 0)
        XCTAssertEqual(result.source, source)
    }

    func testGeneratedCSourceCompilesAndDecodesAtRuntime() throws {
        let source = """
        #include <stdio.h>
        __attribute__((annotate("cocoons:str")))
        const char secret[] = "COCOONS_RUNTIME_SECRET";
        int main(void) { puts(secret); return 0; }
        """
        let protector = SourceStringProtector(
            options: ProcessingOptions(
                protectAnnotatedStrings: true,
                deterministicSeed: 7
            )
        )
        let result = try protector.transform(source: source, relativePath: "main.c")
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }

        let input = directory.appendingPathComponent("main.c")
        let executable = directory.appendingPathComponent("main")
        try result.source.write(to: input, atomically: true, encoding: .utf8)

        try run("/usr/bin/xcrun", arguments: ["clang", input.path, "-o", executable.path])
        let output = try run(executable.path, arguments: [])

        XCTAssertEqual(output.trimmingCharacters(in: .whitespacesAndNewlines), "COCOONS_RUNTIME_SECRET")
    }

    func testGeneratedObjectiveCStringCompilesAndDecodesAtRuntime() throws {
        let source = """
        #import <Foundation/Foundation.h>
        __attribute__((annotate("obfuscate")))
        NSString *secret = @"COCOONS_OBJC_SECRET";
        int main(void) {
            @autoreleasepool { puts([secret UTF8String]); }
            return 0;
        }
        """
        let protector = SourceStringProtector(
            options: ProcessingOptions(
                protectAnnotatedStrings: true,
                deterministicSeed: 9
            )
        )
        let result = try protector.transform(source: source, relativePath: "main.m")
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }

        let input = directory.appendingPathComponent("main.m")
        let executable = directory.appendingPathComponent("main")
        try result.source.write(to: input, atomically: true, encoding: .utf8)

        try run(
            "/usr/bin/xcrun",
            arguments: [
                "clang", "-fobjc-arc", "-framework", "Foundation",
                input.path, "-o", executable.path
            ]
        )
        let output = try run(executable.path, arguments: [])

        XCTAssertFalse(result.source.contains("@\"COCOONS_OBJC_SECRET\""))
        XCTAssertEqual(output.trimmingCharacters(in: .whitespacesAndNewlines), "COCOONS_OBJC_SECRET")
    }

    @discardableResult
    private func run(_ executable: String, arguments: [String]) throws -> String {
        let process = Process()
        let output = Pipe()
        let errors = Pipe()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        process.standardOutput = output
        process.standardError = errors
        try process.run()
        process.waitUntilExit()

        let stdout = output.fileHandleForReading.readDataToEndOfFile()
        let stderr = errors.fileHandleForReading.readDataToEndOfFile()
        guard process.terminationStatus == 0 else {
            let message = String(data: stderr, encoding: .utf8) ?? "unknown error"
            XCTFail("Process failed: \(message)")
            return ""
        }
        return String(data: stdout, encoding: .utf8) ?? ""
    }
}
