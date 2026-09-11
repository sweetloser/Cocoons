import SwiftUI

struct ContentView: View {
    @StateObject private var model = ProcessingViewModel()

    var body: some View {
        NavigationSplitView {
            VStack(alignment: .leading, spacing: 18) {
                VStack(alignment: .leading, spacing: 6) {
                    Text("Cocoons")
                        .font(.system(size: 26, weight: .semibold))
                    Text("Apple 工程保护")
                        .foregroundStyle(.secondary)
                }

                Divider()

                Label("工程处理", systemImage: "shippingbox")
                    .fontWeight(.medium)
                Label("历史记录", systemImage: "clock")
                    .foregroundStyle(.tertiary)

                Spacer()

                Text("MVP · Source Protection")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }
            .padding(20)
            .navigationSplitViewColumnWidth(min: 190, ideal: 210)
        } detail: {
            ScrollView {
                VStack(alignment: .leading, spacing: 24) {
                    header
                    projectSection
                    protectionSection
                    runSection
                    logSection
                }
                .padding(28)
                .frame(maxWidth: 820, alignment: .leading)
            }
        }
        .alert(
            "处理失败",
            isPresented: Binding(
                get: { model.errorMessage != nil },
                set: { if !$0 { model.errorMessage = nil } }
            )
        ) {
            Button("确定", role: .cancel) {}
        } message: {
            Text(model.errorMessage ?? "")
        }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("生成加固工程")
                .font(.title2.weight(.semibold))
            Text("原工程保持不变；处理结果生成在同级的 “_cocoons” 文件夹。")
                .foregroundStyle(.secondary)
        }
    }

    private var projectSection: some View {
        GroupBox("输入与输出") {
            VStack(alignment: .leading, spacing: 14) {
                HStack {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("输入工程")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        Text(model.inputURL?.path ?? "尚未选择")
                            .lineLimit(1)
                            .truncationMode(.middle)
                    }
                    Spacer()
                    Button("选择工程…") {
                        model.chooseProject()
                    }
                    .disabled(model.isProcessing)
                }

                Divider()

                VStack(alignment: .leading, spacing: 4) {
                    Text("输出工程")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Text(model.outputPreview)
                        .lineLimit(1)
                        .truncationMode(.middle)
                        .foregroundStyle(model.inputURL == nil ? .tertiary : .primary)
                }
            }
            .padding(8)
        }
    }

    private var protectionSection: some View {
        GroupBox("保护策略") {
            VStack(alignment: .leading, spacing: 14) {
                Toggle("保护显式标记的字符串", isOn: $model.protectAnnotatedStrings)
                Text("支持 annotate(\"obfuscate\")、annotate(\"cocoons:str\") 和 OBF 标记。")
                    .font(.caption)
                    .foregroundStyle(.secondary)

                Divider()

                HStack {
                    Text("确定性种子")
                    Spacer()
                    TextField("Seed", text: $model.deterministicSeed)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 150)
                        .disabled(model.isProcessing)
                }

                HStack(spacing: 8) {
                    Label("指令替换", systemImage: "hammer")
                    Text("后续版本")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Spacer()
                }
                .foregroundStyle(.tertiary)

                HStack(spacing: 8) {
                    Label("控制流平坦化", systemImage: "point.3.connected.trianglepath.dotted")
                    Text("后续版本")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Spacer()
                }
                .foregroundStyle(.tertiary)
            }
            .padding(8)
        }
    }

    private var runSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            if model.isProcessing {
                ProgressView(value: model.progress) {
                    Text(model.status)
                }
            } else {
                Text(model.status)
                    .foregroundStyle(.secondary)
            }

            HStack {
                Button("开始处理") {
                    model.process()
                }
                .buttonStyle(.borderedProminent)
                .disabled(!model.canProcess)

                if model.outputURL != nil {
                    Button("用 Xcode 打开") {
                        model.openOutputInXcode()
                    }
                    Button("在 Finder 中显示") {
                        model.revealOutput()
                    }
                }
            }
        }
    }

    private var logSection: some View {
        GroupBox("处理日志") {
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 5) {
                    if model.logLines.isEmpty {
                        Text("处理日志将在这里显示。")
                            .foregroundStyle(.tertiary)
                    } else {
                        ForEach(Array(model.logLines.enumerated()), id: \.offset) { _, line in
                            Text(line)
                                .font(.system(.caption, design: .monospaced))
                                .textSelection(.enabled)
                                .frame(maxWidth: .infinity, alignment: .leading)
                        }
                    }
                }
                .padding(8)
            }
            .frame(minHeight: 130, maxHeight: 210)
        }
    }
}
