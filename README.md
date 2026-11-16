# 元宝 Bot CLI（纯命令行版）

> **当前版本：v1.0.1-cli**
>
> 腾讯元宝开放平台 Bot 的命令行工具 —— **纯 C++17、无 Web 前端、无浏览器**。编译产物为单个 `main.exe`，直接传参执行：
>
> ```bash
> main.exe send-group <群号> "文本"      # 发群消息（发完即退出）
> main.exe listen                        # 常驻在线，把收到的消息实时打印到终端
> main.exe help                          # 查看全部命令
> ```

> ⚠️ 说明：本项目已从「Web 管理服务器」改造为**纯命令行一次传参执行**的版本。
> `index.html / style.css / app.js / ico/` 等前端资源已**不再被程序读取或使用**，可安全删除；
> 程序运行时不监听任何 HTTP 端口。
> JSON 插件、代理转发、撤回监控等功能**已删除**；LLM 自动回复保留（配置 LLM 后，`listen` 常驻时被 @ 或收到私聊会自动回复，`--no-ai` 可关闭）。`listen` 常驻在线并实时打印收到的消息/日志（Ctrl+C 退出），其余命令均为连接→执行→退出。

---

> 以下为功能与使用说明（原文保留供参考，Web/API 章节描述的能力在 CLI 中均有对应子命令，见 `main.exe help`）。

---

## ✨ 功能特性

### 🤖 Bot 核心
- **WebSocket 长连接**：auth-bind 鉴权、心跳保活、断线自动重连
- **消息收发**：群消息、私聊（DM）、@ 指定成员、@ 全体、引用回复
- **实时消息流**：群消息经 SSE 实时推送到前端，缓存与日志历史合并展示
- **多群同时监听**：`LISTEN_GROUPS` 配置 + 前端逐群开关，被监听群的消息实时记录/展示/触发回复
- **已加入群列表**：收到任意群消息自动收集群号并持久化（`known_groups.json`），无需手动维护

### 📤 发送能力
- **文本 / @ 成员**：支持单 @、多 @、@ 全体
- **引用回复**：回复指定消息（`/api/send-reply`）
- **贴纸**：内置 80+ 腾讯表情，支持贴纸 + 文字 + @，可设置**次数与间隔批量刷屏**
- **图片**：URL 图片或本地文件上传发送
- **文件 / 视频**：本地文件上传（最大 20MB，视频以文件消息发送）

### ⚡ 增强能力
- **刷屏引擎**：15 种文本变换（全角 / MOCK / 乱码 / 彩虹 / 地震 / 矩阵等），自定义次数与间隔，后台线程执行不阻塞响应
- **大模型回复**：接入 OpenAI 兼容 API（DeepSeek / 通义千问 / 智谱等），群消息智能自动回复，可设置 system prompt / 温度 / 超时
- **代理转发模式**：非元宝群的群消息转发到元宝群并 @ 元宝，元宝的回复按 FIFO 队列自动回传到原群（引用原消息）；支持「仅 @ 元宝 时转发」
- **撤回监控**：检测群撤回事件，发送「撤回通知」（撤回者 / 原发送者 / 时间 / 原内容）；图片被撤回自动补发原图
- **图片消息展示**：解析图片 `media_info`，前端直接渲染缩略图

### 🧩 插件系统
- **JSON 声明式插件**：扫描 `plugins/*.json`，定义「命令 → 回复」规则，收到群消息匹配即回复
- 命令匹配**优先于 AI 回复**；支持 `@元宝 /ping` 形式与 `{user}` 占位
- 前端插件面板可安装（粘贴 JSON 或 URL 下载）、启用 / 禁用、重载

### 👥 群成员管理
- 实时获取群成员列表、群主标记、昵称备注（`/api/users`）
- 切换主群 / 监听群后自动清理旧缓存，重新获取

### 📋 消息日志
- JSONL + 文本双格式落盘
- 历史消息回放、按日统计、下载、清空

### 📱 现代 Web 前端
- 现代视觉风格，响应式布局
- **手机端适配**：竖排恢复条导航（左右栏折叠入口）、栏内返回按钮、发送面板吸底置顶操作条等宽

