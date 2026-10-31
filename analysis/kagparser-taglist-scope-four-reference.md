# KAGParser 的 taglist 输出边界

## 问题与取证范围

默认 KAGParser 的返回字典被 Web 实现无条件附加了可枚举的 `taglist`。
部分脚本会通过 `Array.assign(dictionary)` 枚举全部属性，并把未知属性解释为
资源名；因此这项扩展元数据会成为错误的图片请求。应恢复原生解析器种类的
输出边界，而不是隐藏字典成员、过滤游戏脚本或吞掉资源加载错误。

本轮修改 C++ 前，重新打开并核对了以下四个 1.3.9 参考及配套 IDB。
文件来自 workspace 的共享 `fixture/kirikiroid2-1.3.9/binaries/`，不入本仓库。
所有反编译调用显式指定所属 database，以下 GetNextTag 均完整分页至 done。

| 参考文件 | database | 默认 GetNextTag | Ex GetNextTag | Exb GetNextTag | Ext GetNextTag |
|---|---|---|---|---|---|
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | 39469ebb | sub_A1FDA0 / 0xa1fda0 | sub_550E54 / 0x550e54 | sub_56231C / 0x56231c | sub_6490F8 / 0x6490f8 |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 80cf4557 | sub_769F00 / 0x769f00 | sub_4AE2B4 / 0x4ae2b4 | sub_4B8DD8 / 0x4b8dd8 | sub_5481AC / 0x5481ac |
| Kirikiroid2_1.3.9_iOS_arm64 | c514c3f4 | sub_10003451C / 0x10003451c | sub_10009F1EC / 0x10009f1ec | sub_100371BA4 / 0x100371ba4 | sub_10034A750 / 0x10034a750 |
| Kirikiroid2_1.3.9_iOS_armv7 | fb45a045 | sub_328D4 / 0x328d4 | sub_9DD9C / 0x9dd9c | sub_374668 / 0x374668 | sub_34D7A4 / 0x34d7a4 |

默认类由 UTF-16 `getNextTag` 绑定、实例工厂及构造器中的
`src/core/utils/KAGParser.cpp` 字面路径交叉定位，未沿用 Ex 的旧地址。
Ex、Exb、Ext 是另外三组实现，不能因为脚本类名相同而混用。

| 参考 | 默认类注册 / getNextTag wrapper / 构造器 | Ex taglist helper | Exb taglist helper |
|---|---|---|---|
| Android arm64 | 0xa2447c / 0xa24e40 / 0xa1a0fc | 0x54f818 | 0x569368 |
| Android armv7 | 0x76c7fc / 0x76cf94 / 0x766a1c | 0x4ad62c | 0x4bc920 |
| iOS arm64 | 0x1000376c8 / 0x100037f6c / 0x10002ff54 | 0x10009e2f8 | 0x1003766a8 |
| iOS armv7 | 0x358f0 / 0x36318 / 0x2e590 | 0x9cd30 | 0x37970c |

四份 UTF-16LE `taglist\0` 均经原始字节读取确认，错误的单字符标注已修正。
Android arm64 的字面量合并在 0x14bfc24；另外三份 Ex / Exb 分别位于
Android armv7 0x4ad714 / 0x4bca08，iOS arm64 0x101958b78 / 0x10197a598，
iOS armv7 0x174aedc / 0x176c94a。对这些地址的全部交叉引用仅落入各自的
Ex / Exb helper；默认与 Ext 的完整返回路径也独立核对。

## 四份共同伪代码与差异

```cpp
// 默认 / Ext：字符、换行、interrupt 与普通返回标签
clearCurrentTag();
parseIntoDictionary();
if (tagShouldBeReturned) {
    addRef(dictionary);
    return dictionary;  // 没有附加 taglist
}

// Ex / Exb：在同样的成功返回节点走各自的 helper
variant names(orderedAttributeNames, orderedAttributeNames);
dictionary.PropSetByVS(MEMBERENSURE, "taglist", names, dictionary);
addRef(dictionary);
return dictionary;

// 显式加载插件替换全局类；已经创建的实例保持原来的种类
link("KAGParserEx.dll"):  PreRegist -> createExClass -> global.KAGParser = class;
link("KAGParserExb.dll"): PostRegist -> createExbClass -> global.KAGParser = class;
```

