# layeredwindow.dll 的 Web 呈现边界

这是新增的 Web 平台插件边界，不是声称四个移动参考中已有该插件。
原生参考和现有的 Window、模态循环、Layer 输入路由均不修改。

## 本轮四参考映射

2026-11-04 通过原生 IDA MCP 分别打开并核对以下四份配套 IDB 的
module、input_path、imagebase 和 Hex-Rays 状态。逐文件进行完整
`find_bytes`（`layeredwindow` ASCII、UTF-16LE、`LayeredWindow` UTF-16LE）
及 `find_regex(layered.?window|UpdateLayeredWindow)`，结果均为零、游标完成。

| 二进制 | layeredwindow 注册 / 实现状态 | 本轮证据 |
| --- | --- | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | 缺失，无可映射地址 | 三组字节搜索及字符串搜索均完成、零命中 |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | 缺失，无可映射地址 | 三组字节搜索及字符串搜索均完成、零命中 |
| `Kirikiroid2_1.3.9_iOS_arm64` | 缺失，无可映射地址 | 三组字节搜索及字符串搜索均完成、零命中 |
| `Kirikiroid2_1.3.9_iOS_armv7` | 缺失，无可映射地址 | 三组字节搜索及字符串搜索均完成、零命中 |

四者共同结论仅为：没有找到这个插件的入口、字面注册键或 Win32 API；
不能据此制造 Window 的布尔属性或声称已经还原了移动端该功能。
四者没有可比较的插件控制流，平台/ABI 差异不适用于缺失入口。
本地的模块加载链 `Plugins.link -> TVPLoadPlugin -> ncbAutoRegister::LoadModule`
也没有提供该模块；加载失败的返回值未传播，因而直到脚本调用全局函数才报错。

## 实际 Windows 插件合同

独立提供的 `layeredwindow.dll` 为 PE32，67072 字节，SHA-256
`35cddee234e55c49fdb50a36a8ca9fd3d8e227ddcfa8c94512777aedba8c73c1`。
本轮以原生 IDA MCP 对它的导出、UTF-16 字节、字符串交叉引用、虚表、导入、
反编译及反汇编共同取证。原始记录及私有运行证据留在忽略的验收目录。

| 所属二进制 | 函数或注册站点 | 地址与证据 |
| --- | --- | --- |
| `layeredwindow.dll` | `V2Link` | `0x10001470`，注册全局 `layeredwindow`、`is2kover` |
| `layeredwindow.dll` | `tLayeredWindowFunction` 虚表 | `0x1000c14c`，FuncCall 指向 `0x10001030` |
| `layeredwindow.dll` | `tLayeredWindowFunction::FuncCall`（虚表/RTTI 确认） | `0x10001030`，五参数、DIB 像素循环、`UpdateLayeredWindow` |
| `layeredwindow.dll` | `tIs2kOverFunction` 虚表 | `0x1000c1cc`，FuncCall 指向 `0x10001380` |
| `layeredwindow.dll` | `tIs2kOverFunction::FuncCall`（虚表/RTTI 确认） | `0x10001380`，`GetVersionExA` 后返回 S_OK，不写 result |

注册名是独立的全局函数，不是 `Window.layeredwindow` 属性，也不属于
`windowEx.dll`。Windows 实现的公共伪代码为：

```text
V2Link:
    import TJS exports; get script global
    publish dispatch objects as layeredwindow and is2kover
    release temporary dispatch references

layeredwindow(hwnd, pixels, pitch, width, height):
    convert all five variants using tjs_int conversion
    allocate positive-height, 32-bit BI_RGB DIB(width, height)
    for each input row (pixels + y*pitch):
        for each BGRA pixel:
            if alpha == 0: write zero BGRA
            else if alpha == 255: copy pixel
            else: write each RGB channel * alpha / 255, retain alpha
    acquire window DC, memory DC, selected DIB
    read current window rectangle
    ensure WS_EX_LAYERED style
    UpdateLayeredWindow(hwnd, rectangle, DIB, origin=(0,0),
                        sourceConstantAlpha=255, AC_SRC_ALPHA, ULW_ALPHA)
    restore/release GDI objects; return S_OK without writing result

is2kover:
    initialize OSVERSIONINFOA size; call GetVersionExA
    return S_OK without writing result
```

