# STP Viewer

**面向 Mac 和 Windows 的中文、离线 STEP 工程查看器。**

> **V1.1.00** 改善 Excel 装配树、全局随机着色、单面着色和三维全屏，修复选面后点击空白崩溃，并增加全屏抓手。移除开发分支中未完善的客户外包络实验模块。自 1.0 以来合入的位置编辑、整装 STEP 另存、自定义剖切和背景设置也包含在本版中。详见 [V1.1.00 更新说明](doc/release-v1.1.00.md)、[功能说明](doc/bom-color-fullscreen.md) 和 [验证范围](VALIDATION.md)。

STP Viewer 用于打开 `.stp` / `.step` 文件，查看三维零件与装配、提取工程信息、测量精确几何，并导出 Excel BOM 或拆分零件。文件在本机处理，应用不上传模型、不调用云服务，也不修改源 STEP 文件。

项目基于 [Mayo v0.10.0](https://github.com/fougue/mayo/tree/v0.10.0)，使用 Qt、Open CASCADE Technology 和 libxlsxwriter。V1.1.00 在 1.0 的离线工程查看与测量功能上继续扩展装配编辑、导出和三维观察。

![STP Viewer 的测量操作页与多条三维结果](doc/screenshots/measurement.png)

## 主要功能

- **三维查看**：旋转、平移、缩放、标准视图、透视／正交、装配树、隔离、隐藏、透明、剖切与显示精度切换。
- **装配探索**：按总装或子装配爆炸；抓手拖动单个零件实例，松开后保留显示位置，支持复位和每文件独立状态。
- **零件位置编辑**：工具栏“移动 / 旋转”打开右侧“位置编辑”页；支持按 mm 平移、按角度旋转、预览、应用、撤销及重做，再通过“另存 STEP”保存整个装配。
- **背景颜色**：视图菜单切换浅色渐变、白色、浅灰、深灰或自定义颜色，设置在下次启动时保留。
- **模型着色**：全局随机着色按 BOM 名称统一配色，优先提高相邻异名零件的色差；支持选中零件或单个面着色、撤销/重做，并通过整装 STEP 另存保留新颜色。
- **三维全屏**：视口右下角四角图标切换模型全屏，进入/退出带 180 ms 扩展/收缩动画；全屏抓手位于退出按钮左侧，退出后恢复布局并保留临时摆放。
- **工程信息**：读取文件中可获得的名称、编号、实例数量、材料和密度，计算包围盒、面积、有效体积及质量；缺失材料与密度可在当前会话补充。
- **独立测量**：点坐标、长度、半径、直径、面积、体积、质量、质量中心和体积中心。体积中心独立于密度；质量和质量中心要求有效密度。
- **对象之间测量**：点、边、面、实体、零件及装配汇总的最小距离，提供最近点和 ΔX／ΔY／ΔZ；按切线、法线或特征轴测量 0–90° 小夹角。
- **圆心、轴线与无限拓展**：拾取圆／圆弧圆心及圆／圆柱／圆锥参考轴；A、B 可分别将解析直线边或平面面无限拓展。轴线天然无限，显示长度不限制计算。
- **连续测量记录**：成功测量自动追加，操作页与记录页分开；支持复制、单条删除、隐藏标注和清除所有测量。每个文件标签独立保留会话记录。
- **Excel 与 STEP 导出**：四表 `.xlsx` 默认打开可展开/收拢的装配树，保留层级序号及完整实例路径；按勾选范围拆分叶级零件并附带清单和诊断。Excel 与拆分导出使用导入数据；整装“另存 STEP”保存已应用的位置和颜色编辑。
- **PMI 查看**：显示已识别的源图形与可验证的基础语义标注，并列出不支持、关联失败或显示失败的原因。

轴线和无限拓展测量的三维标签省略 M 编号与无穷符号，记录表保留编号。默认单位为 mm、mm²、mm³、kg、度，显示三位小数，可调整至六位。默认使用原始装配位置，也可测量包含爆炸和抓手位移的当前显示位置。

## 下载与平台

[V1.1.00 Release](https://github.com/Xiaoyang899/STPViewer/releases/tag/v1.1.00) 提供以下成品；下载后可使用随附的 `SHA256SUMS.txt` 核对文件。

| 平台 | 下载与启动 | 当前验证边界 |
|---|---|---|
| macOS Apple Silicon | [arm64 ZIP](https://github.com/Xiaoyang899/STPViewer/releases/download/v1.1.00/STPViewer-1.1.00-macos-arm64.zip)，完整解压后打开 `STPViewer.app` | 功能基线覆盖 Apple Silicon Mac 本机与 Retina；每个发布包以随附验证报告为准 |
| Windows x64 | [单文件 EXE](https://github.com/Xiaoyang899/STPViewer/releases/download/v1.1.00/STPViewer-1.1.00-windows-x64-portable.exe) 或 [完整目录 ZIP](https://github.com/Xiaoyang899/STPViewer/releases/download/v1.1.00/STPViewer-1.1.00-windows-x64.zip) | 当前功能回归在 Windows 11 ARM64 VMware 中运行 x64 程序，属于仿真验证，不等同于原生 x64 硬件验收 |

另提供 [源码 ZIP](https://github.com/Xiaoyang899/STPViewer/releases/download/v1.1.00/STPViewer-1.1.00-source.zip) 和 [SHA-256 校验汇总](https://github.com/Xiaoyang899/STPViewer/releases/download/v1.1.00/SHA256SUMS.txt)。Mac／Windows 的具体构建、交互与便携包验证结果以同次发布的报告为准，历史 1.0 平台记录不自动继承为 V1.1.00 的验收结果，详见 [验证说明](VALIDATION.md)。

Windows 单文件 EXE 可独立复制。首次启动将内嵌、校验后的运行库展开至 `%LOCALAPPDATA%\STPViewer\Portable\<包哈希>`，后续校验并复用缓存；无需管理员权限，不联网。它使用 .NET Framework 4.x 启动器，运行时使用展开后的 Qt／OCCT DLL 和资源。

Mac 应用使用本地 ad-hoc 签名，尚未做 Apple 公证；Windows 发行程序尚未做 Authenticode 签名。平台实测、自动回归及其他系统／显卡环境的验证范围见 [验证说明](VALIDATION.md)。本仓库提供完整源码及构建入口。

## 快速使用

1. 打开应用，点击“打开 STP”或拖入一个或多个 STEP 文件。
2. 在装配树或三维视图选择对象；右侧查看工程信息与 PMI，底部使用爆炸、显示和剖切操作。
3. 点击“测量”，选择“测量对象”或“测量之间”，分别拾取所需对象，再点击“测量”。结果会自动保留。
4. 在“测量记录”页复制、隐藏、删除或清除结果；通过顶部按钮导出 Excel BOM 或拆分 STEP。

默认左键拖动旋转，右键拖动平移，滚轮缩放；双击或按 F 适配窗口。抓手与测量拾取互斥。Windows 多选使用 Ctrl，Mac 使用 ⌘。测量、人工属性与显示位置只保留于文件会话，关闭或重新读取文件后清空。

## 从源码构建

固定依赖为 Qt **6.8.3**、OCCT **7.9.3**、libxlsxwriter **1.2.4**；版本与下载校验见 [依赖锁定文件](scripts/dependencies.lock.json)。依赖下载和构建首次需要联网，生成的应用在本机离线运行。

### macOS arm64

需要 Apple Silicon Mac、Xcode Command Line Tools 和 Python 3。

```sh
bash scripts/bootstrap-macos.sh --jobs 6
export PATH="$PWD/.tools/venv/bin:$PATH"
cmake -C .deps/paths.cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 -DSTP_BUILD_TESTS=ON
cmake --build build --target STPViewer stp_core_tests stp_measurement_tests stp_pmi_tests stp_job_queue_tests stp_placement_tests stp_transform_panel_tests stp_section_color_tests stp_bom_outline_tests stp_color_edit_tests --parallel 6
ctest --test-dir build --output-on-failure
bash scripts/package-macos.sh build/STPViewer.app
```

### Windows x64

需要 Visual Studio 2022 C++ Build Tools、Windows SDK 和 Python 3.12+。双击 `build-windows.cmd`，或在普通 PowerShell 运行：

```powershell
./scripts/build-windows.ps1 -Jobs 4
```

入口初始化编译器、准备固定版本依赖、运行回归并生成 ZIP 和单文件 EXE。界面与打包自检需要可用图形桌面和 OpenGL。详细配置见 [Windows 构建说明](WINDOWS_BUILD.md)及[使用与构建说明](STP_README.md)。GitHub Actions 提供手动触发的双平台便携构建。

## 文档与样例

- [V1.1.00 更新说明](doc/release-v1.1.00.md)
- [完整使用说明](STP_README.md)
- [Excel 装配树、模型着色与三维全屏](doc/bom-color-fullscreen.md)（含外包络移除及保存范围）
- [工程测量说明](doc/stp_mac_measurement.md)（保留原文件名，Mac／Windows 均适用）
- [Windows 测量与单文件 EXE](doc/stp_windows_measurement.md)
- [抓手摆放说明](doc/stp_mac_part_grab.md)
- [样例及来源](samples/README.md)
- [测试与已知边界](VALIDATION.md)
- [参与开发](CONTRIBUTING.md)

`measurement-reference.step` 可体验半径、直径、圆心、轴线和中心测量；`measurement-extension.step` 可对比有界几何与无限直线／平面的距离。质量测量前需提供有效密度。

## 使用边界

PMI 支持取决于源 STEP 内容与 OCCT 的读取结果，识别数量不代表全量保真。拆分导出中的缺失或语义变化会写入诊断。几何 BOM 反映文件的定义与实例组织，不能自动恢复企业正式 EBOM。

体积及中心面向有效封闭实体；装配按实例累加，不做布尔融合、不扣除重叠体积。超大型真实工程装配、最低系统版本与全部 CAD 导出组合仍需进一步验证。测量记录保留于会话；未提供跨文件测量、CSV 导出或手动坐标输入。

## 许可与致谢

源码沿用 [BSD-2-Clause 许可](LICENSE.txt)，保留 Mayo／Fougue SAS 的原始版权声明。Qt、OCCT 和其他第三方组件保留各自许可，详见 [第三方声明](THIRD_PARTY_NOTICES.md)。Mayo 原始介绍保存在 [README-Mayo.md](README-Mayo.md)。

应用署名为 **Powered by AEME**。问题与建议可通过 GitHub Issues 提交，应用内保留反馈邮箱 **xiaoyang8992@gmail.com**。
