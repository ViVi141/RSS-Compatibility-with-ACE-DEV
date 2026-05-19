//------------------------------------------------------------------------------------------------
//! RSS ↔ ACE 兼容层 — ACE 状态读取桥接器
//!
//! 所有 ACE 类型引用均通过 Cast() 调用并判空：
//! - ACE 未加载时所有方法返回默认值 0 / 1.0 / false
//! - 无需修改 RSS 或 ACE 源码
//!
//! 药物浓度读取：
//!   通过 ACE-DEV 的 PK/PD 系统读取实时血药浓度（nM），
//!   以各药物的典型有效浓度归一化到 0~1 返回。
//!   ACE 未加载 / 未用药时返回 0。
//------------------------------------------------------------------------------------------------
class SCR_RSS_ACE_Bridge
{
	//------------------------------------------------------------------------------------------------
	//! 从 IEntity 获取 CharacterDamageManagerComponent 的安全帮助方法
	static SCR_CharacterDamageManagerComponent GetDamageManager(IEntity owner)
	{
		if (!owner)
			return null;

		ChimeraCharacter character = ChimeraCharacter.Cast(owner);
		if (!character)
			return null;

		return SCR_CharacterDamageManagerComponent.Cast(character.GetDamageManager());
	}

	//------------------------------------------------------------------------------------------------
	//! 快速检测 ACE 是否已加载（通过疼痛系统是否存在判断）
	static bool IsACELoaded(IEntity owner)
	{
		SCR_CharacterDamageManagerComponent dmgMgr = GetDamageManager(owner);
		if (!dmgMgr)
			return false;

		return ACE_Medical_HasPainSystem(dmgMgr);
	}

	//------------------------------------------------------------------------------------------------
	//! 检测 ACE 疼痛系统是否存在
	static bool ACE_Medical_HasPainSystem(SCR_CharacterDamageManagerComponent dmgMgr)
	{
		if (!dmgMgr)
			return false;

		ACE_Medical_PainHitZone painHz = ACE_Medical_PainHitZone.Cast(dmgMgr.ACE_Medical_GetPainHitZone());
		return painHz != null;
	}

	//------------------------------------------------------------------------------------------------
	//! 3.1 读取 ACE 出血量
	//! @return 0.0（无出血）~ 1.0（致命失血）
	static float GetBleedingAmount(IEntity owner)
	{
		SCR_CharacterDamageManagerComponent dmgMgr = GetDamageManager(owner);
		if (!dmgMgr)
			return 0.0;

		SCR_CharacterBloodHitZone bloodHz = dmgMgr.GetBloodHitZone();
		if (!bloodHz)
			return 0.0;

		return bloodHz.GetTotalBleedingAmount();
	}

	//------------------------------------------------------------------------------------------------
	//! 3.2 读取 ACE 疼痛强度（已包含吗啡抑制）
	//! @return 0.0（无痛）~ 1.0（剧痛）
	static float GetPainIntensity(IEntity owner)
	{
		SCR_CharacterDamageManagerComponent dmgMgr = GetDamageManager(owner);
		if (!dmgMgr)
			return 0.0;

		return dmgMgr.ACE_Medical_GetPainIntensity();
	}

	//------------------------------------------------------------------------------------------------
	//! 读取 ACE 综合健康值
	//! @return 0.0（濒死）~ 1.0（满血）
	static float GetHealthScaled(IEntity owner)
	{
		SCR_CharacterDamageManagerComponent dmgMgr = GetDamageManager(owner);
		if (!dmgMgr)
			return 1.0;

		return dmgMgr.ACE_Medical_GetHealthScaled();
	}

	//------------------------------------------------------------------------------------------------
	//! 获取 ACE 疼痛抑制值（吗啡设定）
	//! @return 0.0（无抑制）~ 1.0（完全抑制）
	static float GetPainSuppression(IEntity owner)
	{
		SCR_CharacterDamageManagerComponent dmgMgr = GetDamageManager(owner);
		if (!dmgMgr)
			return 0.0;

		return dmgMgr.ACE_Medical_GetPainSuppression();
	}

