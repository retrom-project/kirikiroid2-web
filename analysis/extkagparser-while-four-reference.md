# ExtKAGParser 循环与调用边界：四参考对照

本轮先完成原生取证和下述实现对照，再改 C++。这是 `ExtKAGParser.dll` 独立类的复原；基础 KAG 不解释这些循环标签，也不新增 Ext 的存档字段。独立类/工厂/模块的证据见 `extkagparser-macro-iscript-four-reference.md`。不以游戏名、资源路径、表达式内容或循环次数启用行为。

原始取证保存在当前 PFB 忽略目录 `retrom/.pfb/acceptance/karigurashi/ext-while-control-native.json`。四个 binary 和配套 IDB 的对应关系、可读性和架构重新核对；IDB 由主任务拥有，未关闭或重启。GetNextTag 沿用本次对话已完成的全量原生反编译（6874/3156/2961/5116 行，所有页到 done），本轮补齐栈 helper、表达式入口、UTF-16 字节与标签绑定 disasm。

`ext-while-control-supplement.json` 另存标签绑定 disasm、FindNearestLabel 原始字节/类型修正后的四份 fresh decompile 及 health。完整生命周期 raw 为 `ext-while-lifecycle-native.json`（SHA-256 `7de9f5235cee53f7e0e895fb5e3425e5f66597fb2ffc62d8816ed0bea06a7895`），175 个完整函数与全部 Store/Pop disasm、187 条分页审计均到 done；其临时汇总为 `ext-while-lifecycle-findings.md`。

## 四文件映射

| 操作 | `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `Kirikiroid2_1.3.9_iOS_arm64` | `Kirikiroid2_1.3.9_iOS_armv7` |
|---|---|---|---|---|
| Ext GetNextTag | `sub_6490F8` / `0x6490f8` | `sub_5481AC` / `0x5481ac` | `sub_10034A750` / `0x10034a750` | `sub_34D7A4` / `0x34d7a4` |
| PushWhileStack | `sub_64804C` / `0x64804c` | `sub_547664` / `0x547664` | `sub_100349990` / `0x100349990` | `sub_34C828` / `0x34c828` |
| PopWhileStack | `sub_646600` / `0x646600` | `sub_546644` / `0x546644` | `sub_100348720` / `0x100348720` | `sub_34B3CC` / `0x34b3cc` |
| WhileStackControlForEndwhile | `sub_653BAC` / `0x653bac` | `loc_54D358` / `0x54d358`（IDA 错并入 `sub_54D328`，完整原生 disasm） | `sub_100350B5C` / `0x100350b5c` | `sub_354D38` / `0x354d38` |
| while frame constructor | 内联于 PushWhileStack，两个容量分支相同 | `sub_547CC8` / `0x547cc8`，`0x547c38` 是解引用参数 wrapper | `sub_100349F90` / `0x100349f90` | `sub_34CE60` / `0x34ce60` |
| FindNearestLabel | `sub_647E54` / `0x647e54` | `sub_547514` / `0x547514` | `sub_100349798` / `0x100349798` | `sub_34C6CC` / `0x34c6cc` |
| ExecuteExpression 无 context 入口 | `sub_8E4374` / `0x8e4374` → `0x8e4384` | `sub_6B47E4` / `0x6b47e4`（context=0 内联） | `sub_100187430` / `0x100187430` → `0x100187440` | `sub_184C68` / `0x184c68` → `0x184c70` |

四份 Ext 的标签表都把 while/endwhile/break/continue 绑定到内部枚举 15/16/17/18；原始 UTF-16 字节和各自绑定调用的 disasm 一并核实。内部枚举不是公开 API，本地共享实现中的命名枚举可与已有 pmacro 编号共存，不能让两个标签共享同一枚举分支。

## 共同控制流（循环）

```text
tag classification:
    only ProcessSpecialTags && Ext instance recognizes these four tags
    ordinary macro recording occurs before loop execution
    while/endwhile are structural, before the ordinary cond && active gate
    break/continue execute only under cond && active

while:
    normalize position after opener (']' already consumed; @ command -> next line)
    PushWhileStack() // snapshots outer exp/each and old If/Exclude
    ++IfLevel
    if ExcludeLevel != -1: resume normal parsing without evaluating expressions
    result = void
    init = string(tag.init); if init != "": ExecuteExpression(init, null, result)
    WhileLevelExp = string(tag.exp); if empty: throw syntax/missing-parameter
    WhileLevelEach = string(tag.each)
    ExecuteExpression(WhileLevelExp, null, result)
    if !bool(result): ExcludeLevel = IfLevel
    resume at saved after-opener position

