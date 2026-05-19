# RSS × ACE-DEV 兼容层

**将 ACE-DEV 医疗系统的生理状态桥接到 Realistic Stamina System，让受伤影响体力，治疗促进恢复。**

---

## 概述

[Realistic Stamina System (RSS)](https://github.com/ViVi141/Realistic-Stamina-System) 是一个基于 Pandolf 能量消耗公式的拟真体力模组。[ACE-DEV](https://github.com/acemod/ACE-Anvil) 是 Advanced Combat Environment 的开发版本，提供完整的医疗、药物和生理模拟系统。

两个模组各自独立运行时，ACE-DEV 的出血、疼痛、药物代谢状态不会影响 RSS 的体力消耗。

本兼容层在这两者之间搭建桥梁，让 ACE-DEV 的生理/药理状态成为 RSS 体力模型的一部分。

---

## 功能

### 伤势 → 体力消耗

| 链路 | 效果 | 说明 |
|------|------|------|
| **出血** | stamina 消耗 +0~40% | 失血→携氧能力↓→心肺代偿 |
| **疼痛** | stamina 消耗 +0~20%（仅移动时） | 疼痛→运动效率降低 |
| **健康值下降** | stamina 消耗 +0~30%（濒死时最大） | 组织损伤→全身性应激 |
| **内源性肾上腺素** | 轻伤代偿 ~95%，重伤代偿 ~40% | 身体自动释放儿茶酚胺对抗疲劳 |

### 药物 → 体力影响（基于 PK/PD 实时浓度）

| 药物 | 效果 | 生理依据 |
|------|------|---------|
| **肾上腺素** | 浓度正比 stamina 回补 + 代谢反弹 | 交感兴奋→心输出量↑→短暂体能提升 |
| **吗啡** | 疼痛抑制→降低 pain drain | 经 ACE 内部处理，兼容层读取已抑制后的疼痛值 |
| **美托洛尔**（β受体阻滞剂） | stamina 消耗 +0~15%（移动时） | 心率上限受抑→运动时心输出量不足 |
| **去氧肾上腺素**（血管收缩剂） | stamina 消耗 +0~10% | 外周血管收缩→肌肉血流减少 |
| **纳洛酮**（阿片拮抗剂） | stamina 微量额外消耗 | 拮抗吗啡+撤药应激 |

> 所有药物效果基于 ACE-DEV 的药代动力学（PK）系统实时浓度计算，而非硬编码定时器。

### 治疗 → 体力恢复

| 链路 | 效果 |
|------|------|
| **止血**（出血率下降） | stamina 恢复加速 |
| **治疗**（健康值上升） | stamina 恢复加速 |

---

## 设计原则

- **不修改 RSS 和 ACE-DEV 源码**：纯兼容层，双方独立升级不受影响
- **能量消耗率模型**：ACE-DEV 的影响表达为 RSS 基准消耗率的乘数，而非固定绝对值。静止时 RSS 消耗率 ≈0，ACE 额外消耗 ≈0；冲刺时 RSS 消耗率高，ACE 额外消耗按比例增加
- **PK/PD 连续模型**：药物效果基于实时血药浓度计算，随代谢自然升降，无硬编码定时器
- **ACE-DEV 未加载时自动降级**：所有 ACE-DEV 类型引用通过 `Cast()` 调用并判空，ACE 不存在时兼容层静默不工作
- **对所有角色生效**：玩家和 AI 都受影响

---

## 链路架构

```
ACE-DEV 状态                    兼容层读取（Bridge）          stamina 影响
────────────────────────────────────────────────────────────────────────
出血量 GetTotalBleedingAmount() → 0~1 连续值              消耗 +0~40%
疼痛强度 GetPainIntensity()     → 0~1（已含吗啡抑制）       消耗 +0~20%（移动时）
健康值 GetHealthScaled()       → 0~1 连续值              消耗 +0~30%
肾上腺素 PK 浓度 (nM)           → 归一化 0~1              回补 + 浓度下降惩罚
美托洛尔 PK 浓度 (nM)           → 归一化 0~1              消耗 +0~15%（移动时）
去氧肾上腺素 PK 浓度 (nM)        → 归一化 0~1              消耗 +0~10%
纳洛酮 PK 浓度 (nM)             → 归一化 0~1              微量额外消耗
```

---

## 项目结构

```
scripts/Game/RSS_ACE_Compat/
├── SCR_RSS_ACE_Constants.c      — 可调常量（代谢乘数/药物浓度基准/阈值）
├── SCR_RSS_ACE_Bridge.c         — ACE-DEV 状态读取桥接器（含 PK/PD 浓度读取）
└── SCR_RSS_ACE_PlayerBase.c     — modded SCR_CharacterControllerComponent 主入口
```

---

## 依赖

- [Realistic Stamina System (RSS)](https://github.com/ViVi141/Realistic-Stamina-System) — 必需 · AGPL-3.0
- [ACE-DEV](https://github.com/acemod/ACE-Anvil) — 必需 · GPL-2.0

---

## 安装

1. 将本 addon 放入 Arma Reforger Workbench 的 `addons/` 目录
2. 在 Workbench 中打开项目并编译
3. 确保 RSS 和 ACE-DEV 同时加载

---

## 兼容性

- 与 RSS 的 combat stim（苯甲酸钠咖啡因注射液）不冲突，两者独立生效
- 所有 ACE-DEV 的医疗物品（绷带、吗啡、肾上腺素、美托洛尔等）正常使用
- 药物效果基于 ACE-DEV PK/PD 系统实时浓度计算
- AI 角色同样受 ACE-DEV 伤害和药物影响

---

## 许可证

AGPL-3.0

---

## 作者

**ViVi141** — 兼容层设计 & 实现
