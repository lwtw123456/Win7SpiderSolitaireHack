# Win7 Spider Solitaire Hack

一个面向学习与研究的 **Windows 7 x64 蜘蛛纸牌逆向工程项目**。

项目通过 `version.dll` 代理加载进入游戏进程，并综合演示特征码扫描、运行时内存修改、字节补丁、指针链、Inline Hook、Mid Hook、系统 DLL 导出转发以及原生 Win32 控制界面等技术。

> [!IMPORTANT]
> 本项目仅用于逆向工程、Windows 程序分析与 C/C++ 开发学习。请仅在自己拥有或获准分析的软件环境中使用，不要将相关技术用于联网游戏、破坏公平性、规避安全机制或其他未授权行为。

## 功能概览

| 功能 | 说明 | 实现方式 |
| --- | --- | --- |
| 自由移牌 | 解除牌组移动限制 | 特征码扫描 + 字节补丁 |
| 自由选牌 | 解除可选牌条件 | 特征码扫描 + NOP 补丁 |
| 明牌开局 | 发牌时将牌局中的纸牌切换为正面显示 | Mid Hook + 游戏内部函数调用 |
| 有序开局 | 发牌时按照牌面点数重新排列牌组 | Mid Hook + 指针数组排序 |
| 不加移牌数 | 移牌后不增加操作次数 | 特征码扫描 + NOP 补丁 |
| 移牌不扣分 | 移牌后不减少当前分数 | 特征码扫描 + NOP 补丁 |
| 满 13 张即收 | 一列达到 13 张时自动收牌 | Inline Hook |
| 直接获胜 | 直接获取当前游戏的胜利 | 模块基址 + 指针链写入 |
| 数值修改 | 修改当前分数和移牌数，并同步界面文本 | 指针链写入 + 窗口消息 |

控制窗口关闭时，项目会卸载 Hook、恢复原始字节并清理已安装的 Patch。

## 环境要求

- Windows 7 x64 蜘蛛纸牌，目标进程必须为 **64 位**。
- 64 位 Windows 开发环境。
- CMake 3.20 或更高版本。
- 支持 C++23 的编译器。
- 推荐使用 Visual Studio 2022 和 MSVC x64 工具链。

> [!NOTE]
> 本项目只支持 x64。CMake 会拒绝非 Windows 平台和 32 位工具链。

## 获取依赖

仓库中的第三方源码被打包在 `3rdparty.zip` 中。首次构建前，需要将其解压到项目根目录下的 `3rdparty` 文件夹：

```text
Win7SpiderSolitaireHack/
├─ 3rdparty/
│  ├─ safetyhook.cpp
│  ├─ safetyhook.hpp
│  ├─ Zydis.c
│  └─ Zydis.h
├─ CMakeLists.txt
├─ SpiderSolitaireHack.cpp
└─ ...
```

## 编译

### Visual Studio 2022

在项目根目录打开 x64 Native Tools Command Prompt，执行：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

多配置生成器的输出通常位于：

```text
build\bin\Release\version.dll
```

### Ninja 或其他单配置生成器

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

输出通常位于：

```text
build\bin\version.dll
```

## 使用方法

1. 确认目标是 Windows 7 自带的 **64 位蜘蛛纸牌**，并先退出游戏。
2. 备份游戏目录及重要文件。
3. 将编译得到的 `version.dll` 复制到 `SpiderSolitaire.exe` 所在目录。
4. 启动蜘蛛纸牌。
5. DLL 加载后会等待游戏主窗口出现，随后打开“蜘蛛纸牌九项修改器”控制窗口。
6. 进入牌局后再使用依赖游戏状态指针的功能，例如修改分数、修改移牌数或直接获胜。

### 卸载

退出游戏后，从游戏目录删除自定义 `version.dll` 即可。

## 项目结构

```text
.
├─ CMakeLists.txt                 # CMake 构建配置
├─ SpiderSolitaireHack.h          
├─ SpiderSolitaireHack.cpp        # Patch、Hook、指针链及功能实现
├─ memoryeditor.hpp               # 内存读写、特征码扫描和 Patch 管理
├─ safetyhook_manager.hpp         # SafetyHook 的封装
├─ simple_controller.h
├─ simple_controller.cpp          # Win32 控制窗口
├─ version_x64.c                  # version.dll 代理与入口逻辑
├─ version_x64_jump.asm           # MSVC/MASM x64 跳转桩
├─ version_x64_jump.S             # GNU 汇编 x64 跳转桩
├─ version.def                    # DLL 导出定义
├─ 3rdparty.zip                   # SafetyHook 与 Zydis 源码包
└─ LICENSE
```

## 关键代码入口

阅读源码时可以按照以下顺序：

1. `version_x64.c`：理解代理 DLL 如何加载原版系统 DLL，以及控制线程何时启动。
2. `simple_controller.cpp`：查看界面控件如何映射到各项修改功能。
3. `SpiderSolitaireHack.h`：了解对外暴露的功能接口和错误结果。
4. `SpiderSolitaireHack.cpp`：分析特征码、Patch、Hook 和指针链的具体实现。
5. `memoryeditor.hpp`：理解底层内存访问、模式扫描与原始字节恢复。
6. `safetyhook_manager.hpp`：理解 Hook 生命周期和 SafetyHook 封装。

## 第三方组件

本项目使用或包含以下第三方项目的源码：

- **SafetyHook**：用于 Inline Hook 与 Mid Hook。
- **Zydis**：用于 x86/x64 指令解码。
- **AheadLibEx 生成代码**：用于构建 `version.dll` 代理与导出转发框架。

使用、分发或修改第三方组件时，请同时遵守其各自的许可证与版权声明。

## License

本项目采用 [MIT License](LICENSE)。
