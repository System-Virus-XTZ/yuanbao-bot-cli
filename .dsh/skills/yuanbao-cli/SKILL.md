---
name: yuanbao-cli
description: "通过命令行操作腾讯元宝 Bot（yuanbao / main.exe）：发送群消息、私聊、@ 成员、贴纸、图片、文件、刷屏，并查询群列表、群成员与本地消息历史。"
whenToUse: "当用户要求用元宝机器人或你的分身发消息、发贴纸/图片/文件、@某人、刷屏，或查询群成员/群名/消息历史时使用。"
---

# 元宝 Bot CLI（yuanbao）

通过命令行工具 `yuanbao`（Windows 为 `main.exe`）操作腾讯元宝开放平台 Bot。
所有命令都是**一次性执行、执行完即退出**（连接 -> 鉴权 -> 执行 -> 输出 -> 退出）。

## 1. 先定位可执行文件

按以下顺序确定命令入口，不要凭猜测执行：

1. 环境变量 `YUANBAO_EXE` 指定的路径；
2. 当前工作目录下的 `main.exe`（Windows）/ `./yuanbao`（Linux/macOS）；

> 若都找不到，先询问用户 yuanbao 可执行文件所在路径，不要继续。
> Windows 上请用 PowerShell/cmd 执行；含中文参数时用英文双引号包裹整个参数。

## 2. 硬性规则（必须遵守）

