# tTJSVariantArrayStack::InternalCompact 四文件对照（2026-10-30）

## 函数映射

定位方式：UTF-16LE 消息名 `TJSInternalError` → 静态初始化写入的消息 holder 全局 →
holder 的 xref 中唯一具有“释放多余 array block、清理未用槽、realloc、失败抛
TJSInternalError、重算 Current”结构的函数。

| 二进制 | 函数 | TJSInternalError holder | 状态 |
| --- | --- | --- | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `0x9CD90C` | `0x1AF4560` | 已定位 |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `0x73CCA8` | `0x1143E3C` | 已定位 |
| `Kirikiroid2_1.3.9_iOS_arm64` | `0x100201F94` | `0x102527A70` | 已定位 |
| `Kirikiroid2_1.3.9_iOS_armv7` | `0x1FFE50` | `0x214CD20` | 已定位 |

四个 IDB 中函数已命名为 `tTJSVariantArrayStack_InternalCompact`（二进制无字面名，
按本地类名约定），holder 命名为 `TJSInternalError`（来自字面 UTF-16 消息名）。

## 共同伪代码

```text
OperationDisabledCount++
while NumArraysAllocated > NumArraysUsing:
    NumArraysAllocated--; delete[] Arrays[NumArraysAllocated].Array
if Current: Clear Current->Array[Current->Using .. Current->Allocated)
if NumArraysUsing == 0:
    if Arrays: free(Arrays); Arrays = null
    Current = null
else:
    available = Current && Arrays
    offset = available ? (byte address of Current) - (byte address of Arrays) : 0
    tmp = realloc(Arrays, sizeof(tVariantArray) * NumArraysUsing)
    if tmp: Arrays = tmp
    else if NumArraysUsing > 0: throw TJSInternalError
    Current = (available && Arrays) ? Arrays + offset : null   // 指针运算，offset 再乘元素大小
OperationDisabledCount--
```

## 关键边界：offset 是字节差，被当作元素数重新应用

- Android arm64：`SUB X21, X9, X0` 保存字节差（无移位）；`ADD X9, X8, X21,LSL#4`
  即 `Arrays + 16 * 字节差`。
- Android armv7：`v12 = (char *)Current - Arrays`；`Arrays + 12 * v12`。
- iOS arm64：`v10 = Current - Arrays`（字节）；`Arrays + 16 * v14`。
- iOS armv7：`v14 = (char *)Current - Arrays`；`Arrays + 12 * v14`。

四文件一致；差异只有 `sizeof(tVariantArray)`（64 位 16、32 位 12），属于 ABI 差异。
本地 `offset = (size_t)Current - (size_t)Arrays; Current = Arrays + offset;` 与之逐项对应，
是对原始实现的忠实复刻。当 `NumArraysUsing >= 2` 且 `Current != Arrays` 时，重算后的
`Current` 会越界——这是原始 Kirikiroid2 1.3.9 本身的缺陷，不是 Web 移植引入。

2026-10-30 曾尝试把 offset 改为元素下标，但它既不是平台边界，也没有消除
`table index is out of bounds`（真正原因见
`wavesoundbuffer_jspi_sync_semantics_four_binary_2026-08-30.md` 中 decode thread owner
引用的说明），已回退以保持与参考二进制一致。
