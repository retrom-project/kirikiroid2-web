# ExtKAGParser 宏内 iscript：四参考对照

本轮先完成四参考新鲜原生 IDA 取证，再修改源码。四个 binary / `.i64` 的可读性、架构和基址已重新核对。原始证据保存于当前 PFB 忽略验收目录 `karigurashi/kag-iscript-{native-initial,ext-native,ext-registration-native}.json`，不将私有资源入库。

## 解析器身份与逐文件映射

同一参考镜像包含核心原生 KAGParser、`KAGParserEx.cpp`、`KAGParserExb.cpp`、`ExtKAGParser/ExtKAGParser.cpp` 多组实现；不得因注册类名都为 KAGParser 而混用地址。上一轮参数宏分析的 `0x550e54` 等属于 `KAGParserEx.cpp`，不是 ExtKAGParser。

| 操作 | Android arm64-v8a `.so` | Android armabi-v7a `.so` | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| 完整 binary 文件名 | `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `Kirikiroid2_1.3.9_iOS_arm64` | `Kirikiroid2_1.3.9_iOS_armv7` |
| KAGParserEx GetNextTag | `sub_550E54` / `0x550e54` | `sub_4AE2B4` / `0x4ae2b4` | `sub_10009F1EC` / `0x10009f1ec` | `sub_9DD9C` / `0x9dd9c` |
| Ex SkipCommentOrLabel | `sub_54E780` / `0x54e780` | `sub_4ACAC8` / `0x4acac8` | `sub_10009D478` / `0x10009d478` | `sub_9BE5C` / `0x9be5c` |
| Exb GetNextTag（含内联 Skip） | `sub_56231C` / `0x56231c` | `sub_4B8DD8` / `0x4b8dd8` | `sub_100371BA4` / `0x100371ba4` | `sub_374668` / `0x374668` |
| Ext GetNextTag（含内联 Skip） | `sub_6490F8` / `0x6490f8` | `sub_5481AC` / `0x5481ac` | `sub_10034A750` / `0x10034a750` | `sub_34D7A4` / `0x34d7a4` |
| Ext getNextTag wrapper | `sub_641A30` / `0x641a30` | `sub_541C70` / `0x541c70` | `sub_1003433D8` / `0x1003433d8` | `sub_346088` / `0x346088` |
| Ext PostRegist/class binding | `sub_640968` / `0x640968` | `sub_540F30` / `0x540f30` | `sub_100342404` / `0x100342404` | `sub_344CE4` / `0x344ce4` |
| Ext native instance factory/constructor | `sub_641368` / `0x641368` | `sub_541850` / `0x541850` | `sub_100342DE8` / `0x100342de8` | `sub_345994` / `0x345994` |
| Ext static registrar initializer | `sub_42ED08` / `0x42ed08` | `sub_301264` / `0x301264` | `InitFunc_159` / `0x100351960` | `InitFunc_159` / `0x355b48` |
| NCB AllRegist(line)，核对 Post line | `sub_548EAC` / `0x548eac` | `sub_4A96A0` / `0x4a96a0` | `sub_100287B8C` / `0x100287b8c` | `sub_28A950` / `0x28a950` |

上述 Skip、Ext 注册/工厂/initializer/wrapper 均完整反编译；Exb GetNextTag 的 5678/2855/2983/4371 行、Ext GetNextTag 的 6874/3156/2961/5116 行全部分页到 done。ARM32 某些指针 xrefs 只在函数地址带 Thumb bit 时可见，按 UTF-16 `getNextTag` 绑定 key 交叉找到注册函数，再按带 bit 的回调 xrefs 闭合初始化链，未按邻接地址猜函数身份。

四个 Ext GetNextTag 都引用字面的 `.../plugins/ExtKAGParser/ExtKAGParser.cpp` 源路径。四个 initializer 的 module key 字节均为 `ExtKAGParser.dll`，指向各自 PostRegist 绑定函数；终止回调为空。四个 NCB `_top` 的 base 分别为 Android arm64 `0x1ab5920`、Android armv7 `0x1111bc0`、iOS arm64 `0x10256b8f8`、iOS armv7 `0x218f184`，Ext initializer 写其第 2 项，证明是 PostRegist。

关键常量经原始 UTF-16 读取、类型修正、重新 decompile 验证：起止三种独立行形式、`[eval exp='`、`']`、反引号+CR+反引号+LF 和 module name。Hex-Rays 部分别名仍显示首字符时，以原始字节与参数流为准，不把 `"'"` 误当成只含引号的结尾。

## 四者共同控制流

```text
Ext module PostRegist:
    global = owning script dispatch; if null: return
    class = new native class named "KAGParser"
    install independent Ext native-instance factory and methods/properties
    value = object(class, null) // class reference is retained by Variant
    release temporary class reference
    global.PropSet(MEMBERENSURE, "KAGParser", value, global)
    release global; release local Variant
    // no mutation or traversal of existing parser instances

Ex/Exb Skip path:
    if a standalone iscript opener is reached during macro recording:
        throw LabelOrScriptInMacro

