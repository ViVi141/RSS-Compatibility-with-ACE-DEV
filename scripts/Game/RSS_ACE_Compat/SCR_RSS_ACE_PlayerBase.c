//------------------------------------------------------------------------------------------------
//! RSS ↔ ACE 兼容层主入口
//! modded class SCR_CharacterControllerComponent — 与 RSS 同一扩展类
//!
//! 实现策略：
//! 所有 ACE 效果通过 stamina 通道统一作用于 RSS：
//!   - 出血 → stamina drain ↑ → RSS 自然降低速度
//!   - 疼痛 → stamina drain ↑ → RSS 自然降低速度
//!   - 肾上腺素（浓度）→ stamina boost → RSS 自然提升速度
//!   - 吗啡（浓度）→ 疼痛抑制 → 减少 pain drain
//!   - 美托洛尔（浓度）→ β阻滞 → stamina drain ↑
//!   - 去氧肾上腺素（浓度）→ 血管收缩 → stamina drain ↑
//!   - 纳洛酮（浓度）→ 拮抗吗啡 → pain drain 回归
//!
//! 不直接调用 OverrideMaxSpeed，避免与 RSS 的速度控制冲突。
//! 不读取 RSS 的 protected 成员，避免跨 addon 编译问题。
//!
//! 使用独立 200ms CallLater 监控循环，安全读取 ACE 状态。
//! ACE 未加载时自动降级。
//!
//! v2 — 基于 ACE-DEV PK/PD 实时药物浓度的连续模型
//------------------------------------------------------------------------------------------------
modded class SCR_CharacterControllerComponent
{
	//==============================================================================================
	// 成员变量
	//==============================================================================================

	//! ACE 是否已检测加载
	protected bool m_bACE_Detected = false;
	//! 监控循环是否正在运行
	protected bool m_bACE_IsMonitoring = false;
	//! 角色已删除标记（防止 CallLater use-after-free）
	protected bool m_bACE_IsDeleted = false;

	//! 缓存的体力组件引用（与 RSS 共用同一组件）
	protected SCR_CharacterStaminaComponent m_pACE_StaminaComponent;

	// ── 出血 → 消耗 ──
	protected float m_fACE_BleedingAmount = 0.0;

	// ── 疼痛 → 消耗 ──
	protected float m_fACE_PainIntensity = 0.0;

	// ── 综合健康值（钝击/爆炸等直接扣 HP 的伤害） ──
	protected float m_fACE_HealthScaled = 1.0;

	// ── 肾上腺素 浓度驱动 ──
	protected float m_fACE_EpiLevel = 0.0;
	protected float m_fACE_EpiPrevLevel = 0.0;
	protected bool m_bACE_EpiInDecline = false;

	// ── 吗啡 浓度 ──
	protected float m_fACE_MorphineLevel = 0.0;
	protected float m_fACE_PainSuppression = 0.0;

	// ── 美托洛尔 浓度 ──
	protected float m_fACE_MetoprololLevel = 0.0;

	// ── 去氧肾上腺素 浓度 ──
	protected float m_fACE_PhenylephrineLevel = 0.0;

	// ── 纳洛酮 浓度 ──
	protected float m_fACE_NaloxoneLevel = 0.0;

	// ── 能量消耗追踪 ──
	protected float m_fACE_PreviousStamina = -1.0;

	// ── 伤势好转追踪（治疗→恢复加速） ──
	protected float m_fACE_PrevBleedingAmount = 0.0;
	protected float m_fACE_PrevPainIntensity = 0.0;
	protected float m_fACE_PrevHealthScaled = 1.0;

	//==============================================================================================
	// OnInit
	//==============================================================================================
	override void OnInit(IEntity owner)
	{
		super.OnInit(owner);

		if (!owner || !GetGame().InPlayMode())
			return;

		// 缓存体力组件
		m_pACE_StaminaComponent = SCR_CharacterStaminaComponent.Cast(
			owner.FindComponent(SCR_CharacterStaminaComponent));

		// 检测 ACE
		m_bACE_Detected = SCR_RSS_ACE_Bridge.IsACELoaded(owner);

		// 对所有角色生效（玩家 + AI）
		if (m_bACE_Detected)
		{
			if (GetGame().GetCallqueue())
				GetGame().GetCallqueue().CallLater(ACE_StartMonitoring, 1000, false);
		}
	}

	//==============================================================================================
	// 析构函数
	//==============================================================================================
	void ~SCR_CharacterControllerComponent()
	{
		m_bACE_IsDeleted = true;
		m_bACE_IsMonitoring = false;
		m_pACE_StaminaComponent = null;
	}

	//==============================================================================================
	// 启动监控循环
	//==============================================================================================
	protected void ACE_StartMonitoring()
	{
		if (m_bACE_IsDeleted || m_bACE_IsMonitoring)
			return;
		if (!GetGame() || !GetGame().InPlayMode())
			return;

		m_bACE_IsMonitoring = true;
		ACE_MonitorTick();
	}

	//==============================================================================================
	// 监控循环主 tick（200ms）
	//==============================================================================================
	protected void ACE_MonitorTick()
	{
		// ── 安全检查 ──
		if (m_bACE_IsDeleted || !m_bACE_IsMonitoring)
			return;
		if (!GetGame() || !GetGame().InPlayMode())
		{
			m_bACE_IsMonitoring = false;
			return;
		}

		IEntity owner = GetOwner();
		if (!owner)
		{
			m_bACE_IsMonitoring = false;
			return;
		}

		// 确保 stamina 组件有效
		if (!m_pACE_StaminaComponent)
		{
			m_pACE_StaminaComponent = SCR_CharacterStaminaComponent.Cast(
				owner.FindComponent(SCR_CharacterStaminaComponent));
			if (!m_pACE_StaminaComponent)
			{
				ACE_ScheduleNextTick();
				return;
			}
		}

		// 步骤 1：读取 ACE 全部状态（含药物浓度）
		ACE_ReadACEState(owner);

		// 步骤 2：统一体力修正
		ACE_ApplyUnifiedStaminaModifier();

		// 调度下一 tick
		ACE_ScheduleNextTick();
	}

	//==============================================================================================
	// 调度下一 tick
	//==============================================================================================
	protected void ACE_ScheduleNextTick()
	{
		if (m_bACE_IsDeleted || !m_bACE_IsMonitoring)
			return;
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(ACE_MonitorTick,
				SCR_RSS_ACE_Constants.MONITOR_INTERVAL_MS, false);
	}

	//==============================================================================================
	// 读取 ACE 全部医疗状态（含 PK/PD 药物浓度）
	//==============================================================================================
	protected void ACE_ReadACEState(IEntity owner)
	{
		if (!m_bACE_Detected)
		{
			m_fACE_BleedingAmount = 0.0;
			m_fACE_PainIntensity = 0.0;
			m_fACE_HealthScaled = 1.0;
			m_fACE_EpiLevel = 0.0;
			m_fACE_MorphineLevel = 0.0;
			m_fACE_PainSuppression = 0.0;
			m_fACE_MetoprololLevel = 0.0;
			m_fACE_PhenylephrineLevel = 0.0;
			m_fACE_NaloxoneLevel = 0.0;
			return;
		}

		// 连续医疗状态
		m_fACE_BleedingAmount = SCR_RSS_ACE_Bridge.GetBleedingAmount(owner);
		m_fACE_PainIntensity = SCR_RSS_ACE_Bridge.GetPainIntensity(owner);
		m_fACE_HealthScaled = SCR_RSS_ACE_Bridge.GetHealthScaled(owner);
		m_fACE_PainSuppression = SCR_RSS_ACE_Bridge.GetPainSuppression(owner);

		// PK/PD 药物浓度（归一化 0~1）
		m_fACE_EpiPrevLevel = m_fACE_EpiLevel;
		m_fACE_EpiLevel = SCR_RSS_ACE_Bridge.GetEpinephrineLevel(owner);
		m_fACE_MorphineLevel = SCR_RSS_ACE_Bridge.GetMorphineLevel(owner);
		m_fACE_MetoprololLevel = SCR_RSS_ACE_Bridge.GetMetoprololLevel(owner);
		m_fACE_PhenylephrineLevel = SCR_RSS_ACE_Bridge.GetPhenylephrineLevel(owner);
		m_fACE_NaloxoneLevel = SCR_RSS_ACE_Bridge.GetNaloxoneLevel(owner);
	}

	//==============================================================================================
	// 统一体力修正（能量消耗率版本）
	//
	// v2 模型：
	//   - 出血/疼痛/健康值 → aceMetabolicMult（乘数型，与运动强度挂钩）
	//   - 肾上腺素 → 直接 stamina 回补（浓度正比） + 浓度下降惩罚
	//   - β阻滞剂/血管收缩剂 → aceMetabolicMult 加层
	//   - 纳洛酮 → 拮抗吗啡的疼痛抑制，使 pain 回归
	//==============================================================================================
	protected void ACE_ApplyUnifiedStaminaModifier()
	{
		if (!m_pACE_StaminaComponent)
			return;

		float currentStamina = m_pACE_StaminaComponent.GetTargetStamina();
		if (currentStamina < 0.0)
			return;

		// ── 移动状态检测 ──
		int movePhase = GetCurrentMovementPhase();
		bool isMoving = (movePhase > 0);

		// ── 推算 RSS 基准消耗率 ──
		float staminaDelta = 0.0;
		if (m_fACE_PreviousStamina >= 0.0)
			staminaDelta = Math.Max(m_fACE_PreviousStamina - currentStamina, 0.0);
		m_fACE_PreviousStamina = currentStamina;

		float netDelta = 0.0;

		// ── 仅在 RSS 确实在消耗 stamina 时叠加 ACE 成本 ──
		if (staminaDelta > 0.0001)
		{
			float aceMetabolicMult = 0.0;

			// ── 3.1 出血 → 代谢成本增加 ──
			if (m_fACE_BleedingAmount > 0.0)
			{
				aceMetabolicMult += m_fACE_BleedingAmount * SCR_RSS_ACE_Constants.BLEED_METABOLIC_MULT;
			}

			// ── 3.2 疼痛 → 运动效率降低（仅移动时） ──
			// ACE_Medical_GetPainIntensity 已包含吗啡抑制，无需额外判断
			if (m_fACE_PainIntensity > 0.0 && isMoving)
			{
				aceMetabolicMult += m_fACE_PainIntensity * SCR_RSS_ACE_Constants.PAIN_METABOLIC_MULT;
			}

			// ── 3.3 健康值下降 → 整体运动能力降低 ──
			if (m_fACE_HealthScaled < 1.0)
			{
				float injuryLevel = 1.0 - m_fACE_HealthScaled;
				aceMetabolicMult += injuryLevel * SCR_RSS_ACE_Constants.HEALTH_METABOLIC_MULT;
			}

			// ── 3.4 美托洛尔（β阻滞剂）→ 运动能力受限 ──
			if (m_fACE_MetoprololLevel > SCR_RSS_ACE_Constants.METOPROLOL_MIN_EFFECTIVE_LEVEL && isMoving)
			{
				aceMetabolicMult += m_fACE_MetoprololLevel
					* SCR_RSS_ACE_Constants.METOPROLOL_METABOLIC_MULT;
			}

			// ── 3.5 去氧肾上腺素（血管收缩）→ 肌肉血流减少 ──
			if (m_fACE_PhenylephrineLevel > SCR_RSS_ACE_Constants.PHENYLEPHRINE_MIN_EFFECTIVE_LEVEL)
			{
				aceMetabolicMult += m_fACE_PhenylephrineLevel
					* SCR_RSS_ACE_Constants.PHENYLEPHRINE_METABOLIC_MULT;
			}

			// ── 内源性肾上腺素代偿（仅对伤势类乘数生效，不对药物生效） ──
			float injuryMetabolicMult = 0.0;
			if (m_fACE_BleedingAmount > 0.0)
				injuryMetabolicMult += m_fACE_BleedingAmount * SCR_RSS_ACE_Constants.BLEED_METABOLIC_MULT;
			if (m_fACE_PainIntensity > 0.0 && isMoving)
				injuryMetabolicMult += m_fACE_PainIntensity * SCR_RSS_ACE_Constants.PAIN_METABOLIC_MULT;
			if (m_fACE_HealthScaled < 1.0)
				injuryMetabolicMult += (1.0 - m_fACE_HealthScaled) * SCR_RSS_ACE_Constants.HEALTH_METABOLIC_MULT;

			if (injuryMetabolicMult > 0.0)
			{
				float injurySeverity = Math.Max(
					Math.Max(m_fACE_BleedingAmount, m_fACE_PainIntensity * 0.6),
					1.0 - m_fACE_HealthScaled);
				float compensationRatio = Math.Lerp(
					SCR_RSS_ACE_Constants.COMPENSATION_LIGHT_INJURY,
					SCR_RSS_ACE_Constants.COMPENSATION_SEVERE_INJURY,
					Math.Clamp(injurySeverity / SCR_RSS_ACE_Constants.COMPENSATION_SEVERE_THRESHOLD, 0.0, 1.0));
				aceMetabolicMult -= injuryMetabolicMult * compensationRatio;
			}

			// ── 低体力保护 ──
			if (currentStamina < SCR_RSS_ACE_Constants.FLOOR_START)
			{
				float floorFactor = Math.Clamp(
					(currentStamina - SCR_RSS_ACE_Constants.FLOOR_END)
					/ (SCR_RSS_ACE_Constants.FLOOR_START - SCR_RSS_ACE_Constants.FLOOR_END),
					0.0, 1.0);
				aceMetabolicMult *= floorFactor;
			}

			// ── 应用额外消耗 ──
			if (aceMetabolicMult > 0.001)
			{
				float extraDrain = staminaDelta * aceMetabolicMult;
				netDelta -= extraDrain;
			}
		}

		// ── 3.6 治疗 → 恢复加速 ──
		if (m_bACE_Detected)
		{
			float healingBonus = 0.0;

			if (m_fACE_BleedingAmount < m_fACE_PrevBleedingAmount - 0.01)
			{
				float bleedDrop = m_fACE_PrevBleedingAmount - m_fACE_BleedingAmount;
				healingBonus += bleedDrop * 0.05;
			}

			if (m_fACE_HealthScaled > m_fACE_PrevHealthScaled + 0.01)
			{
				float healthRise = m_fACE_HealthScaled - m_fACE_PrevHealthScaled;
				healingBonus += healthRise * 0.05;
			}

			m_fACE_PrevBleedingAmount = m_fACE_BleedingAmount;
			m_fACE_PrevPainIntensity = m_fACE_PainIntensity;
			m_fACE_PrevHealthScaled = m_fACE_HealthScaled;

			if (healingBonus > 0.001)
				netDelta += healingBonus;
		}

		// ── 3.7 外源性肾上腺素（浓度驱动） ──
		if (m_bACE_Detected)
			ACE_ApplyEpinephrineEffect(netDelta);

		// ── 3.8 纳洛酮（拮抗吗啡的疼痛抑制） ──
		// 纳洛酮本身不直接消耗 stamina，但当它抵消吗啡的疼痛抑制后，
		// pain intensity 已经由 ACE 更新过（GetPainIntensity 已包含抑制），
		// 所以 pain drain 已经在 3.2 中自然体现。
		// 但纳洛酮本身作为应激源，给予微量的额外消耗。
		if (m_fACE_NaloxoneLevel > 0.05 && staminaDelta > 0.0001)
		{
			netDelta -= staminaDelta * (m_fACE_NaloxoneLevel * 0.05);
		}

		// 没有净变化时跳过
		if (Math.AbsFloat(netDelta) < 0.0001)
			return;

		float newStamina = Math.Clamp(currentStamina + netDelta, 0.0, 1.0);
		m_pACE_StaminaComponent.SetTargetStamina(newStamina);
	}

	//==============================================================================================
	// 肾上腺素浓度驱动效果
	//
	// v2 — 替代旧的硬编码 60s/30s 定时器
	//   浓度 > 阈值 → stamina 回补正比于浓度
	//   浓度下降（相比上一 tick）→ 代谢反弹惩罚
	//   无浓度 → 无效果
	//==============================================================================================
	protected void ACE_ApplyEpinephrineEffect(inout float netDelta)
	{
		// ── BUFF：浓度正比 stamina 回补 ──
		if (m_fACE_EpiLevel > SCR_RSS_ACE_Constants.EPI_MIN_EFFECTIVE_LEVEL)
		{
			netDelta += m_fACE_EpiLevel * SCR_RSS_ACE_Constants.EPI_BUFF_RECOVERY_MAX;
		}

		// ── 浓度下降检测 & 惩罚 ──
		// 如果浓度正在下降（上一 tick 浓度 > 当前浓度 × 阈值），触发代谢反弹
		if (m_fACE_EpiPrevLevel > SCR_RSS_ACE_Constants.EPI_MIN_EFFECTIVE_LEVEL &&
			m_fACE_EpiLevel < m_fACE_EpiPrevLevel * SCR_RSS_ACE_Constants.EPI_DECLINE_THRESHOLD)
		{
			m_bACE_EpiInDecline = true;
		}

		if (m_bACE_EpiInDecline)
		{
			float levelDrop = Math.Max(m_fACE_EpiPrevLevel - m_fACE_EpiLevel, 0.0);
			float dropPenalty = (levelDrop / 0.1) * SCR_RSS_ACE_Constants.EPI_PENALTY_PER_DROP;
			netDelta -= dropPenalty;

			// 浓度已降到近乎零，清除下降状态
			if (m_fACE_EpiLevel < 0.01)
			{
				m_bACE_EpiInDecline = false;
			}
		}
	}
}
