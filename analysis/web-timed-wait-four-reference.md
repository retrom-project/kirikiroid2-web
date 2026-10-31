# 线程事件、媒体条件变量与 Web 定时等待

本页记录 2026-10-30 原生 IDA MCP 四参考取证，以及一次 Web
`condition_variable` 终止的静态定位。没有据此改变线程等待、返回码处理或
`chrono` 语义。原生共同实现、标准库差异和尚未证明的根因必须分开。

## 四参考映射

| 参考文件 | 线程事件 WaitFor | 定时等待桥接 | 媒体 DecodeExecute |
| --- | --- | --- | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `sub_A34828` `0xa34828` | 直接 `pthread_cond_timedwait` | `0x96fa54` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `sub_777DA8` `0x777da8` | `0x6d6a20` | `0x7041a4` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `sub_10002EC94` `0x10002ec94` | 毫秒辅助函数 `0x10002ed28` | `0x1002ac6f4` |
| `Kirikiroid2_1.3.9_iOS_armv7` | `sub_2D210` `0x2d210` | 毫秒辅助函数 `0x2d2e0` | `0x2b1dcc` |

四个事件等待包装均完整反编译。共同伪代码：

```text
lock event.mutex
if timeoutMilliseconds != 0:
    condition.wait_for(lock, milliseconds(timeoutMilliseconds))
else:
    condition.wait(lock)
unlock event.mutex
```

四者没有持久 Signaled 字段，也没有谓词循环。包装不使用定时等待的结果。
Android64 直接忽略 pthread 返回值；Android32 的标准库辅助函数按时钟判断
是否超时，没有按 pthread 错误码抛异常。iOS 的最后一层
`condition_variable::__do_timed_wait` 位于外部 libc++ 动态库，四个参考文件
并不包含其实现，因此不能由 Android 推断 iOS 对错误码的处理。

iOS 的辅助函数包含 steady/system 时钟转换、时长溢出饱和处理。Android32
反编译器将立即数 `0xf4240` 的地址偶合显示为字符串；指令确认它是
1,000,000，用于毫秒到纳秒转换，不是字符串输入。DecodeExecute 的限时等待
和零值等待调用用于确认映射；其循环不为上述事件增加持久状态。

本地 `cpp/core/utils/win32/ThreadImpl.cpp` 另有受 mutex 保护的 Signaled
状态、提前消费及谓词等待。这是已存在的差异，不应在没有运行证据时作为
此次终止的根因，也不能把本地行为倒推为原生共同实现。

## condition_variable_any 的双锁交接

| 参考文件 | wait_until 包装 | 下层辅助函数 |
| --- | --- | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `0x937bbc` | 直接 pthread 定时等待 |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `0x6e1740` | `0x6d6a20` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `0x10037fc2c` | `0x10037fd34` → `0x10037fd98` |
| `Kirikiroid2_1.3.9_iOS_armv7` | `0x382c74` | `0x382dc8` → `0x382e14` |

四个包装完整反编译，公共步骤为：

```text
sharedInternalMutex = conditionAny.internalMutex
lock sharedInternalMutex with internal unique_lock
unlock caller's unique_lock
perform timed condition wait using internal unique_lock
unlock internal unique_lock
relock caller's unique_lock
release sharedInternalMutex
```

传给底层 `condition_variable` 的是内部普通 mutex 的 unique_lock，
不是调用者 recursive_mutex 的 unique_lock。内部 shared_ptr 只保护该内部
mutex，不保护条件变量本身、调用者 mutex 或包含它们的媒体对象。调用方必须
确保这些对象比等待线程活得更久；shared_ptr 不能证明整个事件的生命周期安全。

Android64 和 Android32 的结果分类仍是其 libstdc++ 行为；iOS 最后一层仍在
外部 libc++。iOS64 的 `0x10037fe40`、iOS32 的 `0x382ee8` 是内部锁释放
辅助函数，拥有锁标志与实际 mutex 指针分别处理。不能把两把锁混成一把，
也不能把异常路径对“未持有锁”的检查解释成正常调用必然未持有锁。

