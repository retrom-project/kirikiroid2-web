# 视频半分辨率解码（lowres）与 Web 平台偏离

## 现象

1920×1080 的 MPEG-1 视频（像素率 > 1400×720×30）在 VideoOverlay
`vomLayer` 模式下只显示在影片图层左上 1/4，其余区域为黑色。

## 四参考映射

均于 2026-11-07 使用原生 IDA MCP 反编译核对。

`CDVDFactoryCodec::CreateVideoCodec`（lowres 判定）：

| 参考文件 | 函数 | 证据 |
| --- | --- | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `sub_932E9C` `0x932e9c` | 引用 `"lowres"` `0x151289c`，阈值浮点 `0x1512898` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `sub_6DF2A4` `0x6df2a4` | 引用 `"lowres"` `0x6df470` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `sub_10024FA1C` `0x10024fa1c` | 阈值浮点 `0x1015d83e8`；`"lowres"` 为内联 SSO 常量 `0x736572776F6C`，值 `"1"`（49） |
| `Kirikiroid2_1.3.9_iOS_armv7` | `sub_25174C` `0x25174c` | 阈值浮点 `0x251b84`；`strcpy(v54, "lowres")` 内联 SSO |

iOS 中其余 `"lowres"` 字符串命中属于 FFmpeg 内部（MJPEG/AVOption），不是此函数。

`VideoPresentLayer::AddVideoPicture`：

| 参考文件 | 函数 |
| --- | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `sub_934A10` `0x934a10` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `sub_6DFC50` `0x6dfc50` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `sub_100341840` `0x100341840`（assert 字符串 `AddVideoPicture` / `KRMovieLayer.cpp:91`） |
| `Kirikiroid2_1.3.9_iOS_armv7` | `sub_34410C` `0x34410c`（同上 assert） |

`VideoPresentLayer::GetFrontBuffer`：Android armv7 `sub_6DFAAC` `0x6dfaac`。

## 共同控制流

```text
CreateVideoCodec:
    if width*height*fpsrate/fpsscale > 1400*720*30:
        options += ("lowres", "1")
    options += ("surfaces", max_buffer_size)

AddVideoPicture(pic):
    w, h = pic.iWidth, pic.iHeight            # 解码后尺寸（lowres 时减半）
    data = TJSAlignedAlloc(w*h*4, 4)
    sws_getCachedContext(ctx, w, h, YUV420P, w, h, RGBA(28), SWS_FAST_BILINEAR)
    sws_scale(...)                            # 不缩放
    ring[...] = {data, w, h, pts}

GetFrontBuffer:
    texture[n]->Update(pic.data, w*4, 0, 0, w, h)   # 只写解码尺寸
```

四个文件在 lowres 判定、阈值和 `AddVideoPicture` 同尺寸转换上一致；
没有未解释差异。本地 `KRMovieLayer.cpp` 与此一致；VideoOverlay 用流的
标称尺寸分配帧纹理并设置图层尺寸，因此参考实现本身也会把 1080p 影片
显示在左上 1/4。

## Web 平台偏离

本仓库在 `__EMSCRIPTEN__` 下不再请求 lowres，以完整分辨率解码。
这是对参考行为的有意偏离，理由：

- 参考的 lowres 是移动端性能启发式（Kodi 源自 `!TARGET_POSIX && !TARGET_WINDOWS`），
  不是内容语义；Web 端目标是正确显示游戏影片。
- 偏离点只有一个编译期条件，`AddVideoPicture`/`GetFrontBuffer`/VideoOverlay
  的数据流和调用链保持与参考一致。
- 测量：原生单线程解码 kamikimi.mpg（1920×1080 MPEG-1，15.7 Mbps）30 秒，
  全分辨率 1.91 s，lowres 1.75 s；MPEG-1 的 lowres 只节省约 9% 解码时间。
- 浏览器实测（2026-11-07，headless Chromium，同一 PFB，cube.mpg 启动动画，
  8 秒窗口内渲染进程全部线程 CPU，各 2 次）：lowres 45–51% 单核，
  全分辨率 53–58% 单核；空闲基线约 10%。两者呈现帧数相同（约 80 帧/6 秒，
  受 Web 主循环 15 FPS 上限约束，影片 30 fps 的其余帧在转换前被丢弃），
  因此全分辨率只增加 RGBA 转换与帧拷贝，约 +7% 单核、相对 +15%，不是 4 倍。
  lowres 解码后再放大回标称尺寸的“降画质”方案，放大转换仍按全尺寸输出，
  预期节省更少，未采用。

验证：PFB 中 cube.mpg（1920×1080，60 fps 标称）启动动画全屏居中显示。
