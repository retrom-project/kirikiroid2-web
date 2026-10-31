# tTVPThread 创建、等待与音频解码线程销毁

2026-10-30 使用原生 IDA MCP 对四份参考逐一完整反编译。本页记录已确认的
生命周期差异；没有据此认定某次 Web 条件变量终止已经找到根因。对应的定时
等待静态证据见 [Web 定时等待](web-timed-wait-four-reference.md)。

## 四参考函数映射

| 参考文件 | 构造 | StartProc | WaitFor | 基类非删除析构 | 音频 DecodeThread 析构 |
| --- | --- | --- | --- | --- | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `0xa3430c` | `0xa344ac` | `0xa34624` | `0xa345bc` | `0x96f998` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `0x777a44` | `0x777b6c` | `0x777c38` | `0x777be8` | `0x70411c` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `0x10002e798` | `0x10002e938` | `0x10002eadc` | `0x10002e9d0` / `0x10002ea14` | `0x1002ac63c` |
| `Kirikiroid2_1.3.9_iOS_armv7` | `0x2ce24` | `0x2d034` | `0x2d13e` | `0x2d094` / `0x2d0be` | `0x2b1c9c` |

构造函数内的 ThreadImpl.cpp 路径、同一基类虚表、保存的 pthread handle、
传给 pthread_create 的 StartProc 及 WaitFor 调用者共同确认映射。音频
DecodeThread 虚表 Execute 槽对应各自 `0x96fa54`、`0x7041a4`、
`0x1002ac6f4`、`0x2b1dcc`；不能仅凭地址邻近确认析构。

## 四者共同的控制流

```text
Thread.constructor(suspended):
    initialize suspension mutex and condition
    terminated = false
    this.suspended = suspended
    pthread_attr_init(default attributes)
    pthread_create(&handle, &attributes, StartProc, this)
    pthread_attr_destroy(attributes)
    // constructor does not set detached state or call pthread_detach

Thread.StartProc(this):
    if suspended:
        lock suspension mutex
        condition.wait(lock)
        unlock suspension mutex
    this.Execute()
    invoke each registered thread-exit callback
    return null

Thread.WaitFor():
    pthread_join(handle, &returnValue)

Thread.destructor():
    destroy suspension condition and mutex
    // does not join or detach

DecodeThread.destructor():
    set decode priority
    running = false
    terminated = true
    Resume()
    Event.Set()
    WaitFor()
    destroy own event and one-loop lock
    Thread.destructor()
```

四份原生实现使用 pthread handle，并不包含本地 std::thread 的所有权对象。
构造使用默认 pthread 属性，未设置 detachstate；四份 StartProc 包装的
Execute 后只迭代退出回调，没有包装自身的 detach 步骤。退出回调本身是
间接调用，单凭这些包装不能排除任意回调可能执行的操作；但四份 DecodeThread
析构都明确调用可连接线程的 pthread_join，再释放同步对象。这些正向调用
证据支持“显式等待完成后销毁”的生命周期，不能把 detached std::thread
解释为其等价复原。

Android64、Android32 的 mutex 实现与 iOS 不同；iOS 的基类析构显式调用
mutex 和 condition_variable 析构，Android 的部分 mutex 析构被内联/消除。
ABI 偏移、线程 handle 和栈保护差异不改变 create → run → join → destroy
的顺序。Android32 对两个终止字段的存储顺序与另外三份略有不同，应保留为
编译差异而非推断额外状态机。

## Resume 与 Event.Set 的区别

| 参考文件 | Resume | 事件 Set |
| --- | --- | --- |
| Android arm64-v8a（文件同上） | `0xa347d4` | `0xa347e4` |
| Android armabi-v7a（文件同上） | `0x777d48` | `0x777d54` |
| iOS arm64（文件同上） | `0x10002ec54` | `0x10002ec60` |
| iOS armv7（文件同上） | `0x2d1e4` | `0x2d1f0` |

Resume 清除 suspended 并通知基类的 suspension condition。
Event.Set 锁定解码事件自己的 mutex、通知解码事件 condition，再解锁。
它们不是同一个通知；仅 Resume 不能唤醒 Execute 内的 Event.WaitFor(0)。
四个 DecodeThread 析构都保留两次通知，随后才 join。