本地媒体 `cpp/core/movie/ffmpeg/Thread.h` 的 `CEvent` 使用
`condition_variable_any` 和 recursive_mutex；`WaitMSec` 是无谓词等待。
`VideoRenderer.h` 和 `Message.cpp` 也有 condition_variable_any。仅看到
标准库等待栈，不能判断具体是哪一个拥有者。

## 已部署 Web WASM 的独立静态定位

一次发布配置 Web 运行在定时等待中终止。该现场 WASM 的 SHA-256 为
`543597a5fd6ea88170d61f2a4dac559a490b46393c2bc6b0cb7085cb977efa2e`。
后续隔离重链接的 type、function、code 节与现场不同，因此没有把其符号表
套到现场栈。下列结论来自现场 WASM 自身的原始 code/data 节和反汇编。

该文件有 551 个函数 imports；Binaryen 输出的定义编号不包含这些 imports。
浏览器栈的全局函数编号必须减去 551 后才对应 WAT 的定义编号：

| 全局函数编号 | WAT 定义编号 | 独立定位的行为 |
| --- | --- | --- |
| 5521 | 4970 | libc++ `condition_variable::__do_timed_wait` |
| 14418 | 13867 | wait_until 与时钟转换 |
| 4327 | 3776 | 带 Signaled 谓词的线程事件等待 |
| 6589 | 6038 | condition_variable_any 内外两把锁的交接 |
| 1059 | 508 | pthread_cond_timedwait |
| 4594 | 4043 | musl `__timedwait_cp` |

全局 5521 检查传入 unique_lock 的拥有锁标志；为 false 时以错误码 63
构造 system_error。标志为 true 时调用 pthread_cond_timedwait；结果不为 0
且不为 ETIMEDOUT（此 Web ABI 为 73）时，以实际错误码构造 system_error。
其 noexcept 边界使这些抛出进入 terminate。现场 data 段独立还原出的错误
字符串分别为 `condition_variable::timed wait: mutex not locked` 和
`condition_variable timed_wait failed`。

已捕获的栈在全局 14418 处截断，既没有实际错误码，也没有更高调用者。
该函数恰有全局 4327 与 6589 两个直接调用者；两条路径静态构造的内部
unique_lock 拥有标志都正确。因此目前只能证明“定时等待的异常终止链”，
不能判断是哪一条检查、哪个具体等待对象、对象是否失效，不能宣布根因是
未持有锁、时间溢出或某个 pthread 错误码。

## 本次 SDK 核对与修复边界

核对 Emscripten 6.0.9 所带 libc++ 和 musl 的实际源文件：

- `system/lib/libcxx/src/condition_variable.cpp`：
  noexcept 定时等待、拥有锁检查、timespec 转换与错误码处理。
- `system/lib/libcxx/include/__condition_variable/condition_variable.h`：
  安全纳秒转换、绝对时间加法饱和、已过期 deadline 的提前返回。
- `system/lib/libcxx/include/condition_variable`：
  condition_variable_any 内部 shared mutex 和调用者锁交接。
- `system/lib/libc/musl/src/thread/__timedwait.c`、
  `pthread_cond_timedwait.c`、`pthread_mutex_trylock.c`：
  futex 返回归一化、条件等待重新获取 mutex 及其错误传播。

32 位无符号毫秒的最大值乘 1,000,000 仍在 int64 范围内，所检查的正常
当前时间、等待参数和转换没有给出可证明的 chrono 溢出。实际现场全局
4594 也将普通 futex EAGAIN 归一化为 0，不能因为某段旧注释提到 EAGAIN
就把所有此类错误吞掉。重新获取失效 mutex、非法 timespec 和对象生命周期
仍是需要运行证据区分的方向。

后续诊断应保全同一次官方链接的 WASM、符号、完整 stderr、首个错误及调用
对象生命周期；ASan 或额外日志会改变布局和时序，没有复现不等于已修复。
私有游戏内容、运行身份、截图、原始堆数据与完整反汇编留在忽略验收目录，
本页只维护四参考与通用语义。线程创建、分离、等待及销毁链仍需单独四证，
本页不对其是否安全下结论。

