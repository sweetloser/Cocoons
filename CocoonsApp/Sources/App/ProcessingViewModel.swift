import AppKit
import Foundation

@MainActor
final class ProcessingViewModel: ObservableObject {
    @Published var inputURL: URL?
    @Published var protectAnnotatedStrings = true
    @Published var deterministicSeed = "20260901"
    @Published var isProcessing = false
    @Published var progress = 0.0
    @Published var status = "选择一个 Xcode 工程文件夹"
    @Published var logLines: [String] = []
    @Published var outputURL: URL?
    @Published var errorMessage: String?

    private let processor = ProjectProcessor()

    var outputPreview: String {
        guard let inputURL else { return "—" }
        return inputURL
            .deletingLastPathComponent()
            .appendingPathComponent("\(inputURL.lastPathComponent)_cocoons")
            .path
    }

    var canProcess: Bool {
        inputURL != nil && !isProcessing
    }

    func chooseProject() {
        let panel = NSOpenPanel()
        panel.title = "选择 Xcode 工程根目录"
        panel.prompt = "选择工程"
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        panel.canCreateDirectories = false

        guard panel.runModal() == .OK, let selectedURL = panel.url else { return }
        inputURL = selectedURL
        outputURL = nil
        errorMessage = nil
        status = "准备处理 \(selectedURL.lastPathComponent)"
        logLines = ["输入：\(selectedURL.path)", "输出：\(outputPreview)"]
    }

    func process() {
        guard let inputURL else { return }

        let seed = UInt64(deterministicSeed) ?? 0xC0C0_0A5
        let options = ProcessingOptions(
            protectAnnotatedStrings: protectAnnotatedStrings,
            deterministicSeed: seed
        )

        isProcessing = true
        progress = 0
        outputURL = nil
        errorMessage = nil
        logLines.append("开始处理…")

        Task {
            do {
                let report = try await processor.process(
                    inputURL: inputURL,
                    options: options
                ) { [weak self] fraction, message in
                    Task { @MainActor in
                        self?.progress = fraction
                        self?.status = message
                        self?.logLines.append(message)
                    }
                }

                let resultURL = URL(fileURLWithPath: report.outputPath)
                outputURL = resultURL
                status = "已生成 \(resultURL.lastPathComponent)"
                logLines.append(
                    "完成：扫描 \(report.scannedFiles) 个源码文件，保护 \(report.protectedStrings) 个字符串。"
                )
            } catch {
                errorMessage = error.localizedDescription
                status = "处理失败"
                logLines.append("失败：\(error.localizedDescription)")
            }
            isProcessing = false
        }
    }

    func revealOutput() {
        guard let outputURL else { return }
        NSWorkspace.shared.activateFileViewerSelecting([outputURL])
    }

    func openOutputInXcode() {
        guard let outputURL else { return }
        let candidates = ((try? FileManager.default.contentsOfDirectory(
            at: outputURL,
            includingPropertiesForKeys: nil
        )) ?? []).filter {
            ["xcworkspace", "xcodeproj"].contains($0.pathExtension.lowercased())
        }
        guard let project = candidates.sorted(by: { lhs, rhs in
            lhs.pathExtension == "xcworkspace" && rhs.pathExtension != "xcworkspace"
        }).first else {
            NSWorkspace.shared.open(outputURL)
            return
        }
        NSWorkspace.shared.open(project)
    }
}