Ext Skip path inside GetNextTag:
    only scan at physical line start when !LineBufferUsing && CurPos == 0
    opener must equal [iscript], [iscript]\\, or @iscript (whole line)
    script = empty; ++CurLine; script_start = CurLine
    until an exact [endscript], [endscript]\\, or @endscript line:
        if RecordingMacro:
            for each source code unit c until NUL:
                if c == single_quote or c == backtick: append backtick
                append c
            append backtick, CR, backtick, LF
        else if ExcludeLevel == -1:
            append line text; append CR,LF
        ++CurLine
    if CurLine == LineCount: throw InlineScriptNotEnd
    if RecordingMacro:
        LineBuffer = "[eval exp='" + script + "']"
        LineBufferUsing = true; CurLineStr = LineBuffer.c_str()
        retain CurPos == 0 and CurLine at endscript line
        resume ordinary GetNextTag parsing from this LineBuffer
    else:
        if ExcludeLevel == -1 && Owner:
            Owner.onScript(script, StorageShortName, script_start)
        continue physical-line scan after endscript

ordinary macro recording:
    parse the synthetic eval tag under RecordingMacro
    no expression evaluation and no onScript call at definition time
    append its original escaped tag text to RecordingMacroStr
    existing line-buffer/end-of-line progression consumes the endscript line
    on eventual macro invocation eval executes the decoded complete script
```

宏录制分支先于排除状态检查，不能把正文当 `ch`，也不能在定义时调用 onScript。单引号/反引号转义在构造时完成，CR/LF 同样带反引号，保持脚本文本和 KAG 单引号属性的边界。正文中的括号、KAG 形式的文本和注释不被独立解析；只认可完整终止行。缺失终止仍抛原错误。标签行/普通脚本的原有错误和 callback 行号不改变。

## 差异与本地对照

- Android arm64：Ext 的 RecordingMacro 字段 `+290`，LineBuffer `+256`、LineBufferUsing `+264`、CurLineStr `+248`，合成/发布在 `0x64cfc0..0x64d2c4`；大量 Variant/string 引用计数内联。
- Android armabi-v7a：对应 `+162`、`+140`、`+144`、`+136`，合成/发布在 `0x54a0e4..0x54a5fa`。序言/条件布局不同，流程一致。
- iOS arm64：字段同 64 位 Android，合成/发布 `0x10034aae4..0x10034ad8c`；字符串拼接/清理更多走 helper。
- iOS armv7：字段同 32 位 Android，合成/发布 `0x34dcb2..0x34df74`，额外浮点寄存器保存为 ABI 产物。
- 四者在脚本识别、转义字符、行末、缺失终止、排除/录制优先顺序、LineBuffer 发布、延迟执行及独立模块类工厂上未发现语义分歧。

修改前，本地 `SkipCommentOrLabel` 对宏内脚本直接 throw，与 Ex/Exb 相符；读取类全字段、构造和全部注册调用链，并以 gitgrep/文件枚举独立核实没有 ExtKAG module。故不能无条件删除错误并声称它本来支持。只在游戏显式 link ExtKAGParser.dll 时按原生注册动作安装独立类。

本地最小复原采用独立 `tTJSNI_ExtKAGParser` 实例类型和独立 create-instance factory，由同一份现有 KAG API 绑定代码生成新的 class object。Ext 子类覆盖受保护的宏脚本能力方法；它是两种独立原生类共用扫描代码时的实现选择，不是参考二进制里的额外 bool 字段或脚本 API。没有全局能力开关，不修改已创建基础实例，不把能力选择写入 checkpoint，不在 assign/restore 时改变实例种类。参考 Ext 还有 while/local 变量等不同状态拓扑，本轮不声称完整还原该模块，不添加假成功 API。

`SkipCommentOrLabel` 的 Ext 录制路径逐步对应共同伪代码：扫描完整块→逐字符转义→合成 eval→写 LineBuffer→设 using→更新 CurLineStr→返回给普通 parser。复用的字段本来已经参与 Assign、Store、Restore 和 ClearBuffer；不引入旁路正文缓存或单独执行队列。

先定义子类再 link 的时序也按原生 global replacement 保留。本地 TJS 引擎现有 `CreateExtendsExprCode/CreateExtendsExprProxyCode` 为 superclass 生成表达式 getter；类构造和 superclass proxy 会重新求值。无需修改 TJS 或遍历旧类。是否覆盖实际游戏初始化时序，以完整游戏运行及主任务的真实输入探针确认，不能仅凭这条源码调用链宣称验收通过。

源码修改限制为 KAGParser 公共实现/声明、一个 ExtKAGParser NCB 注册文件及 CMake source discovery。基础实例保留拒绝行为，Ext 实例仅增加本轮已取证功能。不改游戏脚本、不造 fixture，不自行构建或重启。实现后 `git diff --check` 和 `python3 .github/rpg-runtime/verify-source.py` 均通过；正式构建和实际游戏验证由主任务执行，目前不能宣称运行通过。