endwhile:
    require nonempty WhileStack && IfLevel-1 == back.IfLevel
    repeat = false
    if ExcludeLevel == -1:
        ExecuteExpression(WhileLevelEach, null, result)
        ExecuteExpression(WhileLevelExp, null, result)
        repeat = bool(result)
    WhileStackControlForEndwhile(repeat)
    if repeat: resume restored after-opener position
    else: consume this end-command normally

break:
    same nonempty/IfLevel guard
    WhileStackControlForEndwhile(true)
    ExcludeLevel = IfLevel
    resume normal parsing at restored after-opener position

continue:
    same nonempty/IfLevel guard
    WhileStackControlForEndwhile(true)
    ExecuteExpression(WhileLevelEach, null, result)
    ExecuteExpression(WhileLevelExp, null, result)
    if !bool(result): ExcludeLevel = IfLevel
    resume normal parsing at restored after-opener position

WhileStackControlForEndwhile(repeat):
    if !repeat: PopWhileStack(rewind=false)
    else:
        save current exp/each by value
        PopWhileStack(rewind=true)
        PushWhileStack() // snapshots outer state again
        restore saved current exp/each
        ++IfLevel
```

没有另设逐字搜索 endwhile 的文本扫描器。break 把恢复后的本层置为排除，再由普通结构标签处理走到匹配终点；嵌套 while 仍正常压栈/出栈，不求值被排除的表达式。continue 每次先 each 再 exp，不重做 init。原生严格要求 IfLevel 匹配，因此不能自行放宽成“任意 if 深度都可 break”。空 each 仍传给原生表达式执行入口，不省略其调用。

## 共同控制流（循环帧）

64 位帧为 68 字节，32 位为 44 字节：依次是 Storage、Label、Offset、OrgLineStr、LineBuffer、Pos、LineBufferUsing、ExcludeLevel、IfLevel、WhileLevelExp、WhileLevelEach。字符串采用原生 ttstr 引用计数；容器是值语义 vector，没有脚本旁路缓存。

Push 先 FindNearestLabel(CurLine)，无 label 则基准行 0；保存相对 offset、当前物理行正文（EOF/无 Lines 则空串）、完整展开后的 LineBuffer 及当前位置。FindNearestLabel 反向扫描时跳过独立 iscript 块内的伪 label，识别与宏内脚本相同的三种完整行形式，使用 Scenario 已有 alias 缓存。

Pop 首先拒绝空栈，也拒绝在有 call frame 时弹出深度不大于其 WhileStackDepth 的帧。rewind=true 仅在当前 Scenario 内由 label cache+offset 定位，不重载 Storage；label 缺失、超过 EOF、原始行正文不一致分别抛错。EOF 恰等于 LineCount 合法。需要 LineBuffer 时先还原其值，再设 CurLineStr 和 using；否则用物理行。无论是否 rewind，恢复 ExcludeLevel、IfLevel 和外层 exp/each，最后析构弹出的帧。原生 Storage 字段仍保存和序列化，不擅自把未读取字段删掉。

## 编译差异与本地修改前对照

Android arm64 大量内联 ttstr 和 frame 构造；Android armv7 有额外参数解引用 wrapper，WhileStackControlForEndwhile 被 IDA 错误并入相邻 vector helper，但 `PUSH {R4-R7,LR}; SUB SP`、完整独立返回、原生调用点证明其边界；iOS 共享构造/helper 更多，armv7 有 SjLj 清理路径。循环条件、缺省值、表达式调用次序、帧字段、层级/调用边界检查和复位流程四者一致。

修改前本地独立 Ext NI 已存在，但只覆盖宏内 iscript 能力；全量读取 KAGParser.h、GetNextTag、Assign/Store/Restore/Clear 和 call stack 后确认没有 WhileStack/两表达式/四标签分支。本轮将在同一共享扫描器下增加 Ext 专用虚方法能力门控，独立 factory/实例种类保持不变；基础实例不解释标签，不读写 Ext 的存档/调用帧字段。局部位置推进因现有本地 scanner 在 tag 结束处才消费 ']'，循环专门在 native 同一控制点消费它，确保存下的是 after-opener，而不是重新执行 while/init。

循环 helper 的字段、保存顺序、检查和中间值按上述共同伪代码逐步复原；不能用单一行号跳转、直接 goto 代替值语义帧、call 边界或完整 LineBuffer。

## 调用和状态生命周期（修改前补充取证）

并行原生取证的完整分页保存在同目录 `ext-while-lifecycle-native.json`，由同伴显式 adopt 同四 IDB、逐函数 decompile 得到。四文件顺序仍为 Android64/Android32/iOS64/iOS32：

| 操作 | Android64 | Android32 | iOS64 | iOS32 |
|---|---|---|---|---|
| Constructor/factory | `641368` | `541850` | `100342de8` | `345994` |
| Assign callback/实现 | `641b3c` | `541d30` | `1003434bc` | `3461b8` |
| Store | `642c10` | `542b60` | `100343fe4` | `346dbc` |
| Restore callback | `643994` → `65461c` | `54359c` | `100344d68` | `347b50` |
| Invalidate | `6455cc` | `545e72` | `100347c48` | `34a78a` |
| Clear | `642a40` | `542aa8` | `100343f14` | `346c8c` |
| ClearBuffer | `6458e4` | `545f88` | `100347ee8` | `34a980` |
| BreakConditionAndMacro | `6459c4` | `546018` | `100347f8c` | `34aa88` |
| PushCallStack | `647890` | `547170` | `100349414` | `34c33c` |
| ClearCallStack callback | `643a58`（内联） | `544a44` → `54d7cc` | `1003468b0` → `100351220` | `349502` → `3554f4` |
| PushLocalVariables | `648890` | `547a78` | `100349d38` | `34cbc0` |
| PopLocalVariables | `646324` | `546428` | `1003484d4` | `34b0c4` |
| 裁局部栈到最新 call | `645ac0` | `5460c4` | `100348060` | `34abb4` |
| 裁 while 栈到最新 call | `645d24` | `546208` | `100348200` | `34adc4` |
| ClearWhileStack | `646530` | `54658c` | `100348648` | `34b2a8` |
| Local Array.assign | `657840` | `54df00` | `1002a2330` | `2a723c` |
| Local Store/assignStruct | `6584e4` | `54e58c` | `1002a296c` | `2a7a1c` |
| Local Restore/assignStruct | `658628` | `54e65c` | `1002a2a88` | `2a7b5c` |
| Local reset | `657c28` | `545d64` | `100347be0` | `34a6d4` |
| local keys via Array.assign(dict) | `6591e0` | `54ed20` | `100289794` | `28c1e0` |
| local args merge | `6592fc` | `54ede0`（parser wrapper `547dd0`） | `100289890`（wrapper `10034a2c4`） | `28c310`（wrapper `34d1e8`） |

return 控制流在四份完整 GetNextTag 内联，不能把独立 while Pop 当 return 全部；主入口分别为 `64bbe4`、`54bbec`、`10034c62c`、`34fa8c` 附近。64 位 call frame 因 STL vector<bool> ABI 为 Android 136/iOS 120 字节，32 位分别 84/76；新增两个字段始终是 32 位整数，键为 `WhileStackDepth`、`LocalVariablesCount`，不是内嵌 while 数组。

```text
Ext constructor:
    LocalVariables = actual TJS Array
    zero/default ordinary fields
    LocalVariables.clear(); push newly created empty Dictionary

