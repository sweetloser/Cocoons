import Foundation

actor ProjectProcessor {
    typealias ProgressHandler = @Sendable (_ fraction: Double, _ message: String) -> Void

    private let fileManager: FileManager

    init(fileManager: FileManager = .default) {
        self.fileManager = fileManager
    }

    func process(
        inputURL: URL,
        options: ProcessingOptions,
        progress: ProgressHandler? = nil
    ) throws -> ProcessingReport {
        let startedAt = Date()
        let input = inputURL.standardizedFileURL
        try validateInput(input)

        let output = input
            .deletingLastPathComponent()
            .appendingPathComponent("\(input.lastPathComponent)_cocoons", isDirectory: true)

        guard !fileManager.fileExists(atPath: output.path) else {
            throw ProjectProcessingError.outputAlreadyExists(output)
        }

        let staging = input
            .deletingLastPathComponent()
            .appendingPathComponent(
                ".\(input.lastPathComponent)_cocoons.\(UUID().uuidString).tmp",
                isDirectory: true
            )

        progress?(0.02, "正在复制工程…")
        do {
            try fileManager.copyItem(at: input, to: staging)
        } catch {
            try? fileManager.removeItem(at: staging)
            throw ProjectProcessingError.cannotCreateStagingDirectory
        }

        do {
            try removeGeneratedDirectories(from: staging)
            let sourceFiles = try collectSourceFiles(in: staging)
            let protector = SourceStringProtector(options: options)

            var fileResults: [FileProcessingResult] = []
            var globalWarnings: [String] = []
            var protectedStrings = 0

            for (index, fileURL) in sourceFiles.enumerated() {
                let relativePath = relativePath(of: fileURL, under: staging)
                let fraction = 0.10 + (Double(index) / Double(max(sourceFiles.count, 1))) * 0.75
                progress?(fraction, "扫描 \(relativePath)")

                do {
                    let source = try String(contentsOf: fileURL, encoding: .utf8)
                    let result = try protector.transform(
                        source: source,
                        relativePath: relativePath
                    )
                    if result.protectedCount > 0 {
                        try result.source.write(to: fileURL, atomically: true, encoding: .utf8)
                    }
                    protectedStrings += result.protectedCount
                    fileResults.append(
                        FileProcessingResult(
                            relativePath: relativePath,
                            protectedStrings: result.protectedCount,
                            warnings: result.warnings
                        )
                    )
                } catch {
                    let warning = "跳过 \(relativePath)：\(error.localizedDescription)"
                    globalWarnings.append(warning)
                    fileResults.append(
                        FileProcessingResult(
                            relativePath: relativePath,
                            protectedStrings: 0,
                            warnings: [warning]
                        )
                    )
                }
            }

            progress?(0.88, "正在写入处理报告…")
            let report = ProcessingReport(
                version: 1,
                startedAt: startedAt,
                finishedAt: Date(),
                inputPath: input.path,
                outputPath: output.path,
                scannedFiles: sourceFiles.count,
                protectedStrings: protectedStrings,
                files: fileResults,
                warnings: globalWarnings
            )
            try writeMetadata(options: options, report: report, to: staging)

            progress?(0.96, "正在生成输出工程…")
            try fileManager.moveItem(at: staging, to: output)
            progress?(1.0, "处理完成")
            return report
        } catch {
            try? fileManager.removeItem(at: staging)
            throw error
        }
    }

    private func validateInput(_ input: URL) throws {
        var isDirectory: ObjCBool = false
        guard fileManager.fileExists(atPath: input.path, isDirectory: &isDirectory) else {
            throw ProjectProcessingError.inputDoesNotExist
        }
        guard isDirectory.boolValue else {
            throw ProjectProcessingError.inputIsNotDirectory
        }

        let children = try fileManager.contentsOfDirectory(
            at: input,
            includingPropertiesForKeys: nil
        )
        let hasXcodeContainer = children.contains {
            ["xcodeproj", "xcworkspace"].contains($0.pathExtension.lowercased())
        }
        guard hasXcodeContainer else {
            throw ProjectProcessingError.noXcodeProject
        }
    }

    private func collectSourceFiles(in root: URL) throws -> [URL] {
        let supportedExtensions = Set(["c", "m", "mm", "cc", "cpp", "cxx"])
        let skippedDirectoryNames = Set([
            ".git", ".build", "build", "DerivedData", "Pods", "Carthage", "Vendor"
        ])
        let keys: [URLResourceKey] = [.isDirectoryKey, .isRegularFileKey]
        guard let enumerator = fileManager.enumerator(
            at: root,
            includingPropertiesForKeys: keys,
            options: [.skipsHiddenFiles]
        ) else {
            return []
        }

        var files: [URL] = []
        for case let url as URL in enumerator {
            let values = try url.resourceValues(forKeys: Set(keys))
            if values.isDirectory == true {
                if skippedDirectoryNames.contains(url.lastPathComponent) {
                    enumerator.skipDescendants()
                }
                continue
            }
            if values.isRegularFile == true,
               supportedExtensions.contains(url.pathExtension.lowercased()) {
                files.append(url)
            }
        }
        return files.sorted { $0.path < $1.path }
    }

    private func removeGeneratedDirectories(from root: URL) throws {
        for name in [".git", "DerivedData"] {
            let url = root.appendingPathComponent(name, isDirectory: true)
            if fileManager.fileExists(atPath: url.path) {
                try fileManager.removeItem(at: url)
            }
        }
    }

    private func writeMetadata(
        options: ProcessingOptions,
        report: ProcessingReport,
        to root: URL
    ) throws {
        let metadataDirectory = root.appendingPathComponent(".cocoons", isDirectory: true)
        try fileManager.createDirectory(
            at: metadataDirectory,
            withIntermediateDirectories: true
        )

        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        encoder.dateEncodingStrategy = .iso8601

        try encoder.encode(options).write(
            to: metadataDirectory.appendingPathComponent("config.json"),
            options: .atomic
        )
        try encoder.encode(report).write(
            to: metadataDirectory.appendingPathComponent("report.json"),
            options: .atomic
        )

        let readme = """
        # Cocoons protected project

        This folder was generated from:
        `\(report.inputPath)`

        Generated at: \(ISO8601DateFormatter().string(from: report.finishedAt))
        Protected annotated strings: \(report.protectedStrings)

        Open the copied `.xcodeproj` or `.xcworkspace` with Xcode. Cocoons.app
        does not need to remain running.
        """
        try readme.write(
            to: root.appendingPathComponent("COCOONS_REPORT.md"),
            atomically: true,
            encoding: .utf8
        )
    }

    private func relativePath(of url: URL, under root: URL) -> String {
        let rootPath = root.standardizedFileURL.path
        let path = url.standardizedFileURL.path
        guard path.hasPrefix(rootPath + "/") else { return url.lastPathComponent }
        return String(path.dropFirst(rootPath.count + 1))
    }
}
