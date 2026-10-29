# KAGParser 参数宏：四参考对照

本轮在修改 `cpp/core/base/KAGParser.{cpp,h}` 前完成四文件原生 IDA 取证。范围是内置 KAGParser 注册到 `KAGParserEx.cpp` 的参数宏机制；同一镜像另有 Ex/Exb 副本，下面不混用它们的地址或字段。现有 `MacroArgs` 的 `{values Dictionary, ordered names Array}` 栈与参数宏注册表是两种独立状态。

## 目标和完整函数映射

所有镜像和配套 IDB 位于 `reference/binaries/`。四个 database 均通过 health 核对架构和基址。所有主函数、递归展开、属性访问器及生命周期函数已在本轮通过原生 `mcp__idalib__decompile` 显式指定 database 读取；大函数各页读到 `cursor.done`。

| 目标函数 | Android arm64-v8a | Android armabi-v7a | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| 二进制 | `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `Kirikiroid2_1.3.9_iOS_arm64` | `Kirikiroid2_1.3.9_iOS_armv7` |
| GetNextTag | `sub_550E54` / `0x550e54` | `sub_4AE2B4` / `0x4ae2b4` | `sub_10009F1EC` / `0x10009f1ec` | `sub_9DD9C` / `0x9dd9c` |
| 递归属性处理 | `sub_550770` / `0x550770` | `sub_4ADF0C` / `0x4adf0c` | `sub_10009ECFC` / `0x10009ecfc` | `sub_9D8C4` / `0x9d8c4` |
| 当前属性写入及有序名字追加 | 上一函数内联 `0x550a54..0x550aac` | `sub_4AD52C` / `0x4ad52c` | `sub_10009E178` / `0x10009e178` | `sub_9CBE4` / `0x9cbe4` |
| 原生 Array 元素计数 | `sub_989934` / `0x989934` | `sub_71425C` / `0x71425c` | `sub_100158384` / `0x100158384` | `sub_15978C` / `0x15978c` |
| class registration | `sub_556D00` / `0x556d00` | `sub_4B12E4` / `0x4b12e4` | `sub_1000A2ADC` / `0x1000a2adc` | `sub_A17EC` / `0xa17ec` |
| paramMacros getter | `sub_55859C` / `0x55859c` | `sub_4B24D0` / `0x4b24d0` | `sub_1000A3E44` / `0x1000a3e44` | `sub_A2C28` / `0xa2c28` |
| paramMacros setter | `sub_558680` / `0x558680` | `sub_4B256C` / `0x4b256c` | `sub_1000A3F00` / `0x1000a3f00` | `sub_A2D20` / `0xa2d20` |
| Constructor | `sub_549C84` / `0x549c84` | `sub_4A9E98` / `0x4a9e98` | `sub_10009A18C` / `0x10009a18c` | `sub_98BE8` / `0x98be8` |
| Assign | `sub_54A0E8` / `0x54a0e8` | `sub_4AA110` / `0x4aa110` | `sub_10009A48C` / `0x10009a48c` | `sub_98EB8` / `0x98eb8` |
| Store | `sub_54AA68` / `0x54aa68` | `sub_4AA718` / `0x4aa718` | `sub_10009A7C8` / `0x10009a7c8` | `sub_99220` / `0x99220` |
| Restore | `sub_54BF60` / `0x54bf60` | `sub_4AB54C` / `0x4ab54c` | `sub_10009BA98` / `0x10009ba98` | `sub_9A440` / `0x9a440` |
| Invalidate | `sub_549F08` / `0x549f08` | `sub_4AA04C` / `0x4aa04c` | `sub_10009A360` / `0x10009a360` | `sub_98E00` / `0x98e00` |

四个参数宏 append 回调分别是 Android arm64 `qword_1AAF7E0`、Android armv7 `dword_110E2C4`、iOS arm64 `qword_102516470`、iOS armv7 `dword_2142688`。反查初始化：Android arm64 的独立序言 `0x5495d0` 被 IDA 合并到 `sub_549530`（普通 decompile 只返回前一个析构函数），以本轮 `disasm` 的独立序言、`0x549950..0x5499f4` 读取方法与写入全局来补足；其余分别完整反编译 `0x4a9ab4`、`0x100099d38`、`0x986c4`。四者读取的 UTF-16 key 均是 `push`，不是 `add`。同一缓存用于单名字和双元素追加。

关键 UTF-16 常量通过 `get_bytes` 验证后修正类型并重新反编译：`pmacro`、`erasepmacro`、`paramMacros`、`tagname`、`name`、`count`、`cond` 和 `push`。Android 的 `name` / `count` 有字符串池后缀共享，按完整字面量或精确后缀读取，不按 Hex-Rays 单字符显示猜测。部分已验证 key 在 Hex-Rays 指针别名视图仍显示首字符，判断以原始 UTF-16 字节和调用数据流为准。

## 四者共同伪代码

```text
constructor:
    Macros = null; ParamMacros = null
    Macros = new TJS Dictionary; ParamMacros = new TJS Dictionary