	//==============================================================================================
	// PK/PD 药物浓度读取
	//==============================================================================================

	//------------------------------------------------------------------------------------------------
	//! 通用药物归一化浓度读取
	//! @param owner 角色实体
	//! @param drugType ACE 药物类型枚举
	//! @param typicalConcNM 该药物的典型有效浓度 [nM]，用于归一化
	//! @return 0.0（无药物）~ 1.0（达到或超过典型浓度）
	static float GetDrugLevel(IEntity owner, ACE_Medical_EDrugType drugType, float typicalConcNM)
	{
		if (!owner)
			return 0.0;

		ACE_Medical_MedicationComponent medComp = ACE_Medical_MedicationComponent.Cast(
			owner.FindComponent(ACE_Medical_MedicationComponent));
		if (!medComp)
			return 0.0;

		ACE_Medical_Medication_Settings settings =
			ACE_SettingsHelperT<ACE_Medical_Medication_Settings>.GetModSettings();
		if (!settings)
			return 0.0;

		ACE_Medical_PharmacokineticsConfig pkConfig = settings.GetPharmacokineticsConfig(drugType);
		if (!pkConfig)
			return 0.0;

		array<ACE_Medical_EDrugType> drugs = {};
		array<ref array<ref ACE_Medical_Dose>> allDoses = {};
		medComp.GetMedications(drugs, allDoses);

		int idx = drugs.Find(drugType);
		if (idx < 0)
			return 0.0;

		array<ref ACE_Medical_Dose> doses = allDoses[idx];
		float totalConcNM = 0.0;

		for (int i = doses.Count() - 1; i >= 0; i--)
		{
			ACE_Medical_Dose dose = doses[i];
			if (dose.IsExpired())
				continue;
			totalConcNM += dose.ComputeConcentration(pkConfig);
		}

		// 归一化到 0~1
		if (typicalConcNM <= 0.0)
			return 0.0;

		return Math.Clamp(totalConcNM / typicalConcNM, 0.0, 1.0);
	}

	//------------------------------------------------------------------------------------------------
	//! 肾上腺素归一化浓度
	//! @return 0.0 ~ 1.0
	static float GetEpinephrineLevel(IEntity owner)
	{
		return GetDrugLevel(owner, ACE_Medical_EDrugType.EPINEPHRINE,
			SCR_RSS_ACE_Constants.EPI_TYPICAL_CONC_NM);
	}

	//------------------------------------------------------------------------------------------------
	//! 吗啡归一化浓度
	//! @return 0.0 ~ 1.0
	static float GetMorphineLevel(IEntity owner)
	{
		return GetDrugLevel(owner, ACE_Medical_EDrugType.MORPHINE,
			SCR_RSS_ACE_Constants.MORPHINE_TYPICAL_CONC_NM);
	}

	//------------------------------------------------------------------------------------------------
	//! 美托洛尔归一化浓度
	//! @return 0.0 ~ 1.0
	static float GetMetoprololLevel(IEntity owner)
	{
		return GetDrugLevel(owner, ACE_Medical_EDrugType.METOPROLOL,
			SCR_RSS_ACE_Constants.METOPROLOL_TYPICAL_CONC_NM);
	}

	//------------------------------------------------------------------------------------------------
	//! 去氧肾上腺素归一化浓度
	//! @return 0.0 ~ 1.0
	static float GetPhenylephrineLevel(IEntity owner)
	{
		return GetDrugLevel(owner, ACE_Medical_EDrugType.PHENYLEPHRINE,
			SCR_RSS_ACE_Constants.PHENYLEPHRINE_TYPICAL_CONC_NM);
	}

	//------------------------------------------------------------------------------------------------
	//! 纳洛酮归一化浓度
	//! @return 0.0 ~ 1.0
	static float GetNaloxoneLevel(IEntity owner)
	{
		return GetDrugLevel(owner, ACE_Medical_EDrugType.NALOXONE,
			SCR_RSS_ACE_Constants.NALOXONE_TYPICAL_CONC_NM);
	}
}
