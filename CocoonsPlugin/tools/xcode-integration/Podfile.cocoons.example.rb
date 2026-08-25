# ============================================================
# Cocoons: CocoaPods 接入示例
# 将 post_install 块复制到你项目的 Podfile 末尾
# ============================================================

platform :ios, '13.0'
use_frameworks!

target 'MyApp' do
  # pod 'AFNetworking'
  # pod 'SnapKit'
end

# ---------- Cocoons 接入 hook 开始 ----------
COCOONS_PLUGIN_PATH = File.expand_path('../CocoonsPlugin/dist/libCocoonsPlugin.dylib', __dir__)
COCOONS_ENABLE = {
  'Debug'   => false,   # Debug 关闭混淆加速编译
  'Release' => true,    # Release 开启
}

COCOONS_FLAGS = [
  "-fpass-plugin=#{COCOONS_PLUGIN_PATH}",
  '-mllvm', '-cocoons-enable-str',
  '-mllvm', '-cocoons-enable-sub',
  '-mllvm', '-cocoons-sub-loop=2',
  '-mllvm', '-cocoons-enable-fla',
].join(' ')

post_install do |installer|
  unless File.exist?(COCOONS_PLUGIN_PATH)
    puts "[Cocoons] ⚠️  找不到插件: #{COCOONS_PLUGIN_PATH}，跳过注入。请先编译 CocoonsPlugin."
    return
  end
  puts "[Cocoons] ✅ 注入到 Pods 工程: #{COCOONS_PLUGIN_PATH}"

  installer.pods_project.targets.each do |target|
    target.build_configurations.each do |config|
      enabled = COCOONS_ENABLE.fetch(config.name, false)
      next unless enabled

      # 注入 OTHER_CFLAGS（C/OC/C++）
      current_cflags = config.build_settings['OTHER_CFLAGS'] || '$(inherited)'
      unless current_cflags.include?('-fpass-plugin=')
        config.build_settings['OTHER_CFLAGS'] = "#{current_cflags} #{COCOONS_FLAGS}"
      end

      # 注入 Swift 混编需要的 -Xcc 参数
      current_swift = config.build_settings['OTHER_SWIFT_FLAGS'] || '$(inherited)'
      unless current_swift.include?('COCOONS')
        swift_flags = COCOONS_FLAGS.split(' ').map { |f| "-Xcc \"#{f}\"" }.join(' ')
        config.build_settings['OTHER_SWIFT_FLAGS'] = "#{current_swift} #{swift_flags}"
      end
    end
  end
end
# ---------- Cocoons 接入 hook 结束 ----------
