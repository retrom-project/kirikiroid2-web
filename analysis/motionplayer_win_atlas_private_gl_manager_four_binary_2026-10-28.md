# Win atlas 的私有 OpenGL manager：四参考修复依据

日期：2026-10-28。本文在修改 C++ 之前记录四文件共同证据和本地对照。

## 问题与证据范围

Web 默认软件 renderer 下，现有 spec=2 产品样本在首次 Emote 绘制时抛出
`RuntimeError: null function`。故障对象仍有有效的构造数据和引用计数，RTTI 是
`tTVPSoftwareTexture2D_static`；OpenGL `OperateTriangles` 却将它当成
`tTVPOGLTexture2D`，在 `SyncPixel` 对应的虚表位置读到 0。像素指针已追溯到
`Player::findSourceForNode` 内 Win atlas 的创建参数。问题在 atlas 的创建 receiver，
不是脚本调用空函数、游戏包损坏或 atlas 缓存提前释放。

输入是用户提供的 Kirikiroid2 1.3.9 四个二进制及其配套 IDB；已核实全部输入的
大小、SHA-256、可读性和数据库对应关系。四个数据库使用独立会话，对下表函数均在
本轮调用原生 `mcp__idalib__decompile`，每次显式指定所属 `database`。
原始结果保存在 PFB 的忽略目录，不提交二进制或私有产品内容。

## 四文件函数映射

全部函数均已定位并取得本轮伪代码；表内地址只能用于其所在文件。

| 参考二进制 | database | findSourceForNode | Win miss 调用点 | 私有 getter | 按名查 manager |
|---|---|---|---|---|---|
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `0462d366` | `sub_691CC8 / 0x691CC8` | `0x692314` | `sub_6930E4 / 0x6930E4` | `sub_848784 / 0x848784` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `ae23abbc` | `0x570500` | `0x5707DA` | `0x570EA0` | `0x657140` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `388de7cc` | `0x1000F316C` | `0x1000F3588` | `0x1000F3D90` | `0x100323D00` |
| `Kirikiroid2_1.3.9_iOS_armv7` | `ba0ae266` | `sub_EF97C / 0xEF97C` | `0xEFD98` | `sub_F0834 / 0xF0834` | `sub_329116 / 0x329116` |

四个 getter 都是带 C++ 静态 guard 的 raw manager 缓存，初始化时构造完整 UTF-16
`opengl` 名称并执行按名查找。字符串均已通过原生 `get_bytes` 核实，不能把 IDA
截断显示的 `o` 当作真实名称。iOS armv7 的字符串位于 `0x174F794`，14 字节为
`6f00 7000 6500 6e00 6700 6c00 0000`，已修正类型后重新反编译 getter。

按名查找使用共享 renderer registry；entry 的 manager 为空时调用注册工厂，初始化
method cache 并保存 instance，非空时直接返回。名称未注册就抛异常，不回退软件
manager。Motion 的“私有”指 getter 的静态缓存，不表示每个 Player 新建 GL manager。

## 四者共同伪代码与分支

```text
privateGL():
    static manager = namedRenderManager(ttstr("opengl"))
    return manager

findSourceForNode(...):
    preserve existing blank/source-group gates and spec dispatch
    if spec == 2 and loaded module exists and source group lookup succeeds:
        if group texture is cached:
            borrow cached texture
        else:
            strictly read texture.truncated_width/height; discard converted values
            strictly read width, height, type and pixel resource
            pitch = width << 2                       // 32-bit arithmetic
            bgra = alignedAlloc(pitch * height, 4)
            if type == "RGBA8":
                reverseRGB(bgra, pixel, signedSize / 4)
            else if type == "A8L8":
                if signedSize >= 1:
                    expand [alpha,luma] pairs to [luma,luma,luma,alpha]
            else:
                alignedFree(bgra)
                throw unsupportedTextureFormat(type)
            texture = privateGL().CreateTexture2D(
                bgra, pitch, width, height, format=4, flags=1)
            alignedFree(bgra)
            slot = winSourceTextures[liveGroupKey]
            if slot != texture:
                release previous slot if non-null
                assign texture; retain if non-null
            texture.Release()                       // unconditional, normal path
            borrow texture in sourceState
        preserve icon metadata and partial-publication order
    else:
        preserve spec-1 and ordinary source fallback
```

Cache hits do not allocate or retain again. Width/height do not default to truncated values.
A8L8 retains the native signed-size gate, including a final paired read for a positive odd
size. The factory receiver is unconditional on renderer configuration, texture dimensions,
game identity and compression support; there is no default-manager fallback or null guard.
The miss probe and later `operator[]` use the live key separately. Buffer free occurs before
map insertion. A throw from insertion/old Release does not add a construction-reference
scope guard. The module map owns the retained texture; sourceState only borrows it.

