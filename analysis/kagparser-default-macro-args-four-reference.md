# 默认 KAGParser 的宏参数与 `*` 转发

本记录只涉及默认 KAGParser。Ex、Exb 和 Ext 是不同参考实现；已有的有序
参数名和扁平键值存档属于扩展实现，不能据此推断默认类的容器和存档格式。
四个目标及其配套 IDB 均在本轮打开、核验并通过原生 IDA MCP 反编译。

## 四文件映射

| 参考二进制 | GetNextTag（含 `*` 和 pop） | push | assign | store | restore | clear 宏栈 |
|---|---|---|---|---|---|---|
| Android arm64-v8a | `sub_A1FDA0` / `0xa1fda0` | `sub_A1F27C` / `0xa1f27c` | `sub_A1A708` / `0xa1a708` | `sub_A1AD60` / `0xa1ad60` | `sub_A1BFE0` / `0xa1bfe0` | `sub_A25044` 内联 / `0xa25044` |
| Android armabi-v7a | `sub_769F00` / `0x769f00` | `sub_769938` / `0x769938` | `sub_766DC8` / `0x766dc8` | `sub_7671FC` / `0x7671fc` | `sub_767E24` / `0x767e24` | `sub_76912C` 内联 / `0x76912c` |
| iOS arm64 | `sub_10003451C` / `0x10003451c` | `sub_100033E04` / `0x100033e04` | `sub_100030480` / `0x100030480` | `sub_1000308B0` / `0x1000308b0` | `sub_100031A24` / `0x100031a24` | `sub_10003037C` / `0x10003037c` |
| iOS armv7 | `sub_328D4` / `0x328d4` | `sub_32198` / `0x32198` | `sub_2EA34` / `0x2ea34` | `sub_2EE70` / `0x2ee70` | `sub_2FE44` / `0x2fe44` | `sub_2E988` / `0x2e988` |

注册回调和方法 wrapper 将上述 assign/store/restore 分别绑定到同名 TJS
方法；push 从各自 GetNextTag 的宏展开路径直接调用。构造器亦已分别反编译：
`0xa1a0fc`、`0x766a1c`、`0x10002ff54`、`0x2e590`。调用点、栈访问及
push/clear 均表明默认类持有指针元素的 C++ vector；每个元素是一个真正的
TJS Dictionary，并不是 `{Dictionary, Array}` 对。

## 四文件共同控制流

```text
push(values):
    if vector.size > depth: slot = vector[depth]
    else:
        if vector.size < depth: throw internal error
        slot = create Dictionary; vector.push_back(slot)
    ++depth
    Dictionary.assign(values) in context of slot

parse '*':
    if not recording macro:
        if depth != 0 and vector[depth - 1] != null:
            Dictionary.assign(vector[depth - 1]) in context of current tag
        set current tag's original tagname again

assign(other):
    Release every element; clear vector; depth = 0
    for each active slot in other:
        slot = create Dictionary; Dictionary.assign(other slot) into slot
        vector.push_back(slot)
    depth = other.depth; base = other.base

store:
    result.macroArgs = create Array
    for each active slot:
        saved = create Dictionary; result.macroArgs[i] = saved
        Dictionary.assign(slot) into saved
    write depth and base

restore:
    depth = 0
    if macroArgs is non-void:
        count = macroArgs.count; Release and clear vector
        if stored depth is non-void: depth = stored depth
        for each saved element:
            slot = create Dictionary; Dictionary.assign(saved element) into slot
            vector.push_back(slot)
    if depth != vector.size: throw malformed save
    base = vector.size

clear:
    Release every element; clear vector; depth = 0
```

四份参考在上述条件、一个参数的 `Dictionary.assign` 调用、重设 tagname、
单字典元素及其生命周期上没有发现行为差异。32/64 位的指针宽度、虚函数槽
偏移及容器扩容生成代码不同；这些 ABI 差异不应写入 C++ 布局。四份默认类
的 `macroArgs` 保存项都是 Dictionary；Ex/Exb 的扁平数组不能直接替代。

## 修改前本地对照

1. 本地四类解析器共用 `{values Dictionary, names Array}` vector；push 和
   clear 总会同时创建、复制和释放两个对象。默认类应只维护字典 vector。
2. 本地 `*` 遍历初始化时记录的有序名字，逐项写入当前标签；默认类应对当前
   宏参数字典调用缓存的 `Dictionary.assign`，然后重设当前 tagname。
   运行中动态加入字典的属性因此也须由引擎的字典调用本身处理。
3. 本地 assign 总是复制成对元素；默认类应只复制活跃字典。
4. 本地 store/restore 对所有类使用扁平键值数组；默认类应存取字典对象。
   共用的 depth/base 检查与栈回退继续保留。
5. 相同种类实例之间，Ex、Exb、Ext 的已有代码路径保持原有实现。不同种类
   实例之间的 `assign` 不能作这一断言，见下方风险记录。

真实完整游戏包和浏览器的诊断副本证明：原始宏运行时新增的场景属性存在，
现有 `*` 分支却未转发它，导致场景调用没有 storage。私有游戏数据、诊断
副本和原始工具输出保存在本任务 PFB 的忽略目录，不进入仓库。

## 不同实例种类之间的 assign：Web 内存安全边界

后续审计发现，显式加载插件可能替换全局类，同时保留早先创建的解析器
实例。四参考各自的 native-class 注册按名字查找现有 ID，因此默认和 Ex
都使用名为 `KAGParser` 的 ID；默认与 Ex 的 assign wrapper 不进一步检查
实际对象种类。仅凭同名 ID 无法认为它们的内部布局或混合赋值安全。

| 参考 | 默认 assign wrapper / 类注册 | Ex assign wrapper / 类注册 | Ex 实例构造 | 按名注册 native class |
|---|---|---|---|---|
| Android arm64 | `0xa24f4c` / `0xa2447c` | `0x557838` / `0x556d00` | `0x549c84` | `0x9f3818` |
| Android armv7 | `0x76d054` / `0x76c7fc` | `0x4b1bc4` / `0x4b12e4` | `0x4a9e98` | `0x7524b4` |
| iOS arm64 | `0x100038050` / `0x1000376c8` | `0x1000a34cc` / `0x1000a2adc` | `0x10009a18c` | `0x10004e0f0` |
| iOS armv7 | `0x36448` / `0x358f0` | `0xa23b0` / `0xa17ec` | `0x98be8` | `0x4cdec` |

四参考默认 assign 以指针宽度读取来源的宏参数元素；Ex 的 vector 元素是
两个指针，Ex assign 的 vector helper 还按完整 vector 而非活跃 depth 复制。
Exb 构造还显示其 vector/深度字段位置与默认类不同。因跨类解释会把一种
布局当另一种布局，不能将混合赋值宣传为受支持，也不能未经证据发明转换
规则。本地共享 C++ 实现中，不同种类使用不同的 vector；原拟实现对来源和
目标种类不同、且来源宏栈非空的 assign 会索引未使用的 vector。WebAssembly
无法安全复现跨不同 C++ 容器布局的类型混用和任意越界访问，因此 Web 在
复制状态之前对该条件抛出已有的 `TVPKAGSpecifyKAGParser`；来源活跃栈为空
及同一种容器的已有路径不受这个检查影响。此限制与原有本地共享 pair vector
的行为不同，也不是四参考提供的混合转换契约；非 Web 不引入该检查。以上
真实游戏验收仅覆盖同种类解析器，尚无现成混合类型运行测试材料。