## 2026-10-30 运行证据与已实施的修复

Release 优化 + 可恢复 ASan 诊断构建在 COCORO 实际 Review Run 中报告
`heap-use-after-free`（READ 4 字节，位于 176 字节对象内偏移 12）：

- 读取方：解码线程 T41，`__thread_proxy`（tTVPThread StartProc）→
  `condition_variable::wait` → `pthread_cond_timedwait` → `pthread_mutex_lock`，
  即 Execute 中 `Event.WaitFor(0)` 等待的成员同步对象；
- 释放方：主线程 `tTJSNI_WaveSoundBuffer::Invalidate()` → 删除
  `tTVPWaveSoundBufferDecodeThread`；
- 分配方：`tTJSNI_WaveSoundBuffer` 构造创建 DecodeThread，同一位置创建 T41。

该现场与下文的两处本地偏差完全一致：detach 使 `WaitFor()` 不等待，析构又未
`Event.Set()`，于是对象在线程仍阻塞于其成员条件变量时被释放；随后唤醒写入
已复用的堆块，与此前 `emscripten_builtin_malloc` 内的越界陷阱相符。

已按四参考恢复：构造使用默认属性 `pthread_create`（不 detach），`WaitFor`
为 `pthread_join`；DecodeThread 析构恢复 `Event.Set()` 后再等待。StartProc 与
Resume 已与四参考一致（标志检查后单次 wait、Resume 不加锁只通知），未改动。
本轮四参考反编译确认：DecodeThread 析构（各 `0x96f998`、`0x70411c`、
`0x1002ac63c`、`0x2b1c9c`）、构造、WaitFor、StartProc、Resume 与上表一致。

其余子类析构均已在 WaitFor 前唤醒自身线程（Event.Set/ExitRequest 等），恢复
真实 join 不改变它们的停止条件。

## 本地对照（修复前记录）

`cpp/core/utils/win32/ThreadImpl.cpp` 当前构造立即 `Handle.detach()`，
而 `WaitFor()` 只在 `Handle.joinable()` 时 join。正常构造后 joinable
为 false，使调用方以为已等待、实际却可以继续释放该线程使用的对象。

`cpp/core/sound/win32/WaveImpl.cpp` 当前 DecodeThread 析构的 Event.Set
被注释。该线程 Execute 仍可在 Event.WaitFor(0) 中无限等待。因此只删除
detach 而不恢复完整停止/通知/等待链，会把提前销毁变成等待不结束。

这些差异均在固定上游基线之前已存在：detach 来自 `2d0f333fb`，
Event.Set 注释来自 `0888676fe`。提交历史只能说明引入时间，不能证明
此次偶发终止与它们的因果关系。

Web 音频路径另有异步 FillL2Buffer 完成回调。修复前还必须核对销毁线程是否
可能阻塞主线程，同时等待必须回到主线程完成的回调；不能用恢复 join 掩盖
这一平台边界，也不能吞掉 std::condition_variable 的错误维持运行。后续
需要真实运行中的线程/拥有者生命周期证据。没有 ASan 报告或一次未复现均
不足以宣称生命周期缺陷已排除或已修复。本次只记录分析，未改变上述源码。


## 2026-10-31 挂起启动的丢失唤醒（有意偏离）

四参考 StartProc / Resume（Android arm64 0xA344AC / 0xA347D4，Android armv7 0x777B6C / 0x777D48，iOS arm64 0x10002E938 / 0x10002EC54，iOS armv7 0x2D034 / 0x2D1E4）完全一致：

```
StartProc: if (Suspended) { lock(mutex); cond.wait(lock); unlock }   // 锁外读取，无谓词
           Execute(); 退出回调
Resume:    Suspended = false; cond.notify_one();                      // 不持锁
```

若 Resume 落在线程读取 Suspended 与进入 wait 之间，通知丢失，线程永不执行，恢复真实 pthread_join 后析构的 WaitFor 将永久等待。这是四参考共有缺陷，不是本地复原偏差。经用户确认的修复方针（同 PartialCopy），本地改为：StartProc 持锁以 `!Suspended` 谓词等待；Resume 在同一把锁内清除标志后 notify。线程创建、Execute 与退出回调顺序不变。