四份 Ex / Exb helper 的 flags 都是 512 (`TJS_MEMBERENSURE`)；没有
`TJS_HIDDENMEMBER`、引擎模式判断或游戏名判断。是否附加字段取决于解析器
实现种类，不能从游戏是否碰巧读取它来选择。正常返回、字符/换行、interrupt
经编译器尾合并出现两到三个 helper call site，条件均保留录制宏、排除层和
标签条件的原有门控；EOF、被排除标签及内部消费标签不因此产生新返回值。

Android / iOS、32 / 64 位在这一输出契约上没有行为差异。PropSetByVS 的
vtable 槽偏移为 32 位 40、64 位 80；variant、指针及对象字段偏移、静态字面量
合并、guard 和尾合并方式有 ABI / 编译器差异，不写入 Web 对象布局。

| 参考 | Ex initializer / 注册回调 | Exb initializer / 注册回调 |
|---|---|---|
| Android arm64 | 0x42cd8c / 0x558c94 | 0x42cdd8 / 0x55a9f8 |
| Android armv7 | 0x2ff250 / 0x4b29c8 | 0x2ff2ac / 0x4b3f20 |
| iOS arm64 | 0x1000a567c / 0x1000a43a0 | 0x100377c9c / 0x10036c2ec |
| iOS armv7 | 0xa4438 / 0xa31b8 | 0x37ac10 / 0x36e76c |

四份 initializer 的模块名均经 UTF-16 字节搜索核对；Ex 写 PreRegist 链，
Exb 写 PostRegist 链。注册回调使用 MEMBERENSURE 覆盖 `global.KAGParser`。
Ex 同时持有原类引用，Exb 预取 Dictionary / Array 方法。Exb 的模块缓存与
默认 / Ex / Ext 的每实例方法所有权不同，具体的数据流和生命周期见下节；
不能只恢复输出开关却让新 Exb 实例继续查询并持有每实例方法。

## 本地实现对照（修改前确定）

1. `AttachTagList` 现有 PropSetByVS 的键、可枚举 flags、variant 与数组身份
   对应 Ex / Exb helper，本身不应改成 hidden 或移除。
2. 默认 `tTJSNI_KAGParser` 增加受保护的输出能力方法，默认关闭；
   `AttachTagList` 在默认及 Ext 实例中不写元数据。现有内部有序名字数组、
   参数宏及存档逻辑保持原来实现，避免把输出修复扩展成宏状态重写。
3. Ex 和 Exb 使用独立 NI factory，在共享扫描器中仅开启这项能力。
   能力属于实例类型，不放入全局开关或保存状态，assign / restore 不改变种类。
4. 两个显式命名插件分别按 PreRegist / PostRegist 安装新的 KAGParser 类。
   采用现有 Ext 的共同绑定方式，不修改默认工厂，不追溯改变旧实例。
5. 原有四个成功返回位置继续调用 `AttachTagList`；所有录制、条件、排除和
   AddRef / Release 路径不移动。新插件编译单元显式加入 NCB source list。

原始反编译、字节、交叉引用及运行验收保存在任务 PFB 的 ignored
`retrom/.pfb/acceptance/analysis/taglist/` 与 `retrom/.pfb/acceptance/game/`。
私有游戏脚本、资源、凭据及原始验收输出不入本仓库。

## Exb 模块方法缓存与实例生命周期

这部分在 C++ 修改前另行完成四参考 fresh 取证。沿用同一批 fixture 和打开的
worker，子任务接管时原生 `idb_open` 返回的会话映射如下；没有关闭数据库。
原始工具输出写在同一 ignored 证据目录的 `exb-cache-four-native.json`。