## 每个 ABI 的差异

- Android arm64：GNU STL map/hash-map 模板大量内联，root 共 850 行；manager factory
  位于 vtable `+24`，纹理 refcount 位于对象 `+8`。默认 renderer getter 是独立的
  `0x848834`，配置缺省 `software`；Win 路径调用的是 `0x6930E4`。GL factory 为
  `0xA5A2C4`，消费者 `0xA5B89C` 使用同一 GL manager，纹理 `+152` 为 SyncPixel。
- Android armv7：Thumb/32 位指针，manager factory 位于 vtable `+12`，纹理 refcount
  位于 `+4`；私有缓存为 `0x11119B0`。默认 getter `0x6571B4` 与按名 getter 独立。
  OpenGL factory `0x788968` 与消费者 `0x7895A0`/纹理 `+76` 构成同一创建/消费域。
- iOS arm64：libc++ 容器和异常清理 helper 与 Android 不同；8 字节指针，manager
  factory 位于 `+24`，纹理 refcount 位于 `+8`；私有缓存 `0x101B699E8`。
  OpenGL manager vtable `0x101AFD710 +24` 指向 factory `0x1002E8EF0`。
- iOS armv7：root 完整 421 行，libc++/ARM EH 形状；32 位指针，factory 位于
  `+12`，refcount 位于 `+4`，私有缓存 `0x187D680`，guard `0x187D684`。
  创建后先 free，再查 slot；新引用 retain 和 factory Release 分别在 `0xEFDF8`、
  `0xEFE04`。没有与其余三份不同的 receiver/default 分支。

四端都按 32 位处理 pitch、分配乘积和 source size，而不是按宿主指针宽度扩大。
虚表字节偏移、容器和 EH 差异不改变 manager 选择、创建参数、缓存或 owner 顺序。
纹理 Release 的末次释放可进入现有延迟删除机制；这次修复不改该生命周期。

## 本地逐项对照及拟修改

修改前的 `cpp/plugins/motionplayer/PlayerResource.cpp`：

1. `findWinSourceGroup_guess`：严格 source/非抛 group lookup 和 fallback 保留。
2. `loadWinAtlasTexture_guess` 的 cache find、严格 metadata 读取、W32 buffer 分配和
   RGBA8/A8L8 转换对应上述共同伪代码，保留。
3. 当前第 160 行 `TVPGetRenderManager()->CreateTexture2D(...)` 取默认 manager，
   与四份独立证据不符。改为已有的
   `render_backend_guess::getPrivateOpenGLRenderManager_guess()` receiver。
4. 第 161–164 行的 pixels、pitch、width、height、RGBA、STATIC 参数原样保留。
5. 随后的 free、`operator[]`、`setTexture`、construction Release 和 borrowed 返回原样
   保留，不增加转换缓存或 RAII。
6. 该私有 getter 已被 KRKR atlas 和 GPU batch 使用；它的生产实现已是
   `static TVPGetRenderManager(TJS_W("opengl"))`，不需要再发明 manager 或修改默认值。

这纠正了 2026-08-27 findSource 报告“确认的不匹配只有 diagnostic chain”的过强结论：
该轮未闭合 Win factory receiver。2026-08-30 KRKR 私有 GL 修复仅覆盖 KRKR page loop，
本次把遗漏的 Win producer 恢复为同一个已有 receiver。

## 验证边界（实现前记录）

仓库现有 production-atlas 单元测试使用 KRKR PSB；已同时检查测试源码、资产清单和
atlas 调用链，没有可直接复用的 spec=2 Win 单元 fixture。遵循 CLAUDE.md，不从零
构造 PSB 物料，也不以字符串匹配源码作为回归测试。Win 路径复用用户已有完整产品
样本，通过正常导入、启动、场景绘制、输入、存档和新实例读档验证；另以两个原有
可运行样本回归。

源码检查及 `check-source.sh` 的原生/Web 回归全部通过；通过 fork 的
`build-candidate.sh` 完成 Emscripten 6.0.9 发布构建和 `verify-release.py` 校验。
同一现有样本在未修复 r9 的 Web 输出复现上述 null function；修复候选在审核预览和
普通产品启动均已越过该点，显示双角色 Emote，推进对白后没有运行时异常，并能写入
原生 No.01 存档。Wasm 服务端返回字节已与本次构建 SHA-256 独立比对一致。
完整产品存读档和另两个样本的逐步记录保存于本地 PFB evidence，不提交私有物料。
