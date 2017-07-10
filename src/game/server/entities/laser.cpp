/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include "laser.h"

#include <stdio.h>

CLaser::CLaser(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, float StartEnergy, int Owner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Pos = Pos;
	m_Owner = Owner;
	m_Energy = StartEnergy;
	m_Dir = Direction;
	m_Bounces = 0;
	m_EvalTick = 0;
	GameWorld()->InsertEntity(this);
	DoBounce();
}


bool CLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *Hit = 0;
	CCharacter *SkipChar = OwnerChar;
	vec2 Pos = m_Pos;
	while (length(From-Pos) + length(Pos-To) < length(From-To) + 1e-5)
	{
		SkipChar = GameServer()->m_World.IntersectCharacter(Pos, To, 0.f, At, SkipChar);
		if (!SkipChar)
			break;
		Pos = At + normalize(To-From)*(SkipChar->m_ProximityRadius+1e-5);

		if(g_Config.m_SvLaserSkipFrozen && SkipChar->GetFreezeTicks() > 0)
			continue;

		if (SkipChar == OwnerChar) //can actually happen on bounce
			continue;

		if (OwnerChar && (GameServer()->m_pController->IsTeamplay() && g_Config.m_SvLaserSkipTeammates && OwnerChar->GetPlayer()->GetTeam() == SkipChar->GetPlayer()->GetTeam()))
			continue;

		Hit = SkipChar;
		break;
	}

	if (!Hit) return false;

	m_From = From;
	m_Pos = At;
	m_Energy = -1;
	//Hit->TakeDamage(vec2(0.f, 0.f), GameServer()->Tuning()->m_LaserDamage, m_Owner, WEAPON_RIFLE);

	if ((OwnerChar && (!GameServer()->m_pController->IsTeamplay() || 
	    OwnerChar->GetPlayer()->GetTeam() != Hit->GetPlayer()->GetTeam())) && 
	    (Hit->GetFreezeTicks() <= 0 && 
	    Hit->GetMeltTick() + g_Config.m_SvMeltSafeticks < Server()->Tick())) {
		Hit->Freeze(GameServer()->Tuning()->m_LaserDamage * Server()->TickSpeed(), 
			m_Owner);

		int oid = OwnerChar->GetPlayer()->GetCID();
		struct tee_stats *o = GameServer()->find_round_entry(Server()->
			ClientName(oid));
		struct tee_stats *v = GameServer()->find_round_entry(Server()->
			ClientName(Hit->GetPlayer()->GetCID()));
			
		float da, d = OwnerChar->m_last_travel_dist;
		float dl, l = OwnerChar->m_last_tarposlen;
		
		da = OwnerChar->GetPlayer()->m_avgdist = ADD_AVG(d, 
			OwnerChar->GetPlayer()->m_avgdist, 
			OwnerChar->GetPlayer()->m_numdsamp);
		dl = OwnerChar->GetPlayer()->m_avglen = ADD_AVG(l, 
			OwnerChar->GetPlayer()->m_avglen, 
			OwnerChar->GetPlayer()->m_numlsamp);

		printf("%s froze! dist = %f (avg %f) len = %f (avg %f) num %d\n", 
			Server()->ClientName(oid), d, l, da, dl,
			OwnerChar->GetPlayer()->m_numlsamp);
		
		if (o && v) {
			if (m_Bounces)
		    		o->bounce_shots++;
		    	v->frozen++;
			v->frozeby = oid;
			o->freezes++;
		}
	}


	return true;
}

void CLaser::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < 0)
	{
		GameServer()->m_World.DestroyEntity(this);
		return;
	}

	vec2 To = m_Pos + m_Dir * m_Energy;

	if(GameServer()->Collision()->IntersectLine(m_Pos, To, 0x0, &To))
	{
		if(!HitCharacter(m_Pos, To))
		{
			// intersected
			m_From = m_Pos;
			m_Pos = To;

			vec2 TempPos = m_Pos;
			vec2 TempDir = m_Dir * 4.0f;

			GameServer()->Collision()->MovePoint(&TempPos, &TempDir, 1.0f, 0);
			m_Pos = TempPos;
			m_Dir = normalize(TempDir);

			m_Energy -= distance(m_From, m_Pos) + GameServer()->Tuning()->m_LaserBounceCost;
			m_Bounces++;

			if(m_Bounces > GameServer()->Tuning()->m_LaserBounceNum)
				m_Energy = -1;

			GameServer()->CreateSound(m_Pos, SOUND_RIFLE_BOUNCE);
		}
	}
	else
	{
		if(!HitCharacter(m_Pos, To))
		{
			m_From = m_Pos;
			m_Pos = To;
			m_Energy = -1;
		}
	}
}

void CLaser::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CLaser::Tick()
{
	if(Server()->Tick() > m_EvalTick+(Server()->TickSpeed()*GameServer()->Tuning()->m_LaserBounceDelay)/1000.0f)
		DoBounce();
}

void CLaser::TickPaused()
{
	++m_EvalTick;
}

void CLaser::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_FromX = (int)m_From.x;
	pObj->m_FromY = (int)m_From.y;
	pObj->m_StartTick = m_EvalTick;
}
