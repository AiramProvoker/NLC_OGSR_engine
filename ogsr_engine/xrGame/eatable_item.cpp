////////////////////////////////////////////////////////////////////////////
//	Module 		: eatable_item.cpp
//	Created 	: 24.03.2003
//  Modified 	: 29.01.2004
//	Author		: Yuri Dobronravin
//	Description : Eatable item
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "eatable_item.h"
#include "physic_item.h"
#include "Level.h"
#include "entity_alive.h"
#include "EntityCondition.h"
#include "InventoryOwner.h"
#include "xrServer_Objects_ALife_Items.h"

CEatableItem::CEatableItem()
{
    m_fHealthInfluence = 0;
    m_fPowerInfluence = 0;
    m_fSatietyInfluence = 0;
    m_fRadiationInfluence = 0;
    m_fPsyHealthInfluence = 0;

    m_iPortionsNum = -1;

    m_physic_item = 0;
}

CEatableItem::~CEatableItem() {}

DLL_Pure* CEatableItem::_construct()
{
    m_physic_item = smart_cast<CPhysicItem*>(this);
    return (inherited::_construct());
}

void CEatableItem::Load(LPCSTR section)
{
    inherited::Load(section);

    m_fHealthInfluence = pSettings->r_float(section, "eat_health");
    m_fPowerInfluence = pSettings->r_float(section, "eat_power");
    m_fSatietyInfluence = pSettings->r_float(section, "eat_satiety");
    m_fRadiationInfluence = pSettings->r_float(section, "eat_radiation");
    m_fWoundsHealPerc = pSettings->r_float(section, "wounds_heal_perc");
    clamp(m_fWoundsHealPerc, 0.f, 1.f);
    m_fPsyHealthInfluence = READ_IF_EXISTS(pSettings, r_float, section, "eat_psy_health", 0.0f);
    m_fThirstInfluence = READ_IF_EXISTS(pSettings, r_float, section, "eat_thirst", 0.0f);

    m_iStartPortionsNum = pSettings->r_s32(section, "eat_portions_num");
    m_fMaxPowerUpInfluence = READ_IF_EXISTS(pSettings, r_float, section, "eat_max_power", 0.0f);
    use_for_every_item = READ_IF_EXISTS(pSettings, r_bool, section, "use_for_every_item", false);
    eat_portions_influence = READ_IF_EXISTS(pSettings, r_bool, section, "eat_portions_influence", false);
    VERIFY(m_iPortionsNum < 10000);
}

BOOL CEatableItem::net_Spawn(CSE_Abstract* DC)
{
    if (!inherited::net_Spawn(DC))
        return FALSE;

    if (auto eatable = smart_cast<CSE_ALifeItemEatable*>(DC))
    {
        m_iPortionsNum = eatable->m_portions_num;
        if (eat_portions_influence)
        {
            const float k = float(m_iPortionsNum) / float(m_iStartPortionsNum);
            m_weight *= k;
            m_cost = static_cast<int>(std::roundf(float(static_cast<int>(m_cost)) * k));
        }
    }
    else
        m_iPortionsNum = m_iStartPortionsNum;

    return TRUE;
};

void CEatableItem::net_Export(CSE_Abstract* E)
{
    inherited::net_Export(E);

    if (auto eatable = smart_cast<CSE_ALifeItemEatable*>(E))
        eatable->m_portions_num = m_iPortionsNum;
}

bool CEatableItem::Useful() const
{
    if (!inherited::Useful())
        return false;

    //проверить не все ли еще съедено
    if (Empty())
        return false;

    return true;
}

void CEatableItem::OnH_B_Independent(bool just_before_destroy)
{
    if (!Useful())
    {
        object().setVisible(FALSE);
        object().setEnabled(FALSE);
        if (m_physic_item)
            m_physic_item->m_ready_to_destroy = true;
    }
    inherited::OnH_B_Independent(just_before_destroy);
}

void CEatableItem::UseBy(CEntityAlive* entity_alive)
{
    CInventoryOwner* IO = smart_cast<CInventoryOwner*>(entity_alive);
    R_ASSERT(IO);
    R_ASSERT(m_pCurrentInventory == IO->m_inventory);
    R_ASSERT(object().H_Parent()->ID() == entity_alive->ID());
    entity_alive->conditions().ChangeHealth(m_fHealthInfluence);
    entity_alive->conditions().ChangePower(m_fPowerInfluence);
    entity_alive->conditions().ChangeSatiety(m_fSatietyInfluence);
    entity_alive->conditions().ChangeRadiation(m_fRadiationInfluence);
    entity_alive->conditions().ChangeBleeding(m_fWoundsHealPerc);
    entity_alive->conditions().ChangePsyHealth(m_fPsyHealthInfluence);
    entity_alive->conditions().ChangeThirst(m_fThirstInfluence);

    entity_alive->conditions().SetMaxPower(entity_alive->conditions().GetMaxPower() + m_fMaxPowerUpInfluence);

    //уменьшить количество порций
    if (m_iPortionsNum > 0)
        --(m_iPortionsNum);
    else
        m_iPortionsNum = 0;

    if (eat_portions_influence)
    {
        LPCSTR sect = object().cNameSect().c_str();
        const float weight = READ_IF_EXISTS(pSettings, r_float, sect, "inv_weight", 0.f);
        const float cost = READ_IF_EXISTS(pSettings, r_float, sect, "cost", 0.f);
        const float k = float(m_iPortionsNum) / float(m_iStartPortionsNum);
        m_weight = k * weight;
        m_cost = static_cast<int>(std::roundf(k * cost));
    }
}
void CEatableItem::ZeroAllEffects()
{
    m_fHealthInfluence = 0.f;
    m_fPowerInfluence = 0.f;
    m_fSatietyInfluence = 0.f;
    m_fRadiationInfluence = 0.f;
    m_fMaxPowerUpInfluence = 0.f;
    m_fPsyHealthInfluence = 0.f;
    m_fWoundsHealPerc = 0.f;
    m_fThirstInfluence = 0.f;
}
void CEatableItem::SetRadiation(float _rad) { m_fRadiationInfluence = _rad; }