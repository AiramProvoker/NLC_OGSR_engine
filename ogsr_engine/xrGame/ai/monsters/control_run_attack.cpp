#include "stdafx.h"
#include "../../fl_hook.h"
#include "control_run_attack.h"
#include "BaseMonster/base_monster.h"
#include "monster_velocity_space.h"
#include "control_animation_base.h"
#include "control_direction_base.h"
#include "control_movement_base.h"

void CControlRunAttack::load(LPCSTR section)
{
    read_distance(section, "Run_Attack_Dist", m_min_dist, m_max_dist);
    read_delay(section, "Run_Attack_Delay", m_min_delay, m_max_delay);
}

void CControlRunAttack::reinit()
{
    CControl_ComCustom<>::reinit();

    m_time_next_attack = 0;
}

void CControlRunAttack::activate()
{
    m_man->capture_pure(this);
    m_man->subscribe(this, ControlCom::eventAnimationEnd);
    m_man->subscribe(this, ControlCom::eventAnimationStart);

    m_man->path_stop(this);
    m_man->move_stop(this);

    //////////////////////////////////////////////////////////////////////////

    SControlDirectionData* ctrl_dir = (SControlDirectionData*)m_man->data(this, ControlCom::eControlDir);
    VERIFY(ctrl_dir);
    ctrl_dir->heading.target_speed = 3.f;
    ctrl_dir->heading.target_angle = m_man->direction().angle_to_target(m_object->EnemyMan.get_enemy()->Position());

    //////////////////////////////////////////////////////////////////////////

    SControlAnimationData* ctrl_anim = (SControlAnimationData*)m_man->data(this, ControlCom::eControlAnimation);
    VERIFY(ctrl_anim);

    ctrl_anim->global.set_motion(smart_cast<IKinematicsAnimated*>(m_object->Visual())->ID_Cycle_Safe("stand_attack_run_0"));
    ctrl_anim->global.actual = false;
}

void CControlRunAttack::on_release()
{
    m_man->unlock(this, ControlCom::eControlPath);
    m_man->release_pure(this);
    m_man->unsubscribe(this, ControlCom::eventAnimationEnd);
    m_man->unsubscribe(this, ControlCom::eventAnimationStart);
}

bool CControlRunAttack::check_start_conditions()
{
    // Stock is too strict for OGSR boars: the path controller owns pure capture during the chase,
    // the face cone is ~30 deg and the speed must match run speed within 2. The ram has to start
    // while charging toward the enemy without taking over standing melee (paws).
    if (is_active())
    {
        fl_hook::boar_result(fl_hook::eBoarActive);
        return false;
    }

    const CEntityAlive* enemy = m_object->EnemyMan.get_enemy();
    if (!enemy)
    {
        fl_hook::boar_result(fl_hook::eBoarNoEnemy);
        return false;
    }

    const u32 now = Device.dwTimeGlobal;
    if (m_time_next_attack > now)
    {
        fl_hook::boar_result(fl_hook::eBoarCooldown);
        return false;
    }

    Fvector to_enemy;
    to_enemy.sub(enemy->Position(), m_object->Position());
    const float dist = to_enemy.magnitude();
    fl_hook::boar_last_dist = dist;

    // Run_Attack_Dist as configured, a floor only for a broken config and a hard cap
    float dmin = m_min_dist;
    float dmax = m_max_dist;
    if (dmin < 2.5f)
        dmin = 2.5f;
    if (dmax < dmin + 0.5f)
        dmax = dmin + 3.0f;
    if (dmax > 10.0f)
        dmax = 10.0f;
    if (dist < dmin || dist > dmax)
    {
        fl_hook::boar_result(fl_hook::eBoarDistance);
        return false;
    }

    // ~55 deg face cone on XZ from the object's forward vector
    const Fvector& fwd = m_object->XFORM().k;
    const float fl2 = fwd.x * fwd.x + fwd.z * fwd.z;
    const float tl2 = to_enemy.x * to_enemy.x + to_enemy.z * to_enemy.z;
    const float dot = fwd.x * to_enemy.x + fwd.z * to_enemy.z;
    if (fl2 < 1e-8f || tl2 < 1e-8f || dot <= 0.f || dot * dot < 0.329f * fl2 * tl2)
    {
        fl_hook::boar_result(fl_hook::eBoarNotFacing);
        return false;
    }

    // real movement required, so standing combat stays on eAnimAttack (paws)
    const float vcur = m_man->movement().velocity_current();
    fl_hook::boar_last_vel = vcur;
    if (vcur < 2.0f)
    {
        fl_hook::boar_result(fl_hook::eBoarStanding);
        return false;
    }

    // activate() snaps the heading to the enemy: a charge that ends early must not re-lock at once
    m_time_next_attack = now + 2200;

    fl_hook::boar_result(fl_hook::eBoarOk);
    return true;
}

void CControlRunAttack::on_event(ControlCom::EEventType type, ControlCom::IEventData* dat)
{
    switch (type)
    {
    case ControlCom::eventAnimationEnd:
        m_time_next_attack = time() + Random.randI(m_min_delay, m_max_delay);
        m_man->notify(ControlCom::eventRunAttackEnd, 0);
        break;
    case ControlCom::eventAnimationStart: // handle blend params
    {
        // set animation speed
        VERIFY((SControlAnimationData*)m_man->data(this, ControlCom::eControlAnimation));

        CBlend* blend = m_man->animation().current_blend();
        VERIFY(blend);

        // animation time
        float anim_time = blend->timeTotal / blend->speed;

        // run velocity
        u32 velocity_mask = MonsterMovement::eVelocityParameterRunNormal;
        SVelocityParam& velocity = m_object->move().get_velocity(velocity_mask);

        // distance
        float path_dist = anim_time * velocity.velocity.linear;

        Fvector dir;
        dir.sub(m_object->EnemyMan.get_enemy()->Position(), m_object->Position());
        dir.normalize_safe();

        Fvector target_position;
        target_position.mad(m_object->Position(), dir, path_dist);

        if (!m_man->build_path_line(this, target_position, u32(-1), velocity_mask | MonsterMovement::eVelocityParameterStand))
        {
            m_man->notify(ControlCom::eventRunAttackEnd, 0);
        }
        else
        {
            // enable path
            SControlPathBuilderData* ctrl_path = (SControlPathBuilderData*)m_man->data(this, ControlCom::eControlPath);
            VERIFY(ctrl_path);
            ctrl_path->enable = true;

            m_man->lock(this, ControlCom::eControlPath);

            SControlMovementData* ctrl_move = (SControlMovementData*)m_man->data(this, ControlCom::eControlMovement);
            VERIFY(ctrl_move);
            ctrl_move->velocity_target = velocity.velocity.linear;
            ctrl_move->acc = flt_max;
        }
    }
    break;
    }
}