| 参考文件 | 本次 database | PostRegist | NI 工厂（构造内联） | NI Invalidate | NI 析构 |
|---|---|---|---|---|---|
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | b271af9e | sub_55A9F8 / 0x55a9f8 | sub_5604A8 / 0x5604a8 | sub_56A95C / 0x56a95c | sub_56AA68 / 0x56aa68 |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 029fdd2d | sub_4B3F20 / 0x4b3f20 | sub_4B7B3C / 0x4b7b3c | sub_4BD854 / 0x4bd854 | sub_4BD8A4 / 0x4bd8a4 |
| Kirikiroid2_1.3.9_iOS_arm64 | 5aebc8d9 | sub_10036C2EC / 0x10036c2ec | sub_100370830 / 0x100370830 | sub_100377AD8 / 0x100377ad8 | sub_100377B44 / 0x100377b44 |
| Kirikiroid2_1.3.9_iOS_armv7 | 80fbcf0a | sub_36E76C / 0x36e76c | sub_372E1C / 0x372e1c | sub_37AACA / 0x37aaca | sub_37AB02 / 0x37ab02 |

四份工厂都由注册时类虚表的 NI factory 槽定位，并完整反编译构造、Invalidate、
析构及 deleting destructor。构造只分配私有的字典、数组和解析状态，不查询五个
方法；Invalidate / 析构也不释放模块缓存。armv7 的函数指针低位为 Thumb 标记，
表中使用对齐后的函数地址。

| 参考文件 | Dictionary.clear / assign 全局槽 | Array.clear / assign / push 全局槽 | 成对宏参数复制 helper | 属性写入并追加名字 helper |
|---|---|---|---|---|
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | 0x1ab0bf8 / 0x1ab0c00 | 0x1ab0c08 / 0x1ab0c10 / 0x1ab0c18 | sub_569C9C / 0x569c9c | sub_569F6C / 0x569f6c |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 0x110ee10 / 0x110ee14 | 0x110ee18 / 0x110ee1c / 0x110ee20 | sub_4BCEF4 / 0x4bcef4 | sub_4BD114 / 0x4bd114 |
| Kirikiroid2_1.3.9_iOS_arm64 | 0x101b95e68 / 0x101b95e70 | 0x101b95e78 / 0x101b95e80 / 0x101b95e88 | sub_100376E04 / 0x100376e04 | sub_10037727C / 0x10037727c |
| Kirikiroid2_1.3.9_iOS_armv7 | 0x18a5584 / 0x18a5588 | 0x18a558c / 0x18a5590 / 0x18a5594 | sub_379E70 / 0x379e70 | sub_37A370 / 0x37a370 |

对每个全局槽的完整 `xrefs_to` 均返回 `more=false`。五槽的二进制初值都是零。
四份 GetNextTag 中清除当前字典 / 有序名字数组的调用均从这些全局槽取方法，
以实例私有容器作 `objthis`，不是按实例重新查询。成对复制 helper 使用两个
assign 槽，属性 helper 使用 push 槽；二者也均已在本轮完整反编译。

四份共同伪代码（先于本节对应 C++ 修改确定）：

