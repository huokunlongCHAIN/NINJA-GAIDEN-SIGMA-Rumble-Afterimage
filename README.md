# Ninja Gaiden Sigma Enhancements

《忍者龙剑传 Σ1》PC 功能补丁，提供五套服装的动作残影、手柄震动和 DualSense 输入。三项可独立安装、更新和还原。兼容已验证的 1.0.0.2 与 1.0.0.4 构建，包括已测试的 @imjinjia 1.0.0.4 汉化版。

[下载安装包 / Download the installer](https://github.com/huokunlongCHAIN/NINJA-GAIDEN-SIGMA-Rumble-Afterimage/releases/latest)

## 安装与调整

当前分支是 0.6.27 纯插件测试版。退出游戏并完整解压安装包，保留 `Payload` 文件夹，打开 `NGS1RumblePatch.exe` 选择游戏 EXE。由游戏直接加载插件，不再注册常驻自动加载器。安装器未签名，未宣称通过杀毒或 Nexus 审核。

右上角可即时切换简体中文和 English，安装器会记住上次选择。安装包附有中英文使用说明。

「残影」页提供「仿原版材质」「仿sigma2材质」和「仿无印2材质」。三种材质在五套服装上各自保存发光强度、颜色和透明度。三种材质的发光强度，以及仿sigma2和仿无印2的材质亮度，均可调至 200%。先安装／更新残影运行库，再点击「保存并应用材质」热重载设置。默认黑衣、蓝色传说忍者服和杀戮凶手提供肌肉高亮调节，默认 45%。场景深度可用时，墙面会挡住后方残影。核显专项模式与 Intel 显卡默认跳过场景深度复制，可能影响墙面遮挡。

「渲染模式」默认按游戏实际使用的显卡选择。独显使用完整轨迹和全分辨率遮罩，核显使用两层轨迹和半分辨率遮罩。安装器显示本机 CPU 和显卡，并可手动选择「常规优化」或「核显专项优化」。点击「应用模式」后，运行中的游戏会自动读取设置。

## 故障排查

安装失败或游戏闪退后，在安装器里选中对应的游戏 EXE，点击「导出诊断包」。ZIP 包含最近的安装结果、运行库日志，以及 Windows 为该游戏记录的崩溃摘要。补丁会写入本机日志，导出由玩家主动操作。游戏目录和 Windows 用户目录在导出内容中会被替换为占位符。

闪退后可点「追踪游戏闪退」重新进入并复现问题，再导出诊断包。报告可包含故障模块、调用栈、实际渲染显卡、残影模式和帧间隔。残影不显示时，可在执行对应动作后直接导出。

## 从源码构建

使用 Windows x64、PowerShell 7、带 MinGW 运行库的 x64 LLVM 工具链和 Windows .NET Framework 4.x。将 `clang++.exe` 所在目录加入 `PATH`，或用 `-ClangDirectory` 指定它。MinHook 源码随仓库提供。

```powershell
pwsh -File .\build.ps1 -RunRenderTests
pwsh -File .\package.ps1
```

安装器生成在 `release/NGS1RumblePatch.exe`。`-RunRenderTests` 会运行 D3D11 WARP 渲染测试和动作时序测试。安装器集成测试另外接受临时目录、上一版安装器和用于检查的本地游戏 EXE 路径。

如已有 Windows 信任的代码签名证书，可用 `build.ps1 -SigningCertificateThumbprint <证书指纹>` 签名。构建需要 Windows SDK 的 SignTool，可用 `-SignToolPath` 指定。脚本会先签名 DLL 和可选崩溃追踪工具，再将签名后文件的哈希写入安装器，最后签名安装器。组件以外置文件发布。打包脚本记录签名状态与 SHA-256。

## Code signing policy

[Code signing policy and current application status](docs/CODE-SIGNING.md) · [Privacy policy](docs/PRIVACY.md)

已于 2026-11-18 提交 SignPath Foundation 免费开源签名申请，正在等待审核，当前文件仍未签名。自动构建用于公开验证源码和产物，不会自动请求签名或发布正式版本。

默认构建包括 DualSense 输入、震动与内置 PS／Xbox 图标切换。图标在运行时绘制。需要自定义样式时，可以用自己有权使用的原版与 PS 图标 GT1G 容器生成本地头文件，然后用 `build.ps1 -IconHeader <路径>` 构建。两套容器的文件名为 `02771.GT1G0600` 和 `02777.GT1G0600`。

```powershell
python tools/build_icon_header.py .\icons-original .\icons-ps .\local-icons.h
pwsh -File .\build.ps1 -IconHeader .\local-icons.h
```

仓库提供源码、动作规则、参数和构建脚本。运行时使用用户安装的游戏资源。本项目源码采用 MIT 许可证，MinHook 及 HDE 的许可证随 `vendor/minhook` 提供。

## English

This Windows mod adds configurable afterimages for five Ryu costumes, controller rumble, and direct DualSense input to Ninja Gaiden Sigma. Each feature can be installed, updated, and restored separately.

Download the ZIP from Releases, exit the game, and run `NGS1RumblePatch.exe`. Choose **English** in the upper-right corner. Select the game EXE and install the features you want. You can then start the game normally.

This branch is the unsigned 0.6.27 native-plugin test. Known 1.0.0.2/1.0.0.4 recognition profiles remain, but this round runtime-tested the known 1.0.0.2 EXE and the current Chinese-localized 1.0.0.4 EXE. The independent original 1.0.0.4 full installation was not available for a fresh launch test. The game loads the plugin normally, with no resident injector or new startup entry. Installing DualSense also enables automatic PlayStation/Xbox icons. Build with PowerShell 7 and x64 LLVM/MinGW.

The Afterimages tab offers Classic-style, Sigma 2-style, and Ninja Gaiden 2-style materials, with separate glow strength, color, and transparency settings for all five costumes. All three support 200% glow strength. The Sigma 2- and Ninja Gaiden 2-style materials also support 200% surface brightness. Walls occlude afterimages where scene depth is available. Integrated Graphics mode and Intel adapters skip scene depth copies by default, which can affect wall occlusion. After updating the runtime, **Save and Apply Material** reloads the appearance in a running game.

The installer lists the CPU and graphics adapters. By default, Auto selects a render profile based on the adapter used by the game. Discrete GPUs draw the full trail with full-resolution masks. Integrated GPUs draw up to two poses with half-resolution masks. Choose **Standard** or **Integrated Graphics** and click **Apply Profile** to override the setting during play.

After an installation error or game crash, select the affected game EXE and choose **Export Diagnostics**. The ZIP contains recent installer and runtime logs and a summary of Windows crash events for the selected game. For repeatable crashes, start the game using **Trace Game Crash** to include available stack frames and module addresses in the ZIP.
## Native plugin loading test

The game imports the DLL and starts its worker on the first input API call, outside DllMain.
The installer reads five verified external components and has no executable payload resources.
External-process injection and autostart creation are removed. Legacy cleanup can only stop
and remove owned old entries. Runtime features and optional diagnostics remain.
See `release/SECURITY-REVIEW.txt` and `release/TEST-RESULTS.txt` for behavior and verification limits.
