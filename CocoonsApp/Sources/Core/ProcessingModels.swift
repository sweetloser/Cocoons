import Foundation

struct ProcessingOptions: Codable, Sendable {
    var protectAnnotatedStrings = true
    var deterministicSeed: UInt64 = 0xC0C0_0A5
}

struct FileProcessingResult: Codable, Sendable {
    let relativePath: String
    let protectedStrings: Int
    let warnings: [String]
}

struct ProcessingReport: Codable, Sendable {
    let version: Int
    let startedAt: Date
    let finishedAt: Date
    let inputPath: String
    let outputPath: String
    let scannedFiles: Int
    let protectedStrings: Int
    let files: [FileProcessingResult]
    let warnings: [String]
}

enum ProjectProcessingError: LocalizedError {
    case inputDoesNotExist
    case inputIsNotDirectory
    case noXcodeProject
    case outputAlreadyExists(URL)
    case cannotCreateStagingDirectory

    var errorDescription: String? {
        switch self {
        case .inputDoesNotExist:
            return "输入路径不存在。"
        case .inputIsNotDirectory:
            return "输入路径不是工程文件夹。"
        case .noXcodeProject:
            return "输入文件夹中没有找到 .xcodeproj 或 .xcworkspace。"
        case let .outputAlreadyExists(url):
            return "输出目录已存在：\(url.path)"
        case .cannotCreateStagingDirectory:
            return "无法创建临时处理目录。"
        }
    }
}