---

## 🚀 快速开始

### 环境要求

| 依赖 | 说明 |
|---|---|
| C++17 编译器 | Windows: MinGW-w64（winlibs 带 OpenSSL 版）；Linux/macOS: g++/clang++ |
| OpenSSL | 1.1+（WSS 加密连接必需） |
| 元宝开放平台凭证 | `APP_KEY` / `APP_SECRET` |

### 编译（Windows MinGW）

```bash
g++ -std=c++17 -O2 -Wall -I. main.cpp -o main.exe -lws2_32 -lssl -lcrypto
```

> ⚠️ 中文用户名（如 `C:\Users\<中文用户名>\...`）会导致 g++ 汇编器无法在系统临时目录建文件，
> 编译前请先指向 ASCII 临时目录：
> ```cmd
> set TMP=C:\yb_tmp & set TEMP=C:\yb_tmp & mkdir C:\yb_tmp
> g++ -std=c++17 -O2 -Wall -I. main.cpp -o main.exe -lws2_32 -lssl -lcrypto
> ```

### 编译（Linux）

```bash
g++ -std=c++17 -O2 -Wall -I. main.cpp -o yuanbao -lpthread -lrt -lm -lssl -lcrypto
```

### 运行（纯命令行，无 Web 前端）

> 💡 也可直接下载 [Releases](https://github.com/System-Virus-XTZ/yuanbao-bot-cli/releases) 中的预编译二进制（Windows x64 静态版 / Linux x86_64，由 GitHub Actions 自动构建并附 SHA-256 校验），跳过编译步骤。

复制 `config.example.json` 为 `config.json`，填入 `APP_KEY` / `APP_SECRET`，然后直接传参执行：

```bash
main.exe send-group 群号 "大家好"        # 发群消息（自动连接，发完退出）
main.exe send-c2c 用户ID "你好"           # 发私聊
main.exe send-sticker 群号 六六六 666     # 发贴纸（可附带文本/@）
main.exe send-image 群号 C:\pics\a.png    # 发本地图片（自动上传）
main.exe flood 群号 冲鸭 --count 20 --interval 200 --mode matrix
main.exe members 群号                     # 查询群成员
main.exe stickers                        # 列出内置贴纸
main.exe history --n 30                  # 查看最近消息日志
main.exe listen                          # 常驻监听（Ctrl+C 退出）
main.exe help                            # 全部命令与参数
```

> `APP_KEY` 为空时，`help / stickers / status / groups / history` 等离线命令仍可使用。

### 斜杠命令（在元宝客户端里发送）

`listen` 常驻时，机器人会在元宝客户端响应下列命令（连接成功时命令清单会自动同步到平台，
平台侧才会出现原生斜杠命令菜单）：

| 命令 | 作用 |
|---|---|
| `/help` | 显示可用命令 |
| `/status` | 显示机器人状态（账号 / 连接 / AI 开关 / 本会话上下文条数） |
| `/new`、`/reset` | 开始新会话，清空该会话上下文 |
| `/stop` | 停止正在生成的回复（并收起"思考中"气泡） |
| `/compact` | 压缩当前会话上下文（只保留最近一半） |
| `/retry` | 重新生成上一条回复 |
| `/time` `/uptime` `/version` `/ping` | 当前时间 / 运行时长 / 版本 / 响应测试 |
| `/whoami` | 显示你的用户 ID、昵称、会话类型 |
| `/calc` | 四则运算，如 `/calc (1+2)*3` |
| `/echo` | 复读指定文本 |
| `/commands` | 列出全部可用命令 |
| `/groups` | 列出本机已知群 |
| `/groupinfo [群号]` | 群名称 / 成员数 / 群主（不填群号则用当前群） |
| `/members [群号]` | 群成员列表（最多 100 人） |
| `/history [条数]` | 查看最近消息（默认 10 条，最多 50） |
| `/inspect [序号]` | 查看某条消息详情（1=最新） |
| `/remind <时间> <内容>` | 一次性提醒，如 `/remind 10m 喝水`（支持 `10s/5m/2h/1d` 或 `18:30`） |
| `/cron <间隔> <内容>` | 周期任务，如 `/cron 1h 起身活动`；`/cron list` 查看、`/cron del <ID>` 删除 |
| `/sticker <名称>` | 发表情贴纸，如 `/sticker 六六六` |
| `/stickers` | 列出可用贴纸（59 个） |
| `/img <URL\|本地路径>` | 发图片（群聊，自动下载/上传 COS） |
| `/file <本地路径>` | 发文件（群聊，自动上传 COS） |
| `/reply [消息ID] <文本>` | 引用回复（缺省引用最近一条消息） |
| `/restart` | 软重启：重新加载 config.json 并重连 Bot（进程不退出） |
| `/yuanbao-upgrade`、`/yuanbaobot-upgrade` | **真正执行升级**：下载最新源码（配置 `UPGRADE_SOURCE_URL` 时）或用本地源码 → 重新编译 → 替换自身 → 自动重启（约 1 分钟） |
| `/issue-log` | 导出日志包（仅私聊；群聊会提示到私聊；包内不含 config.json/密钥） |

> 定时任务持久化在 `logs/tasks.json`，重启后自动恢复；`/cron` 最小间隔 1 分钟。

> **命令同步分两组**（与官方插件一致）：`botCommands` = 框架内置命令
> （`/help /status /new /reset /stop /compact /retry /restart` 共 8 个）；
> `pluginCommands` = 插件自定义命令（`/issue-log`、`/yuanbao-upgrade`、`/remind` 等共 28 个）。
> 若某命令在客户端**灰显**，多因同步分组不对（平台把未知命令当成无效内置命令）——
> 重启 `listen` 让两组命令重新同步即可。

- **群聊里需 @机器人** 命令才生效（与官方插件一致）；私聊直接发送即可。
- 命令必须由**常驻的 `listen`** 处理，一次性命令不会响应收到的消息。
- 本地验证命令链路（不需要真人在元宝里发）：

```bash
main.exe slash dm:用户ID /status --account main   # 私聊场景
main.exe slash 群号 "/new" --account main          # 群聊场景
```

---

## 🤖 AI 回复

支持 OpenAI 兼容大模型（DeepSeek / 通义千问 / 智谱 / OpenAI 等）。

**配置**（`config.json` 全局或账号内均可，账号内优先）：

```jsonc
{
  "LLM_API_URL": "https://api.deepseek.com/v1/chat/completions",
  "LLM_API_KEY": "sk-...",
  "LLM_MODEL": "deepseek-chat",
  "LLM_SYSTEM_PROMPT": "你是一个友好的群聊机器人助手……",   // 可省略（自带默认）
  "LLM_MAX_TOKENS": 200,
  "LLM_TEMPERATURE": 0.8,
  "LLM_TIMEOUT": 15
}
```

**命令**：

```bash
main.exe ai-test "你好" --account main                    # 只测模型（不发送、不需连接 Bot）
main.exe ai-reply 群号 用户ID "问题" --account main        # 生成回复并 @ 提问者发送到群
main.exe ai-reply 群号 用户ID "问题" --no-at --account main # 直接发送不 @
main.exe ai-c2c 用户ID "问题" --account main               # 生成回复并私聊发送
main.exe listen --account main                            # 常驻：被 @ 或收到私聊时自动 AI 回复
                                                             # （配置了 LLM 即默认开启；--no-ai 关闭）
```

**AI 回复支持贴纸与 @（自动生效，无需配置）**：

- 模型在回复里写 `[sticker:贴纸名]` 会**发成贴纸消息**（也接受 `[贴纸:名]`、`[表情:名]`）；
  可混在文本里，程序按出现顺序分别发送，例如：`今天真棒！[sticker:点赞]`
- 模型写 `@昵称` 会转成**真实 @**（仅群聊；需要能匹配到群成员，成员缓存缺失时会自动拉取一次，约数秒）
- 内置 59 个贴纸名与用法说明会随系统提示词一起下发给模型，可在日志里看到
  `[AI] 贴纸 xxx 已发送` / `[AI] 已拉取群成员 N 人（用于解析 @昵称）`

### ⚠️ 命令执行 / 文件读写（高风险，默认关闭）

开启后可在元宝里让机器人**执行系统命令、读写文件**（等于把本机开放为远程 Shell，请谨慎）：

| 命令 | 作用 |
|---|---|
| `/exec <命令>` | 执行系统命令并回结果，如 `/exec dir` |
| `/ls [目录]` | 列目录 |
| `/read <路径>` | 读文件（最多 3000 字符） |
| `/write <路径>\|<内容>` | 写文件（覆盖） |
| `/append <路径>\|<内容>` | 追加内容 |
| `[exec:命令]`（AI 自主） | 模型判断需要时自动执行命令（需 `AI_EXEC_ENABLED`） |
| `[ls:目录]`、`[read:路径]`、`[write:路径\|内容]`、`[append:路径\|内容]`（AI 自主） | 模型判断需要时读写本机文件（需 `FILE_OPS_ENABLED`） |

> **AI 自主工具循环（agent 模式）**：模型输出上述指令时，程序在本机执行，并把**执行结果回填给模型**，
> 由模型基于真实结果生成自然语言回复后再发到会话 —— **原始命令输出不会直接贴到群里**
> （避免目录结构、内网 IP、文件内容等泄漏）。最多 5 轮、单轮最多 8 个工具调用；
> 日志可见 `[AGENT] 第 N 轮：模型请求 M 个工具调用` 与每次调用的结果摘要。

配置（`config.json` 顶层）：

```jsonc
{
  "EXEC_ENABLED": true,          // 命令执行总开关（默认 false）
  "EXEC_SCOPE": "private",       // owner=仅 OWNER_ID / private=仅私聊（推荐）/ all=私聊+群聊
  "OWNER_ID": "",                // EXEC_SCOPE=owner 时必填（你的用户 ID，见下方说明）
  "EXEC_ALLOW_FROM": [],         // 额外白名单（数组，空=不限制）
  "AI_EXEC_ENABLED": false,      // 允许模型自主输出 [exec:...]（默认 false）
  "FILE_OPS_ENABLED": false,     // 文件读写（默认 false）
  "EXEC_DENY_ENABLED": true,     // 危险命令黑名单（默认开启，建议保持）
  "EXEC_CWD": "",                // 命令与相对路径的基准目录（空=程序当前目录）
  "FILE_ROOTS": [],              // 允许写入的根目录（空=除系统目录外均可写）
  "FILE_DENY_SYSTEM": true       // 拒绝写入系统目录（Windows: C:\Windows / Program Files；
                                 // Linux: /etc /usr /bin /sbin /boot /sys /proc /dev）
}
```

**文件写入保护**：`/write`、`/append`、`[write:]`、`[append:]` 共用同一套校验：
系统目录（默认拒绝）→ 拒绝含 `..` 的路径（防穿越）→ 若配置了 `FILE_ROOTS`，路径必须落在其中。

**怎么填 `OWNER_ID`**：在群里/私聊发一句话，然后看 `logs/messages_YYYYMMDD.log` 里你自己那条记录的
`"sender_id"` 字段（也可先跑 `main.exe history --n 5`），把它填进 `OWNER_ID`，再把 `EXEC_SCOPE` 改成 `"owner"`。

> ⚠️ **风险提示**：`EXEC_SCOPE=all` 时，群里**任何人** @机器人 发 `/exec 任意命令` 都能在你的电脑上执行
> （删文件、装软件、上传数据皆可）。强烈建议改为 `"EXEC_SCOPE": "owner"` + 填好 `OWNER_ID`，
> 至少改为 `"private"`（仅私聊）。系统会拦下 `format/diskpart/shutdown/rm -rf /` 等破坏性命令，
> 但这只是兜底，无法覆盖所有危险操作。开启后启动时会打印安全提醒。

### 联网搜索 / 网页抓取

```jsonc
"WEB_SEARCH_ENABLED": true,
"SEARCH_API_URL": "",       // 留空 = 抓 Bing HTML；填 Tavily 兼容地址则走 API
"SEARCH_API_KEY": "",
"SEARCH_COUNT": 5,          // 1-10
"WEB_FETCH_ENABLED": true
```

- **AI 工具**：`[search:关键词]` 返回标题/链接/摘要；`[fetch:URL]` 抓网页正文（截断 4000 字）；
  结果都会回填给模型，再由模型组织回答；
- **手动命令**：`/search 关键词`、`/fetch URL`；
- 未配置 API 时抓 `cn.bing.com`（curl 自动跟随重定向、解压 gzip）；
  配置 `SEARCH_API_URL` 时按 Tavily 格式 `POST {"query","max_results"}` +
  `Authorization: Bearer <key>`，解析 `results[].title/url/content`；
- 安全：拒绝私有/环回地址（防 SSRF）；URL 含引号/空白/控制字符直接拒绝。

### MCP（Model Context Protocol）

```jsonc
"MCP_SERVERS": [
  { "name": "fs", "command": "npx",
    "args": ["-y", "@modelcontextprotocol/server-filesystem", "C:/path/to/yuanbao"],
    "env": {} }
]
```

- 启动时对每个 server 执行 `initialize` + `tools/list`，把工具清单注入系统提示词；
- 模型用 `[mcp:服务器.工具|{"参数":"值"}]` 调用，执行结果回填给模型继续推理；
- 每次调用启动一次 server（stdio），无需常驻进程管理；`/mcp` 查看状态与工具；
- 仓库自带 `mcp/test_server.js`（Node，极简 echo/add 工具）作为可运行的示例，正式使用时替换成真实 server 即可。

### Skill（OpenClaw / AgentSkills 兼容）

```
skills/<名称>/SKILL.md
```

- `SKILL.md` = YAML frontmatter（`name` / `description`）+ Markdown 正文，与 OpenClaw 一致；
- 启动时扫描 `skills/` 与 `SKILLS_DIRS`（可配置多个目录）；
- **渐进式披露**：提示词里只放 `name + description`，模型输出 `[skill:名称]` 时才读取正文，
  避免把全部技能塞进上下文；
- `/skills` 查看已加载技能。

### 子代理（Sub-Agent）

- 模型输出 `[agent:任务描述]` → 在**独立会话**中运行一个子代理（独立工具循环，最多 4 轮 / 6 个工具调用）；
- 子代理完成后把结论回填给主代理，由主代理组织成最终回复；
- 并发上限 `SUBAGENT_MAX_CONCURRENT`（默认 4）；**子代理不能再派生子代理**（防递归）；
- `/subagents` 查看运行中数量。

**AI 控制"思考中"气泡 / 发送节奏**（也接受 `[气泡:...]`）：

| 指令 | 效果 |
|---|---|
| `[bubble:on]` | 点亮并保活 30 秒 |
| `[bubble:on\|300]` | 保活 300 秒（每 2 秒续发） |
| `[bubble:on\|forever]` | 长时间保活（24 小时），适合很长的任务 |
| `[bubble:off]` | 立即收起 |
| `[bubble:off\|5]` | 5 秒后收起（之后发 `[bubble:on]` 可取消） |
| `[wait:3]` | 其后的内容等 3 秒再发（控制发送节奏，最多 60 秒） |

- 通常无需手动操作——被 @ / 私聊时自动点亮，回复完成自动收起；
- **手动保持优先**：AI 用过 `[bubble:on]` 后，**回复结束也不会被自动收起**（到保活时长或收到
  `[bubble:off]` 才收）；只有 AI 没控制过的气泡才会在回复完成时立刻收起；
- 日志：`[BUBBLE] AI 控制气泡: RUNNING 保活 Ns` / `FINISH`、`[BUBBLE] AI 手动保持气泡中…跳过自动收起`、
  `[AI] 等待 Nms 后继续发送`。

**AI 引用回复（默认开启）**：

- AI 被 @（群聊）或收到私聊时，回复会自动**引用触发它的那条消息**（对齐官方插件 `replyToMode: first`）
- 长回复分条时只引用首条；可用配置调整：`"REPLY_TO_MODE": "first"`（默认）/ `"all"`（每条都引用）/ `"off"`（不引用）
- 本地验证：`main.exe rich 群号 "收到" --ref <被引用消息ID> --account main`
  （消息 ID 可从 `logs/messages_*.log` 里的 `msg_id` 字段取）

**AI 刷屏（⚠️ 危险，仅在你明确要求时使用）**：

- 模型输出 `[flood:文本|次数|模式|间隔毫秒]` 会触发刷屏引擎（也接受 `[刷屏:...]`），例如
  `[flood:冲鸭|20|matrix]`；仅**群聊**、且需 `listen` 常驻（刷屏在后台执行，不受单条消息限制）
- **提示词里已明确写明"刷屏是一件很危险的事"**：会触发平台风控、可能被群管踢出甚至封号、严重骚扰群成员；
  除你明确要求外模型不会输出该指令，且会先提醒风险
- 本地直接验证（绕过模型，立即执行）：

```bash
main.exe rich 群号 "[flood:测试|3|random]" --account main
main.exe rich 群号 "[sticker:六六六] 大家好啊" --account main
main.exe rich dm:用户ID "[sticker:ok]" --account main
```

**上下文记忆**（默认开启）：

- 每个群 / 每个私聊会话单独维护上下文，保留**最近 100 条消息**（含别人的发言与 bot 自己的回复）；
- 触发 AI 时会把该会话这 100 条历史连同当前消息一起发给大模型，因此它能记住群里聊过什么、和谁聊过什么；
- 上下文落盘在 `logs/ai_context/<账号>_<会话>.json`，重启后依然保留；
- 可调整：

```jsonc
{
  "AI_CONTEXT_ENABLED": true,   // 关闭记忆设为 false
  "AI_CONTEXT_MESSAGES": 100    // 每个会话保留的历史条数
}
```

- 调试预览：`main.exe ai-reply 群号 用户ID "问题" --dry-run --account main`（只生成不发送）

**临时覆盖配置**（无需改文件）：

```bash
main.exe ai-test "你好" --account main \
  --llm-api-url https://api.deepseek.com/v1/chat/completions \
  --llm-api-key sk-xxx --llm-model deepseek-chat
```

> 可用参数：`--llm-api-url`、`--llm-api-key`、`--llm-model`、`--llm-system`、`--llm-max-tokens`、`--llm-temperature`。
> AI 回复流程结束（含回复为空/发送失败）会自动发送 FINISH 心跳收起"思考中"气泡。
>
> **被 @ 的判定**（是否自动回复 / 是否亮气泡）：只认消息 @ 元素（`TIMCustomElem` + `elem_type=1002`）的
> `user_id == 本 bot id`（对齐官方插件），不按昵称文本匹配 —— 群里 @ 另一个同样叫"元宝"的机器人不会误触发；
> 仅当平台完全没下发 `user_id` 时才退回昵称兜底。`listen` 日志会打印 `[@] 命中本 bot / 非本 bot ...` 供核对。
>
> **气泡收尾兜底**：断线期间发不出的 FINISH 会在连接恢复后自动补发；`listen` 收到 Ctrl+C 退出前会收尾所有活跃气泡；
> 进程被强杀（任务管理器结束进程 / `kill -9`）时，活跃会话落盘在 `logs/hb_sessions_<账号>.txt`，
> 下次启动 `listen` 会自动补发 FINISH 清理残留的"思考中"状态。

## ⚙️ 配置（config.json）

> ⚠️ **安全提醒**：`config.json` 含 `APP_SECRET` 等真实凭证，**切勿提交到公开仓库**。演示/示例请使用占位符。

### 多账号（多组 APP_KEY / APP_SECRET，必须命名）

账号统一配置在 `ACCOUNTS` 数组中，**每组 APP_KEY/APP_SECRET 都必须有 `NAME`**：

```jsonc
{
  "API_DOMAIN": "bot.yuanbao.tencent.com",
  "WS_URL": "wss://bot-wss.yuanbao.tencent.com/wss/connection",
  "HEARTBEAT_INTERVAL": 10,
  "MSG_LOG_ENABLED": true,
  "ACCOUNTS": [
    { "NAME": "main", "APP_KEY": "KEY1", "APP_SECRET": "SECRET1",
      "LISTEN_GROUPS": ["群号"], "YUANBAO_ID": "" },
    { "NAME": "bot2", "APP_KEY": "KEY2", "APP_SECRET": "SECRET2" }
  ]
}
```

命令行**必须**使用 `--account` 指定账号（不带会报错并列出可用账号）：

```bash
main.exe send-group 群号 "你好" --account main   # 指定账号发送
main.exe members 群号 --account 1                # 按序号选择（从 0 开始）
main.exe listen --account all                    # 全部账号并行监听
main.exe status --account all                    # 依次显示全部账号
```

- `NAME` 必填：缺失时程序直接报错（`ACCOUNTS[i] 缺少 NAME`）。
- 顶层 `APP_KEY/APP_SECRET` 写法仍兼容，但顶层同样必须提供 `NAME` 字段；推荐统一写入 `ACCOUNTS`。
- 每个账号各自独立的 `LISTEN_GROUPS` / `YUANBAO_ID`；消息日志与心跳状态文件按账号隔离（均在 `logs/`）。
- `listen` 默认监听**机器人所在的所有群**（不限于 `LISTEN_GROUPS`：任意群的 @、他人聊天都会进入日志与 AI 上下文）；如只需监听配置的群，加 `--only-listen-groups`。

```jsonc
{
  "APP_KEY": "你的 APP_KEY",               // 元宝开放平台应用密钥
  "APP_SECRET": "你的 APP_SECRET",
  "API_DOMAIN": "bot.yuanbao.tencent.com",
  "WS_URL": "wss://bot-wss.yuanbao.tencent.com/wss/connection",
  "DEFAULT_GROUP_CODE": "主群号",          // 默认主群（必须，且始终在监听列表）
  "LISTEN_GROUPS": [],                    // 多群监听列表：可留空，首次运行时自动同步所有已加入群
  "YUANBAO_ID": "",                       // Bot 实例 ID：可留空，连接鉴权成功后自动获取并回写
  "HEARTBEAT_INTERVAL": 10,
  // ── 大模型（可选）──
  "LLM_API_URL": "https://api.openai.com/v1/chat/completions",
  "LLM_API_KEY": "",
  "LLM_MODEL": "gpt-3.5-turbo",
  "LLM_SYSTEM_PROMPT": "你是一个友好的QQ群聊机器人助手……",
  "LLM_MAX_TOKENS": 200,
  "LLM_TEMPERATURE": 0.8,
  "LLM_TIMEOUT": 15,
  // ── 代理转发模式 ──
  "FORWARD_MODE_ENABLED": false,
  "FORWARD_AT_ONLY": false,               // 仅 @元宝 时转发
  "FORWARD_AT_YUANBAO": true,             // 转发时 @元宝 确保回复
  // ── 消息记录 / 撤回监控 ──
  "MSG_LOG_ENABLED": true,
  "RECALL_MONITOR_ENABLED": false
}
```

> **自动获取**：`YUANBAO_ID` 连接鉴权成功后自动回写；`LISTEN_GROUPS` 留空时启动自动同步所有已加入群，收到新群消息自动加入监听。两者均可在前端「元宝派」面板逐群开关，运行时改动自动写回 `config.json`。

---

## 🧩 API 概览

| 分组 | 端点 |
|---|---|
| 连接 | `GET/POST /api/connect` `POST /api/disconnect` `GET /api/bot_status` `GET /api/status` `GET /api/health` |
| 心跳 | `GET /api/heartbeat` `POST /api/heartbeat/interval` |
| 发送 | `POST /api/send` `POST /api/send-reply` `POST /api/send-at` `POST /api/send-multi-at` `POST /api/send/at-all` `POST /api/send/ai-image` `POST /api/send-image` `POST /api/send-sticker`（含 count/interval 刷屏） `POST /api/send-file` |
| 消息 | `GET /api/messages` `POST /api/messages/clear` `GET /api/messages/logs` |
| 群 / 监听 | `GET /api/groups` `POST /api/groups/switch` `POST /api/groups/listen` `GET /api/members` |
| 贴纸 | `GET /api/stickers` |
| 刷屏 | `POST /api/flood/start` `POST /api/flood/cancel` `GET /api/flood/stats` `GET /api/flood/list` |
| 代理转发 | `POST /api/forward-mode/{enable,disable,clear-queue,toggle-at-yuanbao}` `GET /api/forward-mode/{config,queue}` |
| 大模型 | `GET/POST /api/llm/config` `POST /api/llm/test` |
| 消息日志 | `GET /api/msg-log/{stats,files,recent}` `POST /api/msg-log/{enable,disable,toggle,clear-today}` `GET /api/msg-log/download` |
| 插件 | `GET /api/plugins` `POST /api/plugins/{install,toggle,reload}` |
| 设置 | `GET/POST /api/settings` |
| SSE | `GET /api/events` `/events` |

---

## 🧩 插件系统（JSON 配置插件）

Python 版可用 `importlib` 动态加载代码，C++ 无等价机制，因此采用**声明式 JSON 插件**：

- 每个插件一个 JSON 文件，放在 `plugins/` 目录（自动创建），命名 `插件名.json`
- 后端启动 / `/api/plugins/reload` 时自动扫描加载
- 收到监听群消息时，内容以命令前缀开头即自动回复对应文本
- 命令匹配**优先于 AI 回复**；`@昵称 命令` 形式也会被识别

```json
{
  "name": "example",
  "version": "1.0.0",
  "author": "yuanbao",
  "description": "示例插件：/ping 回复 pong",
  "active": true,
  "commands": [
    { "command": "/ping", "reply": "pong! 🏓" },
    { "command": "/time", "reply": "现在是北京时间" }
  ]
}
```

字段说明：`name`（必须，即文件名）/ `version` / `author` / `description` / `active`（默认 true）/ `commands`（命令列表，必填）。

- `command`：命令前缀，消息以它开头即触发（支持 `/ping` 或 `@元宝 /ping`）
- `reply`：回复文本，支持 `{user}` 占位

管理方式：前端「设置 → 插件」页可安装（粘贴 JSON 或 URL 下载）、启用 / 禁用 / 重载；也可直接放 JSON 文件到 `plugins/` 后调 `/api/plugins/reload`。

---

## 📁 目录结构

```
├── main.cpp                 # 唯一源码文件（纯 C++17 CLI，无 Web/前端）
├── config.example.json      # 配置模板：复制为 config.json 并填入凭证（config.json 不入库）
├── README.md
├── LICENSE                  # AGPL-3.0
├── .gitignore
├── mcp/test_server.js       # 极简 MCP 测试服务器（echo / add）
├── skills/example/          # 机器人技能示例（OpenClaw / AgentSkills 兼容格式）
├── .dsh/skills/yuanbao-cli/ # 供 AI Agent 操作本 CLI 的技能说明
└── logs/                    # 运行期自动生成：消息日志与已知群列表（不入库）
```

---

## 📝 说明

- 本仓库为**纯 C++17 实现**，无需 Python 运行时；编译产物为单个可执行文件，不监听任何 HTTP 端口。
- 凭证只保存在本地 `config.json`（已被 `.gitignore` 排除），**切勿提交到公开仓库**。
- `LISTEN_GROUPS` 第一项为默认目标群（命令中群号可写 `default`）；多账号用 `--account` 指定。

## 🔒 许可证

本项目按 [GNU Affero General Public License v3.0](./LICENSE) 发布。

本程序通过网络（WebSocket）与用户交互：按 AGPL-3.0 第 13 条，若你修改本程序并部署为网络服务，须向使用该服务的用户提供修改后的对应源码。

---

[![](https://img.shields.io/badge/powered_by-dsh-4D6BFE?style=flat-square&logo=deepseek&logoColor=white)](https://github.com/deepseek-ai/deepseek-harness)
