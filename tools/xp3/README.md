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

### 离线标准化

`standardize.py` 只负责接收 ZIP、计算受管副本的文件事实、调用明确指定的
retrom-runtime `normalize-content`，然后校验文件清单并统一写入 STORE ZIP。
格式检查、名称映射、过滤参数恢复和资源转换与在线导入使用**同一套宿主插件**；
Python 不再保存独立过滤算法、游戏参数或宽松短流策略。

```bash
python3 tools/xp3/standardize.py original.zip standardized.zip \
  --runtime-tool /path/to/runtime-tool/scripts/runtime-cli.mjs \
  --node /path/to/node --report conversion.json
```

需要 Python 3.11+ 及配套 runtime 工具要求的 Node 版本。工具路径是显式输入；
离线包应包含完整 runtime 工具及生产依赖，不依赖某个 Retrom checkout。
可用 `--platform` 选择插件声明的平台，默认 `kirikiri`。运行库文档中的
`normalization/registry.mjs` 是已支持格式的注册清单。

输入文件始终只读。ZIP 先流式接收到本次独占临时目录，支持 STORE/DEFLATE，
校验路径、成员 CRC、长度和摘要；转换器仅在自己的子目录输出资源。未匹配格式
保持原内容，匹配后的错误使操作失败，不发布候选包。普通 XP3、补丁归档层次和
未修改成员均保留，Python 不合并或猜测引擎覆盖规则。

最终包采用排序后的逻辑路径、固定 ZIP 时间及 STORE；写入时再次核对全部成员
大小和 SHA256。转换回执包括插件版本、格式、来源摘要、资源校验和明确的元数据
例外；版本本身不加入内容身份。成功后原子发布完整包，禁止覆盖已有文件，清理
仅限本次临时文件。不会删除来源、执行原生代码、改变浏览器核心或证明游戏可玩。
实际标题、正文、输入和跨会话存档仍须独立验收。

封装层的发布拥有权及失败清理回归运行：
`python3 -m unittest discover -s tools/xp3 -p 'test_*.py'`，也由 fork 质量门禁执行。