1. **禁止执行 `listen`**：它是常驻阻塞命令（Ctrl+C 才退出），只会卡住会话，仅供人工使用。
2. **串行执行**：同一时间只跑一条 yuanbao 命令，不要并发。
3. **需要联网的命令需要凭证**：`config.json` 中的 `APP_KEY` / `APP_SECRET` 为空时，命令会提示"未完整配置"，此时应告知用户先填写凭证，不要反复重试。
4. **超时设置 ≥ 30 秒**：发送/上传类命令约需 1~3 秒，图片/文件上传更久。
5. **群号可用 `default`**：表示 config.json 中监听列表第一项（默认目标群）。
6. **文本含空格必须加英文双引号**；中文参数同理。命令是 UTF-8 输出，不要按 GBK 解读。
7. `--config <路径>` 是全局选项，可放在命令任意位置，默认 `config.json`。
8. **多账号（--account 必填）**：`config.json` 的 `ACCOUNTS` 数组配置多组 APP_KEY/APP_SECRET，**每组 `NAME` 必填**。任何命令都必须显式传 `--account <名称|序号|all>`（仅 `help` 例外），否则程序报错退出。示例：`main.exe send-group 群号 "你好" --account main`；`main.exe listen --account all` 并行监听全部账号。
9. **AI 回复**：`ai-test <文本>` 只测模型（不发送）；`ai-reply <群号> <用户ID> <文本>` 生成回复并 @ 提问者发送到群（`--no-at` 可关闭 @，`--dry-run` 只预览不发送）；`ai-c2c <用户ID> <文本>` 私聊生成回复；`listen` 常驻时**被 @ 或收到私聊会自动 AI 回复**（配置了 LLM 即默认开启，`--no-ai` 关闭）。需要 `LLM_API_URL` + `LLM_API_KEY`（写在 config.json 全局或账号内），也可用 `--llm-api-url/--llm-api-key/--llm-model` 临时覆盖。
    - **AI 回复支持贴纸 / @（自动注入提示词，无需配置）**：模型写 `[sticker:贴纸名]`（也接受 `[贴纸:名]`、`[表情:名]`）会发成贴纸消息，例如 `[sticker:六六六]`；写 `@昵称` 会转成真实 @（仅群聊，需能匹配到群成员，成员缓存缺失时自动拉取一次、约数秒）。能力说明与内置 59 个贴纸名会随 system prompt 一起下发，日志出现 `[AI] 贴纸 xxx 已发送` 即成功。
    - **AI 刷屏（⚠️ 危险，默认不主动用）**：模型输出 `[flood:文本|次数|模式|间隔ms]`（也接受 `[刷屏:...]`）会触发刷屏引擎，例如 `[flood:冲鸭|20|matrix]`，仅群聊、`listen` 常驻时执行；提示词已明确写"刷屏很危险（触发风控/被踢/封号）"，除用户明确要求外模型不会使用。可用 `rich` 命令本地验证：`main.exe rich 群号 "[flood:测试|3|random]" --account main`。
    - **AI 引用回复（默认开启）**：被 @ 或私聊触发时，回复会引用触发它的那条消息（配置 `REPLY_TO_MODE`：`first` 默认 / `all` / `off`）；长回复分条时只引用首条；本地验证 `rich 群号 "收到" --ref <消息ID>`（消息ID 从 `logs/messages_*.log` 的 `msg_id` 取）。
    - **AI 控制"思考中"气泡 / 发送节奏**（也接受 `[气泡:...]`）：`[bubble:on]`（保活 30s）、`[bubble:on|300]`（300s）、`[bubble:on|forever]`（24h）、`[bubble:off]`（立即收）、`[bubble:off|5]`（5s 后收，可被 on 取消）、`[wait:3]`（后续内容等 3 秒再发，最多 60s）。一般无需手动（回复完成自动收起）。**手动保持优先**：用过 `[bubble:on]` 后，回复结束不会被自动收起（到时长或 `[bubble:off]` 才收）。日志：`[BUBBLE] ... RUNNING 保活 Ns / FINISH`、`AI 手动保持气泡中…跳过自动收起`、`[AI] 等待 Nms 后继续发送`。本地验证 `rich dm:用户ID "[bubble:on|forever]"`。
    - **命令执行 / 文件读写（⚠️ 高风险，由 config 的 EXEC_ENABLED / FILE_OPS_ENABLED 控制，默认关闭）**：固定命令 `/exec <命令>`、`/ls`、`/read <路径>`、`/write <路径>|<内容>`、`/append <路径>|<内容>`；AI 自主指令 `[exec:命令]`（需 `AI_EXEC_ENABLED`）与 `[ls:目录]`/`[read:路径]`/`[write:路径|内容]`/`[append:路径|内容]`（需 `FILE_OPS_ENABLED`）。
      **AI 自主执行走工具循环（agent）**：模型输出指令 → 本机执行 → **结果回填给模型** → 模型产出自然语言回复（原始输出不会直接贴到群里）；最多 5 轮、单轮最多 8 个调用。
      权限由 `EXEC_SCOPE`（owner/private/all，**推荐 private**）+ `OWNER_ID` + `EXEC_ALLOW_FROM` 控制；内置危险命令黑名单（`EXEC_DENY_ENABLED`，拦 format/diskpart/shutdown/rm -rf 等）。
      文件写入另有保护：系统目录默认拒绝（`FILE_DENY_SYSTEM`）、拒绝含 `..` 的路径、可配 `FILE_ROOTS` 限定可写根目录；`/write`、`/append`、`[write:]` 共用。日志关键字：`[AGENT]`、`[EXEC]`、`[FILE]`。执行命令/改文件前（尤其破坏性操作）先向用户确认；本地验证 `rich dm:用户ID "[ls:.]"`、`rich dm:用户ID "[exec:echo test]"`。
10. **AI 上下文记忆**：默认开启，每个群/私聊会话保留最近 100 条消息（含 bot 自己的回复），触发时随历史一起发给模型；落盘在 `logs/ai_context/`，重启后保留。可用 config 的 `AI_CONTEXT_ENABLED` / `AI_CONTEXT_MESSAGES` 调整。
11. **被 @ 的判定标准**（决定是否自动回复 / 是否点亮气泡）：只认消息里 @ 元素（`TIMCustomElem` + `elem_type=1002`）的 `user_id == 本 bot id`（对齐官方插件 custom handler），**不按昵称文本匹配** —— 群里 @ 另一个昵称同样叫"元宝"的机器人（或昵称相同的成员）不会触发。仅当平台完全没有下发 `user_id` 时才退回昵称文本兜底。`listen` 日志会打印 `[@] 命中本 bot / 非本 bot user_id=... text=... bot_id=...`，便于核对。

