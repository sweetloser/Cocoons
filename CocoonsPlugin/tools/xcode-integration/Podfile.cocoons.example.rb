# ============================================================
# Cocoons: CocoaPods 接入示例 (v1.2.0+)
# 将 post_install 块复制到你项目的 Podfile 末尾
# ------------------------------------------------------------
# v1.2.0 重要变更：
#   - 不再通过 `-mllvm -cocoons-*` 传开关（会报 Unknown command line）
#   - 改用 **环境变量 COCOONS_*** 或 `.cocoons.json`**。
#   - 本示例通过 pod install 后写入 "Cocoons-Environment.xcconfig"
#     + Build Phases 前置 Shell Script 的方式传 env var。
# ============================================================

platform :ios, '13.0'
use_frameworks!

target 'MyApp' do
  # pod 'AFNetworking'
  # pod 'SnapKit'
end

# ---------- Cocoons 接入 hook 开始 ----------
COCOONS_PLUGIN_PATH = File.expand_path('../CocoonsPlugin/dist/libCocoonsPlugin.dylib', __dir__)

COCOONS_SWITCHES = {
  # '1' / true  = 开启
  # '0' / false = 关闭
  # 也可以写 "annotation" / "annotations-only" = 只对带
  # __attribute__((annotate("cocoons:fla"))) 等注解的函数/变量生效。
  :enable_str       => true,
  :enable_sub       => true,
  :sub_loop         => 2,
  :enable_fla       => true,
  :fla_probability  => 1.0,
  :fla_min_bbs      => 3,
  :fla_max_bbs      => 200,
  :enable_anti_debug => false,   # Anti-Debug 默认关，免得影响调试 / 上架
}

COCOONS_ENABLE_FOR = {
  'Debug'   => false,   # Debug 关闭混淆加速编译
  'Release' => true,    # Release 开启
}

PASS_PLUGIN_FLAG = "-fpass-plugin=#{COCOONS_PLUGIN_PATH}"

post_install do |installer|
  unless File.exist?(COCOONS_PLUGIN_PATH)
    puts "[Cocoons] ⚠️  找不到插件: #{COCOONS_PLUGIN_PATH}，跳过注入。请先运行:"
    puts "  cmake -S CocoonsPlugin -B CocoonsPlugin/build -DLLVM_DIR=\\$(brew --prefix llvm@21)/lib/cmake/llvm"
    puts "  cmake --build CocoonsPlugin/build"
    return
  end
  puts "[Cocoons] ✅ pod post_install 注入开始..."
  puts "  插件:       #{COCOONS_PLUGIN_PATH}"
  puts "  开关:       #{COCOONS_SWITCHES.inspect}"
  puts "  开关注入 配置: #{COCOONS_ENABLE_FOR.inspect}"

  # ------------------------------------------------------------------
  # 1. 生成 Cocoons-Environment.xcconfig (在 Pods/ 里) 放 COCOONS_*
  #    开关，方便 Build Phases 脚本 source 进来 export。
  # ------------------------------------------------------------------
  xcconfig_path = File.join(installer.config.installation_root, 'Pods', 'Target Support Files', 'Pods-MyApp', 'Cocoons-Environment.xcconfig')
  FileUtils.mkdir_p(File.dirname(xcconfig_path))
  env_lines = COCOONS_SWITCHES.map do |k, v|
    up = k.to_s.upcase
    case v
    when TrueClass, FalseClass then "export COCOONS_#{up}=#{v ? '1' : '0'}"
    when Numeric then             "export COCOONS_#{up}=#{v}"
    when String  then             "export COCOONS_#{up}=\"#{v.gsub('"', '\"')}\""
    else
      "export COCOONS_#{up}=#{v}"
    end
  end
  File.write(xcconfig_path, <<~SHELL)
    // ---------------------------------------------------------
    // Cocoons auto-generated (Podfile post_install). DO NOT EDIT.
    // 用在 Build Phases → Run Script "source" 里, 不是 xcconfig include
    // ---------------------------------------------------------
    #{env_lines.join("\n")}
  SHELL
  puts "[Cocoons] ✅ 写入环境变量脚本 → #{xcconfig_path}"

  # ------------------------------------------------------------------
  # 2. 遍历 Pods 项目所有 target，在 Release 注入 OTHER_CFLAGS /
  #    OTHER_SWIFT_FLAGS + 前置 Run Script Phase export env
  # ------------------------------------------------------------------
  installer.pods_project.targets.each do |target|
    target.build_configurations.each do |config|
      enabled = COCOONS_ENABLE_FOR.fetch(config.name, false)

      # --- a. OTHER_CFLAGS (C/ObjC/C++ 代码) ---
      current_cflags = config.build_settings['OTHER_CFLAGS'] || '$(inherited)'
      if enabled && !current_cflags.include?('-fpass-plugin=')
        config.build_settings['OTHER_CFLAGS'] = "#{current_cflags} #{PASS_PLUGIN_FLAG}"
      end

      # --- b. OTHER_SWIFT_FLAGS (Swift 混编) ---
      current_swift = config.build_settings['OTHER_SWIFT_FLAGS'] || '$(inherited)'
      if enabled && !current_swift.include?('COCOONS_PLUGIN')
        # 给 swift 编译器 + clang importer 都传
        config.build_settings['OTHER_SWIFT_FLAGS'] = "#{current_swift} " \
          "-Xcc #{PASS_PLUGIN_FLAG.shellescape}"
      end
    end

    # --- c. 在所有 sources 编译前 export COCOONS_* env var ---
    # Build Phases 顺序很重要：Run Script 必须在 Compile Sources 之前，
    # 这样脚本里 export 的环境变量会被 xcodebuild 继承给 clang 子进程。
    existing = target.build_phases.index { |p|
      p.is_a?(Xcodeproj::Project::Object::PBXShellScriptBuildPhase) &&
      p.name.to_s.include?("[Cocoons]")
    }
    unless existing
      phase = target.project.new(Xcodeproj::Project::Object::PBXShellScriptBuildPhase)
      phase.name    = '[Cocoons] Export Environment'
      phase.shell_script = <<~SHELL
        # 把 Pods 下 Cocoons-Environment.xcconfig 里的 COCOONS_* 变量 source
        # 到当前 build 的 env 里 (xcodebuild 会把脚本 export 的变量透传给
        # 之后 Compile Sources 阶段的 clang 子进程).
        ENVSCRIPT="#{'${PODS_ROOT}'}/Target Support Files/Pods-#{target.name}/Cocoons-Environment.xcconfig"
        if [ -f "$ENVSCRIPT" ]; then
          # strip // comments and only source lines with "export COCOONS_"
          grep '^export COCOONS_' "$ENVSCRIPT" > /tmp/cocoons.$$.sh || true
          if [ -s /tmp/cocoons.$$.sh ]; then
            . /tmp/cocoons.$$.sh
          fi
          rm -f /tmp/cocoons.$$.sh
        fi
      SHELL
      phase.shell_path = '/bin/bash'
      target.build_phases.insert(0, phase)  # 插到最前
    end
  end

  puts "[Cocoons] ✅ Pods project 注入完成。pod install 完成后打开 .xcworkspace 即可。"
end
# ---------- Cocoons 接入 hook 结束 ----------
