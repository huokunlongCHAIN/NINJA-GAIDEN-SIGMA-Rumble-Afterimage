# Ninja Gaiden Sigma Enhancements

《忍者龙剑传 Σ1》PC 功能补丁，提供五套服装的动作残影、手柄震动和 DualSense 输入。三项可独立安装、更新和还原。兼容已验证的 1.0.0.2 与 1.0.0.4 构建，包括已测试的 @imjinjia 1.0.0.4 汉化版。

[下载安装包 / Download the installer](https://github.com/huokunlongCHAIN/ngs1-enhancements/releases/latest)

## 安装与调整

退出游戏，打开 `NGS1RumblePatch.exe`，选择游戏 EXE 并安装所需功能。兼容模式会为当前 Windows 用户安装自动加载器。安装后可直接打开游戏 EXE。

右上角可即时切换简体中文和 English，安装器会记住上次选择。安装包附有中英文使用说明。

每套服装可单独保存残影亮度、颜色和透明度。默认黑衣、蓝色传说忍者服和杀戮凶手额外提供肌肉高亮调节，默认 45%。保存后游戏自动重载。0.6.3 为残影轮廓加入转角柔化，并沿用游戏的原始服装网格。

## 从源码构建

使用 Windows x64、PowerShell 7、带 MinGW 运行库的 x64 LLVM 工具链和 Windows .NET Framework 4.x。将 `clang++.exe` 所在目录加入 `PATH`，或用 `-ClangDirectory` 指定它。MinHook 源码随仓库提供。

```powershell
pwsh -File .\build.ps1 -RunRenderTests
pwsh -File .\package.ps1
```

安装器生成在 `release/NGS1RumblePatch.exe`。`-RunRenderTests` 会运行 D3D11 WARP 渲染测试和动作时序测试。安装器集成测试另外接受临时目录、上一版安装器和用于检查的本地游戏 EXE 路径。

默认构建包括 DualSense 输入、震动与内置 PS／Xbox 图标切换。图标在运行时绘制。需要自定义样式时，可以用自己有权使用的原版与 PS 图标 GT1G 容器生成本地头文件，然后用 `build.ps1 -IconHeader <路径>` 构建。两套容器的文件名为 `02771.GT1G0600` 和 `02777.GT1G0600`。

```powershell
python tools/build_icon_header.py .\icons-original .\icons-ps .\local-icons.h
pwsh -File .\build.ps1 -IconHeader .\local-icons.h
```

仓库提供源码、动作规则、参数和构建脚本。运行时使用用户安装的游戏资源。本项目源码采用 MIT 许可证，MinHook 及 HDE 的许可证随 `vendor/minhook` 提供。

## English

This Windows mod adds configurable afterimages for five Ryu costumes, controller rumble, and direct DualSense input to Ninja Gaiden Sigma. Each feature can be installed, updated, and restored separately.

Download the ZIP from Releases, exit the game, and run `NGS1RumblePatch.exe`. Choose **English** in the upper-right corner. Select the game EXE and install the features you want. You can then start the game normally.

The public package supports verified 1.0.0.2 and 1.0.0.4 builds, including the tested 1.0.0.4 Chinese localization. Installing DualSense support also enables automatic PlayStation/Xbox button icons. The source build commands above use PowerShell 7 and x64 LLVM with MinGW.