## 3. 命令速查

### 连接 / 查询

```text
connect                     连接测试（鉴权成功后退出）
groups                      列出本机已知群（logs/known_groups.json）
members <群号>              查询群成员 / 群名 / 群主
history [--group 群号] [--n 条数]   读取本地消息日志最近记录
stickers                    列出全部内置贴纸（名称为中文，可传名称或编号）
status                      显示配置摘要
rich <群号|dm:用户ID> <文本...>   本地发送富文本（解析 [sticker:名] / [flood:文本|次数|模式|间隔] / @昵称）
help                        查看帮助（-h / --help / /? 等价）
```

### 发送（自动连接，发完退出）

```text
send-group <群号> <文本...>                    群消息（别名 send / sg）
send-c2c <用户ID> <文本...>                    私聊（别名 dm / sc）
send-at <群号> <用户ID> <文本...> [--nick 昵称]  @某人（别名 sat）
send-multi-at <群号> <文本...> --users "u1[:昵称],u2[:昵称]"   @多人（别名 smat）
send-at-all <群号>                             @全体成员（别名 at-all）
send-reply <群号> <被引用消息ID> <文本...> [--at 用户ID] [--at-nick 昵称]   引用回复（别名 sr）
send-sticker <群号> <贴纸名或编号> [文本...] [--at 用户ID] [--at-nick 昵称]
             [--count 次数] [--interval 间隔毫秒]     发贴纸（别名 sticker）
send-image <群号> <本地图片或 http(s)://URL> [--at 用户ID] [--at-nick 昵称]   发图片（别名 image / img）
send-file <群号> <本地文件路径>                发文件（别名 file）
send-video <群号> <本地视频文件路径>           发视频（以文件消息发送，别名 video）
```

### AI 回复

```text
ai-reply <群号> <用户ID> <文本...> [--nick 昵称] [--no-at]   生成 AI 回复并发送到群
ai-c2c <用户ID> <文本...>                                    生成 AI 回复并私聊发送
ai-test <文本...>                                            只调用大模型打印回复（不发送）
listen                                                       常驻；默认监听机器人所在的所有群，被 @ 或私聊自动 AI 回复
                                                             （--no-ai 关闭 AI；--only-listen-groups 仅监听 config 配置群）
```

### 联网搜索 / 网页抓取

- AI 工具：`[search:关键词]`（返回标题/链接/摘要）、`[fetch:URL]`（网页正文，截断 4000 字），结果回填给模型；
- 手动命令：`/search 关键词`、`/fetch URL`；
- 配置：`WEB_SEARCH_ENABLED` / `SEARCH_API_URL`（留空用 Bing HTML，填 Tavily 兼容 API）/ `SEARCH_API_KEY` /
  `SEARCH_COUNT`（1-10）/ `WEB_FETCH_ENABLED`；
- 安全：拒绝私有/环回地址（防 SSRF）、URL 非法字符拒绝；日志 `[SEARCH]`、`[FETCH]`。

### MCP / Skill / 子代理（AI 扩展能力）

- **MCP**：`config.json` 的 `MCP_SERVERS` 配置 stdio server（`{name, command, args[], env{}}`），
  启动时自动 `initialize` + `tools/list`，工具清单注入提示词；模型用 `[mcp:服务器.工具|{JSON}]` 调用，
  结果回填给模型。`/mcp` 查看状态。仓库自带 `mcp/test_server.js` 示例。
- **Skill（OpenClaw 兼容）**：`skills/<名称>/SKILL.md`（YAML frontmatter `name`/`description` + Markdown 正文）；
  只把 name+description 注入提示词，模型输出 `[skill:名称]` 才加载正文（渐进式披露）。`/skills` 查看。
- **子代理**：模型输出 `[agent:任务]` 派生独立会话的子代理（多轮工具调用），结论回填主代理；
  并发上限 `SUBAGENT_MAX_CONCURRENT`，子代理不可再派生。`/subagents` 查看。

