import Foundation
import XCTest
@testable import Cocoons

final class ProjectProcessorTests: XCTestCase {
    func testCreatesSiblingProjectAndLeavesInputUntouched() async throws {
        let parent = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        let input = parent.appendingPathComponent("Sample", isDirectory: true)
        let project = input.appendingPathComponent("Sample.xcodeproj", isDirectory: true)
        let sourceURL = input.appendingPathComponent("main.c")
        let source = #"""
        __attribute__((annotate("obfuscate")))
        const char secret[] = "PROCESSOR_SECRET";
        int main(void) { return secret[0] == 0; }
        """#

        try FileManager.default.createDirectory(at: project, withIntermediateDirectories: true)
        try source.write(to: sourceURL, atomically: true, encoding: .utf8)
        defer { try? FileManager.default.removeItem(at: parent) }

        let report = try await ProjectProcessor().process(
            inputURL: input,
            options: ProcessingOptions(
                protectAnnotatedStrings: true,
                deterministicSeed: 12
            )
        )

        let output = parent.appendingPathComponent("Sample_cocoons", isDirectory: true)
        let outputSource = try String(
            contentsOf: output.appendingPathComponent("main.c"),
            encoding: .utf8
        )
        let originalSource = try String(contentsOf: sourceURL, encoding: .utf8)

        XCTAssertEqual(originalSource, source)
        XCTAssertFalse(outputSource.contains("\"PROCESSOR_SECRET\""))
        XCTAssertEqual(report.protectedStrings, 1)
        XCTAssertTrue(
            FileManager.default.fileExists(
                atPath: output.appendingPathComponent(".cocoons/report.json").path
            )
        )
        XCTAssertTrue(
            FileManager.default.fileExists(
                atPath: output.appendingPathComponent("COCOONS_REPORT.md").path
            )
        )
    }

    func testRefusesToOverwriteExistingOutput() async throws {
        let parent = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        let input = parent.appendingPathComponent("Sample", isDirectory: true)
        let project = input.appendingPathComponent("Sample.xcodeproj", isDirectory: true)
        let output = parent.appendingPathComponent("Sample_cocoons", isDirectory: true)
        try FileManager.default.createDirectory(at: project, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: parent) }

        do {
            _ = try await ProjectProcessor().process(
                inputURL: input,
                options: ProcessingOptions()
            )
            XCTFail("Expected outputAlreadyExists")
        } catch let error as ProjectProcessingError {
            guard case .outputAlreadyExists = error else {
                return XCTFail("Unexpected error: \(error)")
            }
        }
    }
}
