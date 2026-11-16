---
name: example
description: 示例技能（演示 OpenClaw / AgentSkills 兼容格式）：用户要求"演示技能"时使用
---

# 示例技能

这是一个与 OpenClaw / AgentSkills 兼容的技能示例。

## 使用步骤

1. 先运行 `[exec:echo hello]` 确认工具可用。
2. 用一句话把结果汇报给用户。

## 说明

- 技能目录放在 `skills/<名称>/SKILL.md`，文件头是 YAML frontmatter（name / description）。
- 模型平时只能看到 name + description（渐进式披露），需要细节时才输出 `[skill:example]` 读取正文。
