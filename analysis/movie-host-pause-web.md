# Web 宿主暂停与影片播放

## 现象

浏览器宿主暂停游戏时只停止 WASM 主循环（`Module.pauseMainLoop`）。
普通 BGM 的续填依赖主循环，暂停后约 1 秒内停止；影片的 demux /
音频线程独立于主循环，暂停期间影片声音继续播放，时钟也继续前进。

## 参考实现

`tTVPApplication::RegisterActiveEvent` 的注册表与
`BasePlayer::OnDeactive`/`OnActive`（暂停/恢复影片时钟并保存/恢复播放速度）
在本地代码中存在，但本地 `BasePlayer` 析构处的注释已记录：四个参考二进制
中没有任何非空注册的生产者，`m_activeEvents` 只在
`Application::OnActivate`/`OnDeactivate`（cocos 前后台切换）时遍历。
因此参考实现中影片不会随应用失活而暂停。

## Web 平台边界

- `BasePlayer` 构造时（仅 `__EMSCRIPTEN__`）注册活动事件：失活时
  `OnDeactive()` + `Pause()`（速度置 0，音频播放器据此暂停输出），
  激活时 `OnActive()`（恢复时钟和保存的速度）。析构处原有的反注册结束注册。
- `tTVPApplication::NotifyHostActiveEvent`（仅 `__EMSCRIPTEN__`）只遍历注册表，
  不执行 `OnDeactivate` 的 compact、混音器锁定和 `System.onDeactivate` TJS 事件，
  避免宿主暂停改变游戏脚本可见的状态。
- 导出 `krkr2_host_set_paused(int)`。retrom-runtime 在暂停后的主循环已排空时调用
  `1`，在恢复调度下一帧前调用 `0`，每次暂停只调用一次（重复的 `OnDeactive`
  会把速度 0 保存为原速度，导致恢复后影片仍停住）。
- 同一导出在影片之后调用 `alcDevicePauseSOFT`（Emscripten ALC_SOFT_pause_device：
  挂起 AudioContext 并停止调度定时器），恢复时先 `alcDeviceResumeSOFT` 再恢复影片。
  已缓冲完的 BGM 由声音线程续填，不依赖主循环，仅停主循环无法让它静音。
- `krkr2_install_web_audio_resume` 的手势解锁在 `AL.paused` 时不恢复 AudioContext，
  避免暂停期间的按键/点击把宿主暂停的音频重新打开。

非 Web 构建的行为与参考一致，不注册、不暂停设备。