```cpp
// 模块静态指针，初值为 null，不是每实例所有物。
PostRegist() {
    variant dictionaryClass = executeExpression("Dictionary");
    variant arrayClass = executeExpression("Array");
    try {
        cache.dictClear = checkedPropGet(dictionaryClass, "clear").AsObject();
        cache.dictAssign = checkedPropGet(dictionaryClass, "assign").AsObject();
        cache.arrayClear = checkedPropGet(arrayClass, "clear").AsObject();
        cache.arrayAssign = checkedPropGet(arrayClass, "assign").AsObject();
        cache.arrayPush = checkedPropGet(arrayClass, "push").AsObject();
        // checkedPropGet 只检查 TJS_FAILED；失败抛内部错误。
        // AsObject 对保存的方法各持有一次引用。
    } catch (...) {
        for (method : cache in the order above)
            if (method) method->Release();
        throw;
    }
    // 上述两个类 variant 在获取方法结束后销毁。
    if (globalDispatch) {
        classObject = createExbClass();
        globalDispatch.PropSet(MEMBERENSURE, "KAGParser", classObject);
        release(globalDispatch);
    }
}

ExbInstance() {
    createPrivateDictionariesAndArrays();
    initializeParserState();
    // 不查方法，不对 cache AddRef。
}
ExbGetNextTag() {
    cache.dictClear.FuncCall(0, null, null, null, 0, null, privateDictionary);
    cache.arrayClear.FuncCall(0, null, null, null, 0, null, privateNames);
    // 扫描器 / 宏参数 helper 同样直接使用 cache.assign / cache.push。
}
ExbInvalidateAndDestroy() {
    releasePrivateState();
    // 不释放 cache。
}
```

集中 Release 是注册获取失败后的 catch / rethrow，不能误标成成功路径的模块
析构。Android arm64 在该文件 PostRegist 的 0x55b6c0–0x55b740；iOS arm64
在该文件 EH 区 0x10036cdc8–0x10036ce48；iOS armv7 在该文件
0x36f450–0x36f4fa。Android armv7 的 EH 尾位于该文件
0x4b4690–0x4b476e，IDA 尚未将该尾完整定义为函数：本轮保留了原生 disasm
与 get_bytes，五个判空 / Release 序列及相关 literal pool 的五缓存交叉引用，
没有将不完整反编译伪装成独立析构函数。

四份 initializer 的 callback 终止指针均为零；缓存完整交叉引用也未出现成功
卸载时释放这些方法的路径。因此实现保留成功加载后模块期持有，不新增实例
引用计数或 atexit / Unregist 释放。这里是静态控制流结论，未声称执行过原生卸载。
Android / iOS 的异常展开、guard、STL 私有状态布局与内联方式不同，五方法的
获取顺序、成功条件、引用所有权及消费方式一致。

本地对照（实施前确定）：

1. `KAGParserExb.cpp` 保存五个模块级原始 dispatch 指针。在 PostRegist 中通过
   `TVPExecuteExpression` 取脚本全局两个类，按上面的顺序执行 PropGet / AsObject；
   失败时按同一顺序判空 Release 并重抛，成功后再安装类。
2. `KAGParser.h` 只增加这五指针的内部传递结构和共享扫描器的借用入口。
   `KAGParser.cpp` 的 Exb 工厂选择此入口；不增加 TJS API、持久状态或输出类型。
3. 共享扫描器仍通过现有五个字段调用方法；Exb 仅借用模块指针，构造跳过查找和
   AddRef，Invalidate 跳过五方法的 Release。默认 / Ex / Ext 继续拥有各自原有
   方法引用。assign / restore 不改变这种由实例工厂确定的所有权。
4. 私有字典 / 数组、宏状态和扫描逻辑沿用现有共享实现；本节不声称其全部布局
   与参考实现相同，也不扩展为这些已有机制的重写。

纠正旧注释：`Kirikiroid2_1.3.9_Android_arm64-v8a.so!sub_561EF8` 是调用栈帧
复制构造，其内部 0x561f3c 不能作为 Exb NI 构造证据。真实 NI 工厂是上表的
0x5604a8；当前标签的私有字典 / 名字数组确在该文件实例的 +16 / +24，
但不在错误注释引用的函数内建立。旧 `qword_1AB3C10` / `sub_55A618` 引用也
不适用于本轮 Android arm64 二进制；Array.assign 的准确槽见上表。
触及这些注释时删除旧裸地址，避免继续传播错误映射。

五个方法查询相关 UTF-16LE 字面量已逐文件读取原始字节、设置完整
`unsigned short[N]` 类型并重命名。当前 worker 的 Hex-Rays 已缓存函数仍可能
显示旧单字符常量；重新调用的原始输出与字节证据均保留，未据其旧显示猜键名。
