# TVPWindowLayer::SetCursorPos 四文件对照与 Web 鼠标移动补发

`Window.setCursorPos`、`Layer.setCursorPos` 及 `cursorX/cursorY` setter 经
`tTVPLayerManager::SetCursorPos → tTVPDrawDevice::SetCursorPos → tTJSNI_Window::SetCursorPos`
到达平台窗口 `TVPWindowLayer::SetCursorPos(x, y)`。

| 二进制 | 定位方式 | SetCursorPos | 光标淡出辅助 |
|---|---|---|---|
| Android arm64-v8a | 符号 `TVPWindowLayer::SetCursorPos` | 0xAA5140 | 内联 |
| Android armabi-v7a | 符号 `_ZN14TVPWindowLayer12SetCursorPosEii` | 0x7BA764 | sub_7B80CC |
| iOS arm64 | 0.3f 常量 0x10181AB08 → 淡出辅助 sub_100E42820 的三个调用者（onTouchEnded、onTouchCancelled、SetCursorPos）；iTVPWindow 子 vtable（0x101B53DF8）槽 4 thunk sub_100E469AC | sub_100E45994（主 vtable 项 0x101B53C98） | sub_100E42820 |
| iOS armv7 | iTVPWindow 子 vtable（typeinfo 引用 0x186EC2C）槽 4 thunk sub_C54064（this-804） | sub_C530F8 | sub_C500F8 |

## 共同控制流

```
area = this->PrimaryLayerArea
local = Vec2(x, area->getContentSize().height - y)
world = area->convertToWorldSpace(local)
pt = getParent()->convertToNodeSpace(world)
_LastMouseX = (int)pt.x; _LastMouseY = (int)pt.y
if (_mouseCursor) {
    _mouseCursor->setPosition(pt)
    // stopAllActions; setOpacity(255); runAction(Sequence(DelayTime(3.0), FadeOut(0.3)))
    refade()
}
```

差异：Android arm64 内联淡出序列，其余三文件调用独立辅助函数；四文件控制流相同。
四文件均不投递鼠标输入事件。

## Web 平台边界（有意偏离）

Windows 版吉里吉里的 `SetCursorPos` 调用 Win32 `::SetCursorPos`，系统随后向窗口发送
`WM_MOUSEMOVE`，图层收到 onMouseEnter/onMouseMove。移动端 Kirikiroid2 只移动光标图标。
浏览器不能移动真实指针，因此 Web 窗口按 Windows 的语义，在保留参考步骤之后补发一次
`tTVPOnMouseMoveInputEvent(x, y)`。`(x, y)` 与 `onMouseMove` 投递的坐标处于同一
PrimaryLayerArea 空间（`onMouseMove` 用 `(nsp.x, height - nsp.y)`，SetCursorPos 以
`Vec2(x, height - y)` 反算）。

运行证据（RIDDLE JOKER 标题，模拟标准手柄）：游戏的 exgamepad 处理方向输入时调用
`setCursorPos(200, 542..974)`，以 72 为步长在七个菜单按钮之间吸附；未补发前按钮不进入
hover，确认键也不落在吸附的按钮上。
