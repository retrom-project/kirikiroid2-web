## xp3viewer工具

### 解包`xp3`文件

```
./xp3 --help                                    
Usage: xp3 [--help] [--version] [--output VAR] files...

Positional arguments:
files          input files path [nargs: 1 or more]

Optional arguments:
-h, --help     shows help message and exits
-v, --version  prints version information and exits
-o, --output   output dir path
```

### 离线标准化 CxFilterFS / eliF 归档

`standardize.py` 使用 Python 3.11+ 标准库，将已确认采用 CxFilterFS
原名表、MD5 物理名和 TinyXOR 内容过滤的完整 STORE ZIP 转换为新的 STORE ZIP：

```bash
python3 tools/xp3/standardize.py original.zip standardized.zip \
  --format cxfilterfs --report conversion.json
```

工具保留各 XP3 的文件名、外层成员顺序与归档分层，将内部资源写成普通逻辑路径、
未加密的原始 segment；非 XP3 成员逐字节复制。它不执行 EXE/TPM/DLL/脚本，
不改引擎，不替换脚本，不覆盖输入或已有输出。输出成功前位于临时目录；发生错误
不会发布半成品 ZIP。输入前后 SHA256、逐资源 SHA256/Adler32 和转换结果写入报告。
索引读取有尺寸/链长限制，资源内容流式处理，不把整个游戏载入内存。

此格式中每个 `eliF` 的 `h` 独立提供过滤参数：
`t = h ^ 0x1548E29C`，将 `t` 四个字节 XOR 后作为 key，零值改为 `0xD7`。
对 XP3 解压后的完整流执行恒定 XOR，包括 `File.flags == 0` 的条目。
逻辑名仅做 ASCII 小写、斜杠归一和移除开头斜杠，然后按 UTF-16LE 求 MD5
核对物理名；不做 Unicode casefold、路径猜测或游戏名称分支。

默认拒绝长度或校验不一致。对于已核实原生顺序读取只能返回部分内容的输入，
可显式指定 `--inconsistent-entry-policy native-filtered-short-stream` 继续生成候选。
该模式只接受 segment 合计小于声明尺寸的条目，输出实际可读字节经过相同内容过滤
后的结果，并修正输出长度与 Adler32。报告逐项标注 `metadataCorrected: true`，
保留原尺寸、segment、校验值和解码前后 hash；这些条目不计入完整校验通过。
这不重现原引擎矛盾的 GetSize/Read 行为，也不补齐缺失内容。原始字节仍保存在
未修改的源 ZIP 中。候选包必须另行验证实际启动、正文和存读档。

仅支持显式确认的这一种格式。无完整 `eliF`、未知 chunk/segment、额外
`xp3filter.tjs`、其他加密层、压缩外层 ZIP 等均拒绝；不把“不支持”判断为游戏损坏。
自定义 `archive://` 媒体依赖或其他插件 API 不会因资源标准化而自动获得支持。
原 `.sig` 等非 XP3 成员保持原样，它们并非转换后归档的有效签名；本工具的
产物用于普通 XP3 消费端，不承诺能回到原厂 EXE/TPM 验签启动。
