# Web 窗口激活与画布显示位置

`TVPWindowLayer::BringToFront` 在 Web 上恢复被激活窗口的外层 Cocos
显示位置。这是明确的 Web 兼容扩展，不是四个原生参考中遗漏的还原步骤。
原生分支继续保留共同实现。

## 四参考映射

以下函数均于 2026-10-30 通过原生 IDA MCP 完整反编译，未截断。
Android 以导出符号定位；iOS 通过 `14TVPWindowLayer` RTTI、虚表中的
`iWindowLayer` 槽和调整 `this` 的 thunk 交叉定位。

| 参考文件 | BringToFront | SetVisible | ShowWindowAsModal |
| --- | --- | --- | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `TVPWindowLayer::BringToFront` `0xaa5388` | `0xaa55bc` | `0xaa5424` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `TVPWindowLayer::BringToFront` `0x7ba930` | `0x7baafc` | `0x7ba9b4` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `sub_100E45AFC` `0x100e45afc` | `0x100e45cb4` | `0x100e45b70` |
| `Kirikiroid2_1.3.9_iOS_armv7` | `sub_C532C8` `0xc532c8` | `0xc534aa` | `0xc533ac` |

## 原生共同控制流

```text
BringToFront(this):
    if current != this:
        if current:
            size = current.getViewSize()
            current.setPosition(size.width, 0)
            current.nativeInstance.OnReleaseCapture()
        current = this

SetVisible(this, visible):
    requestedVisible = visible
    this.setVisible(visible)
    if visible:
        this.BringToFront()
    else if current == this:
        current = prev ? prev : next

ShowWindowAsModal(this):
    inModal = true
    setVisible(true)
    BringToFront()
    if console:
        remove console; console = null
        scene.scheduleUpdate()
        Director.getInstance().purgeCachedData()
        controlAd(65538, 0, 0)
    Director.getInstance()  // 返回值未消费
    modalResult = 0
    while current == this and modalResult == 0:
        remain = DrawSceneOnce(30)
        ProcessInputEvents()
        if Application.terminate:
            modalResult = mrCancel
        else if modalResult != 0:
            break
        else if remain > 0:
            sleep_for_ms(remain)
    inModal = false
```

四者都没有在 `BringToFront` 中恢复新窗口的位置，也没有在 modal 循环退出时
自动隐藏或销毁该窗口。不能把复位坐标解释为参考实现中的既有行为。

Android 包含 stack cookie；Android 的 `ProcessInputEvents` 调用落入空函数，
iOS 调用实际处理函数。Android 将等待内联为 `nanosleep` 的秒、纳秒拆分，
iOS 使用 `std::this_thread::sleep_for`。指针宽度、字段偏移及 Cocos 虚表槽
随 ABI 和 Cocos 版本不同；窗口切换的条件、调用顺序及默认结果一致。

## Web 兼容扩展

保留 modal 窗口的脚本可以在返回后重新激活原窗口。原生共同路径先把原窗口
移到视口右侧，再将 `current` 改回它，却没有把它移回画布。软件合成可以
已经生成完整位图、脚本也已继续推进，最终窗口仍处于可见区域之外。

Web 的 `BringToFront` 在共同路径末尾执行 `setPosition(Vec2::ZERO)`，使脚本
激活的窗口回到画布中。该步骤位于 `current != this` 判断之外，也修复
`current` 已指向屏外窗口时的重新激活。扩展只由 `__EMSCRIPTEN__` 选择，
不识别游戏、资源、视频、窗口尺寸或具体调用方。

这只修改外层 Cocos 窗口的显示平移，不修改脚本窗口的逻辑坐标、主 Layer
矩形、图像偏移、paint box 或缩放。现有 `iWindowLayer` 的 `SetLeft`、
`SetTop`、`SetPosition` 是空实现，`GetLeft`、`GetTop` 返回零；该扩展没有
改写这些接口，也不能作为它们已经实现的依据。原引擎仍有窗口管理器菜单，
本扩展不依赖“窗口管理器不存在”的假设。

## 验证边界

诊断必须分开观察解码帧、前缓冲纹理、最终合成位图和完整 Cocos 坐标链；
单凭黑色画布不能判断视频解码失败。记录对象生命周期和 iframe 身份，不能
在场景销毁后继续解释旧地址。

本次集成的私有资产、运行身份、堆地址、截图和测量留在 PFB 的忽略验收目录。
本页只维护通用行为、原生依据和有意的 Web 差异。
