# LayerExSave 的 copyBlueToAlpha

本记录限定为 `LayerExSave.dll` 的 `Layer.copyBlueToAlpha` 和其图层行指针
辅助函数；不把尚未复原的图像保存方法注册为桩。四个参考文件及其 IDB 均在
本轮经原生 IDA MCP 查询并反编译，UTF-16 方法名由原始字节核对。

| 参考二进制 | 方法注册回调 | copyBlueToAlpha | 行指针辅助函数 | Layer 的 ApplyFont |
|---|---|---|---|---|
| Android arm64-v8a | `sub_5EBF68` / `0x5ebf68` | `sub_5EBFEC` / `0x5ebfec` | `sub_5EAD0C` / `0x5ead0c` | `sub_80C848` / `0x80c848` |
| Android armabi-v7a | `sub_511194` / `0x511194` | `sub_5111C4` / `0x5111c4` | `sub_5107A8` / `0x5107a8` | `sub_634004` / `0x634004` |
| iOS arm64 | `sub_1002534EC` / `0x1002534ec` | `sub_10025351C` / `0x10025351c` | `sub_100252600` / `0x100252600` | `sub_10007E854` / `0x10007e854` |
| iOS armv7 | `sub_254BC2` / `0x254bc2` | `sub_254BFC` / `0x254bfc` | `sub_254032` / `0x254032` | `sub_7BD38` / `0x7bd38` |

每个注册回调将实际方法名附加到 Layer。辅助函数的错误消息 holder 在各
参考的初始化链中指向 `Specify Layer class object`，对应本地
`TVPSpecifyLayer`。ApplyFont 的条件和写入与本地 `GetMainImage()` 内部调用
一致。

## 四份共同控制流

```text
getRows(object, &width, &height, writable):
    layer = null
    if object != null and object.NativeInstanceSupport(Layer class ID, &layer) fails:
        throw TVPSpecifyLayer
    image = layer.GetMainImage()        // applies pending font state
    texture = writable ? image.GetTextureForRender(false, null) : image.GetTexture()
    width = texture.width; height = texture.height
    rows = new byte*[height]
    if writable:
        rows[0] = texture.GetScanLineForWrite(0)
        pitch = texture.GetPitch()
        for y from 1 to height - 1: rows[y] = rows[y - 1] + pitch
    else:
        for y from 0 to height - 1: rows[y] = texture.GetScanLineForRead(y)
    return rows

copyBlueToAlpha(result, count, params, receiver):
    if count < 0: return bad parameter count
    source = getRows(params[0].AsObjectNoAddRef(), ..., false)
    destination = getRows(receiver, ..., true)
    for y in [0, min(source.height, destination.height)):
        for x in [0, min(source.width, destination.width)):
            destination[y][4*x + 3] = source[y][4*x]
    delete[] source; delete[] destination
    return success
```

四文件均未看到此方法调用 Layer.update，也没有为缺失参数、空 Layer 或
零高度可写图像增加单独防护：`count == 0` 仍会访问第一参数，且可写行表
先写 `rows[0]`。这些是参考实现的边界行为，不应假称健壮。Android armv7 和
iOS armv7 的 helper 在取可写 texture 时比较标志是否等于 1，随后以非零判断；
两份 arm64 的可写分支使用最低位判断。该 helper 的本方法调用只传 0 或 1，
这两种实际输入没有差异；不把其他值当成已证实的跨平台契约。

## 修改前本地对照

本地没有 `LayerExSave.dll` 模块或该方法。`layerExBTOA.dll` 中相似名称的
方法处理单张图的左右或上下半部，不能替代两个 Layer 之间的蓝色到 alpha
拷贝。本地 Layer 的 native instance、`GetMainImage()`、texture 读取/写入
scanline 和 pitch 均可直接表达参考调用链；方法只复制重叠区域并释放两个
行指针数组。构建只增加该真实方法和对应模块注册，不伪造图像编码功能。
