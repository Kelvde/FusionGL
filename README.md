# FusionGL — ZL2 双内核渲染器插件（研究版）

一个面向 Zalith Launcher 2 / Pojav 系启动器的外部渲染器插件骨架。
在插件内按启动的游戏自动选择 GL 内核：

- 现代 GL 栈（1.17+ / lwjgl3ify / Angelica）→ MobileGL DirectVulkan
- 传统 1.7.10（无 lwjgl3ify）→ NG-GL4ES

分派层不依赖 JNI，版本判定通过读取启动器写下的 `zalith-game.cfg` 与版本清单完成；
GL/EGL 符号由 AArch64 三指令尾跳转桩（`adrp` / `ldr` / `br`）转发到已加载内核，
稳态无锁、无哈希、无栈帧。

## 状态与验证

2026-11-15，OPPO PHZ110（天玑 9300 / Mali-G720-Immortalis MC12 / Android 16）：

- GTNH 2.9.0（Java 17~25 / lwjgl3ify 3.0.42 / Angelica）**成功进入游戏并加载世界**。
  分派日志显示 `FORCE MODERN` → `MobileGL G720-tuned DirectVulkan`，
  游戏日志显示 `Preferred shader profile: ANGELICA_CORE`，随后正常保存各维度区块。
- 此前 4 次该版本崩溃均为 `Angelica requires an OpenGL 3.3 core profile context`，
  原因是 GL4ES 系内核只报 `GL 3.1 / core profile: false`。
- GregTech Leisure（1.20.1 Forge）可进入游戏，走 DirectVulkan 内核。
- 纯原版 1.7.10 走 NG-GL4ES 可进入游戏。

**这不是一个完成品。** 已知限制：

-对比AI构建的无改动的MGL，实测帧率约低 0%到10%(这是我给AI说的，实际体感我感受不出来，没时间去测了)。主要怀疑点是本构建
  未启用上游的 `MOBILEGL_ENABLE_LTO`（缺 ThinLTO、`-fno-plt`、
  `-fno-semantic-interposition`、`--icf=safe`），以及编译自 `dev` 分支而非 26.11 tag。
- `VK_EXT_vertex_attribute_divisor` 在 G720 上不可用，instancing 会退化；
  `gl_ClipDistance` 需模拟；multiViewport 不支持。这些是上游共同瓶颈。
- 只在单一设备、单一 GPU 上验证过，**不能推断为兼容所有设备**。

## 许可

本项目组合了多种许可证的组件，详见 `LICENSE` 与 `NOTICE.md`。

核心结论：MobileGL 及其修改版为 **GNU LGPL v3.0**（`licenses/COPYING` +
`licenses/COPYING.LESSER` 为全文），NG-GL4ES 为 **MIT**，其余依赖为
Apache-2.0 / BSD-2-Clause / BSL-1.0 / Khronos 系列许可。

本仓库分发的是**我们编写的源码与构建脚本**，不含：

- 任何编译产物 `.so` 或 APK
- 官方 MobileGL 插件 APK 中的 `classes.dex` / `resources.arsc` / `AndroidManifest.xml`
- 签名密钥（`fusion.keystore` 等）
- 任何游戏本体、整合包或用户数据

## 构建（概要）

完整步骤见 `src/` 内脚本注释。需要的外部条件：Android NDK r27c、CMake 3.22.1、
Ninja、Android SDK build-tools（zipalign / apksigner）、JDK 17。

1. 从上游取得 MobileGL（`dev` 分支）与 NG-GL4ES（`main`）源码，补齐 `.gitmodules`
   中的子模块，用 NDK 交叉编译出两个内核 `.so`。
2. 用 `src/gen_trampolines.py` 从内核导出符号并集生成 `trampolines.S` /
   `sym_names.c`（本仓库已含生成结果）。
3. 用 `src/dispatch.c` 与上述两个文件编译分派层，SONAME 设为 `libFusionGL.so`。
4. `src/repack_v3.py` 负责把库写入插件包、替换包名与显示名，并保证
   `resources.arsc` 为 STORED + 4 字节对齐（Android 11+ 安装要求）。
5. zipalign 后用 apksigner 做 v2/v3 签名。

`src/repack_v3.py` 需要一个插件 APK 作为基底；请从官方渠道自行获取，
本仓库不提供该二进制。