### 斜杠命令（元宝客户端内使用；本地也可用 `slash` 模拟触发）

```text
/help     显示可用命令            /status  显示机器人状态
/new      开始新会话（清空上下文）  /reset   同 /new
/stop     停止正在生成的回复       /compact 压缩会话上下文
/retry    重新生成上一条回复

# 信息 / 工具（对齐 yuanbao-lite）
/time /uptime /version /ping     当前时间 / 运行时长 / 版本 / 响应测试
/whoami                          你的用户ID、昵称、会话类型
/calc (1+2)*3                    四则运算（支持括号、小数）
/echo <文本>                     复读
/commands                        列出全部命令

# 群 / 历史
/groups                          本机已知群
/groupinfo [群号]                群名 / 成员数 / 群主（缺省用当前群）
/members [群号]                  群成员列表（最多 100 人）
/history [条数]                  最近消息（默认 10，最多 50）
/inspect [序号]                  消息详情（1=最新）

# 定时 / 媒体
/remind <时间> <内容>            一次性提醒（10s/5m/2h/1d 或 18:30），如 /remind 10m 喝水
/cron <间隔> <内容>              周期任务（最小 1 分钟），如 /cron 1h 起身活动
/cron list | /cron del <ID>      查看 / 删除定时任务（持久化 logs/tasks.json）
/sticker <名称>                  发贴纸，如 /sticker 六六六
/stickers                        列出可用贴纸
/img <URL|本地路径>              发图片（群聊，自动上传 COS）
/file <本地路径>                 发文件（群聊，自动上传 COS）
/reply [消息ID] <文本>           引用回复（缺省引用最近一条）

# 运维（对齐官方插件）
/restart                         软重启：重新加载 config.json 并重连（进程不退出）
/yuanbao-upgrade                 真正执行升级：下载源码（UPGRADE_SOURCE_URL）或用本地源码 → 编译 → 替换 → 自动重启
/yuanbaobot-upgrade              同 /yuanbao-upgrade（官方定义为同义命令）
/issue-log                       导出日志包（仅私聊；群里会引导到私聊；不含密钥）

slash <群号|dm:用户ID> <命令...>   本地模拟平台下发命令（按目标会话真实执行并回复）
                                  例: main.exe slash dm:用户ID /status
                                      main.exe slash 群号 "/new"
```

- 连接成功时会把命令清单自动同步到元宝平台（`sync_information`），平台侧才会有原生斜杠命令菜单；
  日志出现 `命令列表已同步到平台（内置 8 + 插件 28 个命令）` 即为已发送。
- **命令同步分两组**（对齐官方协议）：`botCommands`（框架内置：/help /status /new /reset /stop /compact /retry /restart）
  与 `pluginCommands`（插件自定义：/issue-log、/yuanbao-upgrade、/remind 等）。
  ⚠️ 自定义命令若混进 `botCommands`，平台会当成"未知内置命令"而**灰显不可用** —— 排查灰显问题时先看这里。
- **群聊里命令需 @机器人** 才生效（对齐官方插件行为）；私聊直接发送即可。
- 命令由 `listen` 处理并回复，因此**命令要能用，必须先跑着 `listen`**（一次性命令不会处理收到的消息）。
- 用户反馈"命令不能用"时：① 确认 `listen --account <账号>` 正在运行；② 群聊里是否 @了机器人；
  ③ 用 `slash` 在本地验证链路（能收到回复说明本地正常，问题在平台消息是否下发）。

### 刷屏

```text
flood <群号> <文本...> [--count 次数] [--interval 间隔毫秒] [--mode 变换]   （别名 flood-text）
```

`--mode` 可选值：
`random` / `fullwidth` / `mock` / `zalgo` / `repeat` / `alternate` / `emoji` / `ghost` / `matrix` / `earthquake` / `bubble` / `firework` / `rainbow` / `storm` / `tsunami` / `quantum`

## 4. 典型任务示例