输入行顺序没有再翻转；正高度 Windows DIB 把第一个输入行作为底行。
反汇编验证部分 alpha 的乘法及有符号常数除法确为整数除以 255；
Hex-Rays 对部分低字节运算的输出不作为相反颜色公式的依据。
Windows 插件只检查参数数目至少为 1 却读取五个参数，这是其不安全边界，
不能在 Wasm 中复制越界读取。Web 入口要求五个参数，并根据实际 Wasm
线性内存大小验证整段源像素、带符号 stride、正尺寸及输出分配范围。

## 本地实现对照与不可避免的差异

浏览器不能加载 PE DLL，也没有 HWND、GDI DC 或 `UpdateLayeredWindow`。
仅 Web 构建注册此虚拟模块，使用现有 HWND getter 返回的 Window native
instance 身份，在窗口列表中匹配而不解引用未经确认的句柄。

`LayeredWindowWeb.cpp` 对应以下步骤：

1. `NCB_REGISTER_FUNCTION` 在显式加载模块时发布两个实际注册名。
2. 读取五个整数参数，检查当前窗口和源内存范围；不引入游戏或脚本规则。
3. 按 DIB 的底向上行语义生成顶向下 RGBA 快照，逐像素保留上述 alpha
   分支和整数预乘步骤；不保留脚本缓冲区的借用指针。
4. 将快照上传为 Cocos Texture2D，以 premultiplied-alpha Sprite 呈现。
   Sprite 由现有窗口 primary area 持有并随该窗口销毁，更新时替换纹理。
   首次调用隐藏同一 primary area 原有的合成输出，防止普通窗口绘制覆盖
   专用的 layered bitmap；脚本 Layer 及其按钮命中、事件保持原状。
5. 两个入口均不写返回 variant；保留调用方确认、取消及模态生命周期。

Windows 为桌面独立窗口更新屏幕矩形；现有移动/Web 窗口模型则只把当前
窗口 fit 到画布，切换时将前窗移到视口外。此扩展复用该模型，不实现桌面
窗口叠加、屏幕定位或另一个浏览器窗口。Sprite 位于现有 primary area
左上角，沿现有 primary area、paint box、canvas 变换呈现，输入使用同一
坐标链。透明像素仍保持透明，但不会恢复已经被现有窗口管理器移出的前窗。

这条路径使用实际输入的现有 fixture 验收；不生成游戏 fixture，不修改游戏
脚本、确认开关或存档。未加载此模块的窗口不增加任何呈现对象或行为。

## Closing-window repaint

Fresh four-reference ASCII/UTF-16 and independent string searches again found
no layeredwindow module in any mobile binary; the complete searches are kept in
the ignored PFB evidence `takeover-plugin-evidence.json`. Fresh decompilation of
the supplied `layeredwindow.dll!tLayeredWindowFunction::FuncCall@0x10001030`
confirmed that the return values of GetWindowRect, GetWindowLongA,
SetWindowLongA and UpdateLayeredWindow do not select an error return. After
releasing its GDI objects the function returns TJS_S_OK, including failed native
window updates; it never writes a result variant.

The real dialog's No button closes the window, then its immediate mouse-leave
handler repaints using the same HWND. The Web window registry then contains
only the main window. Rejecting the retired handle as TJS_E_INVALIDPARAM turns
this otherwise harmless repaint into a fatal script exception. The Web mapping
must therefore return TJS_S_OK when no live window/form matches, without
dereferencing the retired handle or reading its pixel buffer. Parameter-count
and live-window linear-memory checks remain errors. This follows the supplied
Windows plugin's failure behavior rather than inventing a new script lifecycle.
