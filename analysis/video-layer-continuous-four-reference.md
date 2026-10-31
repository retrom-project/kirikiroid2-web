# VideoPresentLayer 连续显示回调

## 四参考映射

以下 `OnContinuousCallback` 函数均于 2026-10-30 使用原生 IDA MCP 完整
反编译并核对控制流，未截断。

| 参考文件 | 函数 |
| --- | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `sub_934944` `0x934944` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `sub_6DFBA0` `0x6dfba0` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `sub_1003417A0` `0x1003417a0` |
| `Kirikiroid2_1.3.9_iOS_armv7` | `sub_34407C` `0x34407c` |

## 共同控制流与本地对照

```text
if usedPicture == 0:
    return
currentPts = player.GetClock() / DVD_TIME_BASE
lock pictureMutex
picturePts = picture[currentPicture].pts
unlock pictureMutex
if picturePts <= currentPts:
    OnPlayEvent(Update, null)
```

`KRMovieLayer.cpp` 在锁内只复制当前头帧的时间戳，在锁外进行正向 `<=`
比较。传入 tick 未使用；每次最多转发一次 Update，不在此处跳帧或消费
环队列。最初的普通计数读取不加锁，进入锁后也不重新检查计数。

这里不能改成 `if (picturePts > currentPts) return; Update()`：有限有序值
下结果相同，但任一操作数为 NaN 时，原生正向比较不发送 Update，后者却会
发送。比较也应保留在解锁之后。

Android64 的时钟辅助函数为 `sub_92DAE4`，Android32 为 `sub_6DBF10`；
iOS64 为 `sub_100322C7C`，iOS32 为 `sub_327D80`。时钟对象偏移和 mutex
实现随 ABI、标准库不同；四者的时间单位转换、锁内复制、解锁和正向比较
顺序一致。

该谓词及锁边界修正是原生语义还原。窗口处于屏外的 Web 显示问题由
[窗口激活扩展](web-window-activation-four-reference.md)处理，不能把两者
混为同一个根因。