> 以下所有命令都必须追加 `--account <账号名>`（例如 `--account main`），否则程序会报错退出。

**查群成员，然后 @某人**
```bash
main.exe members 123456
main.exe send-at 123456 用户ID "你好" --nick 昵称
```

**给群发消息**
```bash
main.exe send-group 123456 "大家好"
```

**发本地图片（自动上传）**
```bash
main.exe send-image 123456 "D:\pics\a.png"
```

**发贴纸（可带文字、可重复）**
```bash
main.exe send-sticker 123456 六六六 666 --count 3 --interval 500
```

**刷屏**
```bash
main.exe flood 123456 "冲鸭" --count 20 --interval 200 --mode matrix
```

**查看某群最近 30 条消息**
```bash
main.exe history --group 123456 --n 30
```

**列群 / 列贴纸**（群号、贴纸名不确定时先查）
```bash
main.exe groups
main.exe stickers
```

## 5. 结果与错误处理

- 成功时程序会输出 `[OK] 已发送` 等；失败时会输出 `[失败] ...`。成功后你无需发送“已成功发送”这类消息（成功后不用发送任何消息），如果失败需要把原始错误消息发给用户。
- `config.json 未配置 APP_KEY / API_DOMAIN` -> 缺少凭证，提示用户填写 config.json。
- `URL 下载失败` -> 发 URL 图片需要系统存在 `curl`，否则改用本地文件路径。
- 非 0 退出码或 stderr 报错时，把**原始错误信息**反馈给用户，不要自行编造原因。
- `send` 类命令只表示"帧已发出"（服务端无同步回执），若用户反馈未收到，建议用 `listen` 人工确认或检查是否被平台风控拦截。

## 6. 思考气泡（回复状态心跳 RUNNING / FINISH）

- `listen` 常驻模式下，收到 **@ 群消息** 或 **私聊消息** 会自动发送 `RUNNING` 心跳，点亮并维持对方的"加载中/思考中"气泡（每 2 秒续发，30 秒无收尾自动停止）。
- **回复发送完成会自动收尾**：`send-group` / `send-c2c` / `send-at` / `send-multi-at` / `send-reply` / `send-sticker` / `send-image` / `send-file` / `flood` 成功后，会自动对该会话发送 `FINISH`，并通知同目录下运行的 `listen` 进程停止 RUNNING。
- 因此请让 `listen` 与回复命令使用**同一个工作目录**（共享 `logs/`），气泡链路才完整；直接发送（未先监听）时没有气泡需要收尾，不影响消息发送。
- **残留兜底**：断线期间发不出的 FINISH 会在连接恢复后自动补发；`listen` 收到 Ctrl+C 退出前会收尾全部活跃气泡；进程被强杀（`kill -9` / 任务管理器）时活跃会话已落盘 `logs/hb_sessions_<账号>.txt`，下次启动 `listen` 会自动补发 FINISH 清理残留的"思考中"状态。若用户反馈"元宝一直显示思考中"，先跑一次 `listen`（几秒即退出）完成清场。
- 协议：群 `send_group_heartbeat`（from_account/to_account/group_code/send_time/heartbeat），私聊 `send_private_heartbeat`（from_account/to_account/heartbeat）；`heartbeat` 值：1=RUNNING，2=FINISH。
- 排查气泡：`listen` 日志出现 `[气泡] FINISH 群 xxx 已发送` / `[气泡] FINISH 私聊 xxx 已发送` 表示收尾帧已发出；
  若出现 `[气泡] 心跳被平台拒绝 code=...`，把 code 反馈（群/私聊会话类型不能混用，程序已按会话自动换算）。

## 7. 平台说明

- Windows：可执行文件为 `main.exe`。
- Linux/macOS：需用 `main.cpp` 重新编译为 `yuanbao` 后使用（`g++ -std=c++17 -O2 main.cpp -o yuanbao -lpthread -lrt -lm -lssl -lcrypto`）。
- 可执行文件所在目录需能读取 `config.json`（或通过 `--config` 指定完整路径）。
