# tTVPRenderMethod_DirectCopy 四文件对照（"Copy" 软件渲染方法）

定位方式：各二进制软件 RenderManager 注册函数中 `RegisterRenderMethod("Copy", &method)` 的静态对象 vtable 末项即 `DoRender`；其 normal 分支构造 `std::function<void(int)>` 并交给 `TVPExecThreadTask`，lambda 调用体内联/调用 `PartialCopy`。

| 二进制 | 注册函数 | DoRender | lambda 调用体 | PartialCopy |
|---|---|---|---|---|
| Android arm64-v8a | sub_849B04（"Copy" 对象 qword_1ADBEB0，vtable 0x1A24C70） | 0x851D68 | 0x85215C（内联 PartialCopy） | 内联于 0x85215C |
| Android armabi-v7a | sub_657D40（"Copy" 对象 dword_113590C，vtable 0x10C0054） | 0x65DE28 | 0x65E174 | 0x65E22E |
| iOS arm64 | sub_1003257E8（"Copy" 对象 qword_101B056B8，vtable 0x101B07378） | 0x10032DFFC | 0x10032E550（lambda vtable 0x101B073F0） | 0x10032E5B8 |
| iOS armv7 | 由 DoRender 断言字符串 xref 定位 | 0x332A44 | 0x332FEA（lambda vtable 0x1845FEC） | 0x33305A |

## 共同控制流

```
pixelsize = (_tar->GetFormat() == 1 /*Gray*/) ? 1 : 4
if rcsrc.top > rcsrc.bottom:            // UDFlip
    逐行 memcpy(w * pixelsize)，8bpp/32bpp 分别按字节/uint32 偏移
elif rcsrc.left > rcsrc.right:          // LRFlip
    逐行 memcpy(w * pixelsize) + TVPReverse8/TVPReverse32
else:
    (iOS 两个文件带 assert "w == rctar.get_width() && h == rctar.get_height()")
    backwardCopy = (_tar == _src && rctar.top > rcsrc.top)
    taskNum = _src == _tar ? 1 : (w*h >= 33000 ? GetAdaptiveThreadNum : 1)
    TVPExecThreadTask(taskNum, lambda{ h, taskNum, this, _tar, rctar, _src, rcsrc, w, backwardCopy })
PartialCopy(dst, dx, dy, src, sx, sy, w, h, backwardCopy):
    w *= 4                              // 无 pixelsize 参数，四文件均固定 4
    逐行 memmove(dst->GetScanLineForWrite(dy+y) + 4*dx,
                 src->GetScanLineForRead(sy+y) + 4*sx, w)
```

差异：Android 两文件为 Release（无 assert），iOS 两文件保留 assert；Android arm64 的 PartialCopy 内联进 lambda 调用体，其余三文件为独立函数。lambda 捕获列表四文件一致，均不包含 pixelsize。

## 8bpp 调用方：tTVPBaseBitmap::CopyRect 四文件对照

| 二进制 | tTVPBaseBitmap::CopyRect |
|---|---|
| Android arm64-v8a | 0x7F86FC |
| Android armabi-v7a | 0x628E1C |
| iOS arm64 | 0x100406414 |
| iOS armv7 | 0x3EE068 |

四文件一致：`if(!Is32BPP()) plane = 3`，plane 3 取 `GetRenderMethod("Copy")`，经 `OperateRect` 进入上面的 DirectCopy normal 分支。即 8bpp（如 Layer 的 province image，`tTJSNI_BaseLayer::CopyRect` 的 dfProvince 分支）同样按 4 字节/像素拷贝，读写都会越过位图末尾，最多 3×pitch 字节。

位图分配与释放（Android arm64：Alloc 0xA71AE0，Free 0xA72070）与本地一致：`allocbytes = size + 44`，`Bits+size` 处写尾哨兵，Free 校验两个哨兵，不一致即抛出 overrun 异常。参考实现没有为该越界预留空间，原生平台能否“存活”取决于系统分配器的尾部余量。

## 结论与有意偏离（2026-10-31）

- 运行证据：COCORO 打开存档画面时，脚本 `Layer.copyRect` 复制 province image（源 Gray 59×465 pitch 64，目标 Gray 177×465 pitch 192）。ASan 报告 `READ of size 236`（59 px × 4）越过源位图末尾；目标末行写入 236 字节，越过 89280 字节的像素区，覆盖尾哨兵和下一个 dlmalloc 块头，之后 Release 版在无关的 `malloc`（Array.split 的 deque 扩容）中 trap 或主线程挂起。
- 对照实验（Release）：仅保留其它修复、PartialCopy 维持参考行为时，打开存档画面后页面挂起；PartialCopy 按目标像素大小拷贝后，存档画面、写入存档槽、返回和继续剧情均正常。
- 这是四个参考二进制共有的缺陷，不是本地复原偏差。经用户确认，本地 `PartialCopy` 有意偏离参考实现：按目标纹理像素大小（Gray 为 1，其余为 4）计算行宽和 x 偏移，32bpp 路径的结果与参考完全相同。源码在该函数注释中标明偏离原因。