parse attribute(name, value, entity, macroarg):
    only call processAttribute when (!RecordingMacro && ExcludeLevel == -1)
                                   or current special tag is elsif

processAttribute(condition&, output&, name, value, entity, macroarg):
    macro = ParamMacros.PropGet(name)
    if lookup succeeded and macro.Type == Object:
        closure = macro's (Object, ObjThis), with ObjThis fallback to Object
        expanded = closure.PropGet("count")
        count = integer(expanded)
        for i = 0; i < count; i += 2:
            key = string(closure.PropGetByNum(i))
            val = string(closure.PropGetByNum(i+1))
            if val starts '%': strip one char; entity=false; macroarg=true
            else if val starts '&': strip one char; entity=true; macroarg=false
            else: entity=false; macroarg=false
            if processAttribute(condition, expanded, key, val, entity, macroarg):
                DicObj.PropSetByVS(MEMBERENSURE, key, expanded)
                cached Array.push(key) into TagList
        return false // consume trigger attribute; ignore its supplied value
    if entity:
        output = evaluate(value, Owner); stringify output unless void
    else if macroarg and current MacroArgs values dictionary exists:
        split at first '|'; PropGet left part (or whole value) into output
        if '|' exists and output is void: output = right suffix
    else:
        output = value
    if name == "cond":
        condition = bool(evaluate(string(output), Owner)); return false
    return true

special pmacro (kind 15), when active condition and outside excluded block:
    name = string(DicObj.PropGet("name")) // no extra lowercase/empty-name guard
    pairs = new TJS Array
    for each TagList item in source order:
        if getting item or DicObj's corresponding value fails: skip
        if item string equals "name" or "tagname": skip
        cached Array.push(original-name-Variant, value-Variant) into pairs
    ParamMacros.PropSet(MEMBERENSURE, name, object(pairs,pairs))
    release pairs; consume tag

special erasepmacro (kind 16), under same active gate:
    name = string(DicObj.PropGet("name"))
    if ParamMacros.DeleteMember(name) fails: throw UnknownMacroName(name)
    consume tag

assign(source):
    Dictionary.assign(source.Macros) into destination.Macros
    Dictionary.assign(source.ParamMacros) into destination.ParamMacros
store:
    create separate saved "macros" Dictionary; Dictionary.assign(Macros)
    create separate saved "paramMacros" Dictionary; Dictionary.assign(ParamMacros)
restore:
    clear temporary Variant; read "paramMacros"
    if not void: Dictionary.assign into existing ParamMacros
    if void: retain existing ParamMacros
invalidate:
    separately Release Macros and ParamMacros when nonnull (do not zero fields)
public paramMacros:
    getter returns object(ParamMacros,ParamMacros); setter returns ACCESSDENIED