PushLocalVariables(copyvar, args):
    if copyvar != void: evaluate string(copyvar) globally
    if evaluation true: new dict; Dictionary.assign(old top)
    else: new empty dict
    array[count] = new dict
    pairs = new Array; pairs.assign(args) // key,value,key,value...
    keys = new Array; copy even pairs slots into keys
    for each key: new top[key] = args[key]

PushCallStack(copyvar,args):
    find nearest label / snapshot original physical line
    PushWhileStack() // call boundary snapshot, even outside a loop
    PushLocalVariables(copyvar,args)
    push ordinary call frame plus current WhileStack.size and locals.count
    MacroArgStackBase = MacroArgStackDepth

return:
    crop locals to current call.LocalVariablesCount
    crop while to current call.WhileStackDepth
    restore ordinary source position/conditional stacks or perform explicit goto
    restore macro arg base; pop call frame
    PopWhileStack(false); PopLocalVariables()
    if explicit target: BreakConditionAndMacro()
    Owner.onAfterReturn()

BreakConditionAndMacro:
    crop locals to call boundary (or 1 if no call)
    crop while to call boundary (or clear all if no call)
    clear current exp/each
    clear ordinary recording / exclusion / if stacks / macro expansion

ClearBuffer:
    BreakConditionAndMacro() BEFORE releasing Scenario and source names