```

注册表查询先于 `&` / `%` 的求值。展开值在使用时解释前缀，嵌套参数宏递归执行，后写属性覆盖先写属性，同时名字 Array 保留写入顺序。递归各层复用该层读取 count 的 Variant 作输出。条件属性会更新同一个 condition 并被消费，不能当普通参数输出。对象读取保留 TJS closure 的 `ObjThis`，没有 Array 类型强制检查；对象为 null 时沿 TJS closure 的 null-access 错误路径。参考未增加递归深度上限、循环检测、奇数 pair 长度拒绝或缺失参数强制成功。本轮不增加这些额外语义。

`ProcessSpecialTags=false` 时参数宏定义/删除与其他特殊标签一样作为普通标签。录制普通宏时保留标签文本，直到展开执行才产生参数宏定义。现有 `*` 参数转发仍是有序直接复制，不经过新的递归参数宏查询。Store/Assign 的 Dictionary.assign 是浅拷贝：注册表 Dictionary 对象独立，内部有序 pair Array 的引用保留，不能以复制整个 registry 指针代替。

## 逐文件差异

- Android arm64：ParamMacros 字段在 parser `+40`，Macros `+32`；属性写入与一部分 Variant 转换/引用计数内联。计数 helper 展开 Android TJS Array 内部 deque 的 size，KAG 仍通过原生 Array API 读取；不把 deque 算式抄到 KAG。
- Android armabi-v7a：ParamMacros `+20`、Macros `+16`；属性写入调用独立 helper。32 位 vtable 槽偏移和引用计数屏障不同；Array size 同为 deque 展开。
- iOS arm64：字段同 64 位 Android；属性写入通过 helper，Array 计数 helper 读取原生 Array 实例 size 字段。字符串/Variant 清理较多独立函数调用。
- iOS armv7：字段同 32 位 Android；Array 计数同 iOS arm64 结构。部分 Hex-Rays 回调原型漏形参，实际调用寄存器/栈与另外三者的 PropGetByNum 和 FuncCall 一致；保留浮点寄存器的序言是 ABI 产物。
- 四者在上述参数宏条件分支、默认值、列表顺序、对象 closure、错误/生命周期上未发现语义分歧。Android/iOS Array 底层容器差异属于各镜像的引擎内部实现，本轮使用 TJS Array，不在 KAG 中替换为 STL 容器。

## 本地逐步对照及修改范围

修改前已读取整个类的字段、构造/清理/Assign/Store/Restore、特殊标签分派、普通属性和 `*` 分支；并换 `paramMacros` / `MacroArgs` / 大小写等搜索交叉核实。现有宏参数 pair 栈、有序 TagList 和保存恢复已存在，但没有独立 ParamMacros 字典、两个特殊标签、公有属性和递归查询链。不能把已有 MacroArgs 当参数宏机制。

1. 在 `Macros` 邻近增加独立 `ParamMacros` Dictionary；构造、异常清理、Invalidate、Assign 与上述生命周期逐项对应。
2. Store 在 `macros` 之后写新建 Dictionary 的 `paramMacros`，Restore 清临时值后仅非 void 时 assign；不改变现有 MacroArgs/调用栈格式，也不增加开发中 checkpoint 版本或迁移分支。
3. 特殊标签枚举在 return 后增加 15/16，保持既有 ProcessSpecialTags、录制、条件和排除门控；pmacro 从 TagList 与 DicObj 收集 pairs，erasepmacro 使用独立注册表。
4. 把原内联 `&` / `%` / cond 链移入 `ProcessAttribute`，在其前增加 registry 递归链。调用位置和普通属性最终写入路径不变。原有 cond 日志移到对应 helper 分支，只作诊断。
5. 单名字和双元素追加统一使用已有缓存 ArrayPush；移除额外 Array.add 缓存。`Array.add` 在本地只消费一个参数，用它构造 pair 会丢 value；原生只有 Array.push 绑定。
6. 增加只读 `paramMacros` 属性和 getter，直接暴露原注册表字典，保持与四原生一致的对象引用语义。

本轮只修改这两个源码文件及本分析。没有游戏名/ID/资源名分支，也不修改输入脚本。现有真实脚本作为运行输入，由主任务统一构建、运行并验证注册、展开及 store/restore/assign。没有为本轮捏造 fixture。实现后 `git diff --check` 和 `python3 .github/rpg-runtime/verify-source.py` 均通过；按分工未自行构建或重启，实际运行待主任务验证，不能宣称已通过。

原始反编译完整页和 UTF-16 原始读数保留在当前 PFB 忽略的验收目录：`kag-param-macros-lifecycle-native.json`（20 个生命周期函数）、`karigurashi/kag-param-macros-branches-native.json`、`karigurashi/kag-param-macros-native-append.json`、`karigurashi/kag-param-macros-native-count.json` 及四份完整 GetNextTag 文本。私有脚本与资源不入库。