ClearCallStack:
    clear call frames; MacroArgStackBase/Depth = 0
    ClearWhileStack(); reset local Array to one real empty Dictionary

Assign:
    LocalVariables.assign(source.LocalVariables) // shallow dict references
    copy exp/each and value-semantic whole while vector
    retain ordinary parser/call state copying

Store:
    LocalVariables = new Array.assignStruct(current locals)
    ordinary macros/paramMacros/arguments/call frames, with two Ext call fields
    global WhileLevelExp, WhileLevelEach and whileStack Array of 11-field dicts
    IgnoreCR = integer 0/1

Restore:
    LocalVariables.assignStruct(saved.LocalVariables)
    ordinary macros/arguments/call frames and the two Ext fields
    reconstruct whileStack, read global exp/each
    ClearBuffer(); LoadScenario(storage); GoToLabel(label)
    restore IgnoreCR and ordinary exclusion/if/macro-base fields
```

while frame 保存的 11 键依次为 `storage,label,offset,orgLineStr,lineBuffer,pos,lineBufferUsing,ExcludeLevel,IfLevel,WhileLevelExp,WhileLevelEach`。全局数组键为小写 `whileStack`，局部栈为大写 `LocalVariables`。Array 下标读取/写入旗标均为 `TJS_MEMBERENSURE(0x200)`，count 读取/设置旗标 0；assign/assignStruct 都是 FuncCall(0) 单参数。临时 Array.assign(dict) 得到扁平键值、再快照 keys、最后重新 PropGet 的中间过程不能简化为直接枚举/合并。Assign 与 Store/Restore 的浅拷/结构拷贝语义不同，不能统一深拷。

四份 Restore 都先读循环状态，后由 ClearBuffer/Load/Goto 重新经过 BreakCondition：当前层循环会裁到 call 边界，当前 exp/each 被清空。最后恢复 IfLevel 等不恢复这些已清值。保留原生顺序，不声称任意中途恢复可继续同一循环。四份 IgnoreCR 都无缺失值 guard，void 转整数 0；仅 Ext 分支保存/恢复此现有字段。EnableNP 更广行扫描/错误恢复机制未在本轮复原。

本地对应：独立 Ext NI 初始化真实 LocalVariables Array；新增 while 值帧与两个 call 帧 depth；Push/Pop/Break/Assign/Store/Restore 均按上述顺序操作，并以 Ext 实例能力门控。共享实现的普通基础实例保留旧分支，不序列化新字段、不改变实例类型。Clear 中 ClearCallStack 后再 ClearWhileStack/reset locals 的重复步骤也保留原生可见顺序。

### 明确的 Web 内存安全边界：局部栈 Pop

四参考 tTJSAry::Pop 在检查 count 后，把 wrapper 本身地址加 `(count-1)*sizeof(tTJSVariant)` 作为 Variant 复制源，而不是读脚本 Array 元素；四 disasm 确认 Android64 `658414..658424` / iOS64 `1002a2924..1002a2934` 为 stride 0x14，Android32 `54e558..54e566` / iOS32 `2a79a6..2a79b8` 为 stride 12。构造和 count/top/get/set helper 已证明这里是单个 Variant 包装的实际 TJS Array。count=1 复制 wrapper 自身，count>1 读取相邻 parser 字段；调用方仅析构返回临时值，然后继续，不使用其值。

仅 Web 局部栈 Pop 省去这次无语义用途的越界 Variant 临时复制；仍保留空栈/call 边界检查与真实 Array.count 缩减，元素由 Array 按原契约释放。这样避免 Wasm 从线性内存相邻字段获得随机 Variant 类型/对象引用并误做引用计数。没有额外 PropGetByNum、假值或替代容器。非 Web 保留四参考复制步骤。此项是明确的平台差异，不宣称逐步骤与原生相同，不扩散到其它 Array 操作。

## 验证边界

不创建 scenario/fixture，不改私有脚本，不自行构建/重启。现有真实资源含循环，可由主任务在完整游戏及独立 parser 中验证；未出现的 break/continue 只能如实标记静态四参考覆盖。运行、Assign 继续推进、Store 结构、Restore 的原生顺序是不同结论，不将“字段写入成功”当作“任意中途读档继续成功”。

实现后 `git diff --check` 与 `python3 .github/rpg-runtime/verify-source.py` 均通过。仅 KAGParser.cpp/.h 和本文变更，源码已冻结交主任务 review、构建和真实资产验证；本分析阶段不宣称候选构建或游戏运行通过。
