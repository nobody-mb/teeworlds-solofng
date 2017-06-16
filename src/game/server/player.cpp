/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <string.h>
#include <new>
#include <engine/shared/config.h>
#include "player.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

MACRO_ALLOC_POOL_ID_IMPL(CPlayer, MAX_CLIENTS)

IServer *CPlayer::Server() const { return m_pGameServer->Server(); }

struct tee_stats CPlayer::read_statsfile (const char *name, time_t create)
{
	char path[128];
	int src_fd;
	struct tee_stats ret;
	
	memset(&ret, 0, sizeof(ret));
	
	snprintf(path, sizeof(path), "%s/%s", STATS_DIR, name);
	if ((src_fd = open(path, O_RDWR, 0777)) < 0) {
		if (create) {
			fprintf(stderr, "creating file\n");
			if ((src_fd = open(path, O_WRONLY|O_CREAT, 0777)) < 0) {
				fprintf(stderr, "error creating file %s\n", path);
				return ret;
			}
			ret.join_time = create;
			write(src_fd, &ret, sizeof(ret));
		}
	} else {
		if (read(src_fd, &ret, sizeof(ret)) != sizeof(ret)) {
			fprintf(stderr, "didnt read enough data\n");
		}
	}
	close(src_fd);

	return ret;
}

CPlayer::CPlayer(CGameContext *pGameServer, int ClientID, int Team)
{
	m_pGameServer = pGameServer;
	m_RespawnTick = Server()->Tick();
	m_DieTick = Server()->Tick();
	m_ScoreStartTick = Server()->Tick();
	m_pCharacter = 0;
	m_ClientID = ClientID;
	m_Team = GameServer()->m_pController->ClampTeam(Team);
	m_SpectatorID = SPEC_FREEVIEW;
	m_LastActionTick = Server()->Tick();
	m_ChatScore = 0;
	m_TeamChangeTick = Server()->Tick();
	gstats.join_time = time(NULL);
	
	totals = read_statsfile(Server()->ClientName(m_ClientID), gstats.join_time);
}

CPlayer::~CPlayer()
{
	if (gstats.spree_max > totals.spree_max)
		totals.spree_max = gstats.spree_max;
	
	for (int i = 0; i < 6; i++)
		totals.multis[i] += gstats.multis[i];
		
	totals.kills += gstats.kills;
	totals.kills_x2 += gstats.kills_x2;
	totals.kills_wrong += gstats.kills_wrong;
	totals.deaths += gstats.deaths;
	totals.steals += gstats.steals;
	totals.suicides += gstats.suicides;
	totals.shots += gstats.shots;
	totals.freezes += gstats.freezes;
	totals.frozen += gstats.frozen;
	totals.hammers += gstats.hammers;
	totals.hammered += gstats.hammered;
	totals.teamhooks += gstats.teamhooks;
	totals.bounce_shots += gstats.bounce_shots;
	totals.join_time += (time(NULL) - gstats.join_time);
	
	char path[128];
	int src_fd;
	
	snprintf(path, sizeof(path), "%s/%s", STATS_DIR, 
		Server()->ClientName(GetCID()));
	if ((src_fd = open(path, O_RDWR, 0777)) < 0) {
		fprintf(stderr, "creating file\n");
		if ((src_fd = open(path, O_WRONLY|O_CREAT, 0777)) < 0) {
			fprintf(stderr, "error creating %s\n", path);
			perror("a");
			return;
		}
	}
	
	write(src_fd, &totals, sizeof(totals));
	close(src_fd);
	
	delete m_pCharacter;
	m_pCharacter = 0;
}

void CPlayer::Tick()
{
#ifdef CONF_DEBUG
	if(!g_Config.m_DbgDummies || m_ClientID < MAX_CLIENTS-g_Config.m_DbgDummies)
#endif
	if(!Server()->ClientIngame(m_ClientID))
		return;

	if (m_ChatScore > 0)
		m_ChatScore--;

	Server()->SetClientScore(m_ClientID, m_Score);

	if(g_Config.m_SvAnticamper == 2 || g_Config.m_SvAnticamper == 1)
	Anticamper();

	// do latency stuff
	{
		IServer::CClientInfo Info;
		if(Server()->GetClientInfo(m_ClientID, &Info))
		{
			m_Latency.m_Accum += Info.m_Latency;
			m_Latency.m_AccumMax = max(m_Latency.m_AccumMax, Info.m_Latency);
			m_Latency.m_AccumMin = min(m_Latency.m_AccumMin, Info.m_Latency);
		}
		// each second
		if(Server()->Tick()%Server()->TickSpeed() == 0)
		{
			m_Latency.m_Avg = m_Latency.m_Accum/Server()->TickSpeed();
			m_Latency.m_Max = m_Latency.m_AccumMax;
			m_Latency.m_Min = m_Latency.m_AccumMin;
			m_Latency.m_Accum = 0;
			m_Latency.m_AccumMin = 1000;
			m_Latency.m_AccumMax = 0;
		}
	}

	if(!GameServer()->m_World.m_Paused)
	{
		if(!m_pCharacter && m_Team == TEAM_SPECTATORS && m_SpectatorID == SPEC_FREEVIEW)
			m_ViewPos -= vec2(clamp(m_ViewPos.x-m_LatestActivity.m_TargetX, -500.0f, 500.0f), clamp(m_ViewPos.y-m_LatestActivity.m_TargetY, -400.0f, 400.0f));

		if(!m_pCharacter && m_DieTick+Server()->TickSpeed()*3 <= Server()->Tick())
			m_Spawning = true;

		if(m_pCharacter)
		{
			if(m_pCharacter->IsAlive())
			{
				m_ViewPos = m_pCharacter->m_Pos;
			}
			else
			{
				delete m_pCharacter;
				m_pCharacter = 0;
			}
		}
		else if(m_Spawning && m_RespawnTick <= Server()->Tick())
			TryRespawn();
	}
	else
	{
		++m_RespawnTick;
		++m_DieTick;
		++m_ScoreStartTick;
		++m_LastActionTick;
		++m_TeamChangeTick;
 	}
}

void CPlayer::PostTick()
{
	// update latency value
	if(m_PlayerFlags&PLAYERFLAG_SCOREBOARD)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
				m_aActLatency[i] = GameServer()->m_apPlayers[i]->m_Latency.m_Min;
		}
	}

	// update view pos for spectators
	if(m_Team == TEAM_SPECTATORS && m_SpectatorID != SPEC_FREEVIEW && GameServer()->m_apPlayers[m_SpectatorID])
		m_ViewPos = GameServer()->m_apPlayers[m_SpectatorID]->m_ViewPos;
}

void CPlayer::Snap(int SnappingClient)
{
#ifdef CONF_DEBUG
	if(!g_Config.m_DbgDummies || m_ClientID < MAX_CLIENTS-g_Config.m_DbgDummies)
#endif
	if(!Server()->ClientIngame(m_ClientID))
		return;

	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, m_ClientID, sizeof(CNetObj_ClientInfo)));
	if(!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Name0, 4, Server()->ClientName(m_ClientID));
	StrToInts(&pClientInfo->m_Clan0, 3, Server()->ClientClan(m_ClientID));
	pClientInfo->m_Country = Server()->ClientCountry(m_ClientID);
	StrToInts(&pClientInfo->m_Skin0, 6, m_TeeInfos.m_SkinName);
	pClientInfo->m_UseCustomColor = m_TeeInfos.m_UseCustomColor;
	pClientInfo->m_ColorBody = m_TeeInfos.m_ColorBody;
	pClientInfo->m_ColorFeet = m_TeeInfos.m_ColorFeet;

	CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, m_ClientID, sizeof(CNetObj_PlayerInfo)));
	if(!pPlayerInfo)
		return;

	pPlayerInfo->m_Latency = SnappingClient == -1 ? m_Latency.m_Min : GameServer()->m_apPlayers[SnappingClient]->m_aActLatency[m_ClientID];
	pPlayerInfo->m_Local = 0;
	pPlayerInfo->m_ClientID = m_ClientID;
	pPlayerInfo->m_Score = m_Score;
	pPlayerInfo->m_Team = m_Team;

	if(m_ClientID == SnappingClient)
		pPlayerInfo->m_Local = 1;

	if(m_ClientID == SnappingClient && m_Team == TEAM_SPECTATORS)
	{
		CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, m_ClientID, sizeof(CNetObj_SpectatorInfo)));
		if(!pSpectatorInfo)
			return;

		pSpectatorInfo->m_SpectatorID = m_SpectatorID;
		pSpectatorInfo->m_X = m_ViewPos.x;
		pSpectatorInfo->m_Y = m_ViewPos.y;
	}
}

void CPlayer::OnDisconnect(const char *pReason)
{
	KillCharacter();
	
	

	if(Server()->ClientIngame(m_ClientID))
	{
		char aBuf[512];
		if(pReason && *pReason)
			str_format(aBuf, sizeof(aBuf), "'%s' has left the game (%s)", Server()->ClientName(m_ClientID), pReason);
		else
			str_format(aBuf, sizeof(aBuf), "'%s' has left the game", Server()->ClientName(m_ClientID));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

		str_format(aBuf, sizeof(aBuf), "leave player='%d:%s'", m_ClientID, Server()->ClientName(m_ClientID));
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);
	}
}

void CPlayer::OnPredictedInput(CNetObj_PlayerInput *NewInput)
{
	// skip the input if chat is active
	if((m_PlayerFlags&PLAYERFLAG_CHATTING) && (NewInput->m_PlayerFlags&PLAYERFLAG_CHATTING))
		return;

	if(m_pCharacter)
		m_pCharacter->OnPredictedInput(NewInput);
}

void CPlayer::OnDirectInput(CNetObj_PlayerInput *NewInput)
{
	if(NewInput->m_PlayerFlags&PLAYERFLAG_CHATTING)
	{
		// skip the input if chat is active
		if(m_PlayerFlags&PLAYERFLAG_CHATTING)
			return;

		// reset input
		if(m_pCharacter)
			m_pCharacter->ResetInput();

		m_PlayerFlags = NewInput->m_PlayerFlags;
 		return;
	}

	m_PlayerFlags = NewInput->m_PlayerFlags;

	if(m_pCharacter)
		m_pCharacter->OnDirectInput(NewInput);

	if(!m_pCharacter && m_Team != TEAM_SPECTATORS && (NewInput->m_Fire&1))
		m_Spawning = true;

	// check for activity
	if(NewInput->m_Direction || m_LatestActivity.m_TargetX != NewInput->m_TargetX ||
		m_LatestActivity.m_TargetY != NewInput->m_TargetY || NewInput->m_Jump ||
		NewInput->m_Fire&1 || NewInput->m_Hook)
	{
		m_LatestActivity.m_TargetX = NewInput->m_TargetX;
		m_LatestActivity.m_TargetY = NewInput->m_TargetY;
		m_LastActionTick = Server()->Tick();
	}
}

CCharacter *CPlayer::GetCharacter()
{
	if(m_pCharacter && m_pCharacter->IsAlive())
		return m_pCharacter;
	return 0;
}

void CPlayer::KillCharacter(int Weapon)
{
	if(m_pCharacter)
	{
		m_pCharacter->Die(m_ClientID, Weapon);
		delete m_pCharacter;
		m_pCharacter = 0;
	}
}

double CPlayer::get_max_spree (struct tee_stats fstats)
{
	return (double)fstats.spree_max;
}

double CPlayer::get_kd (struct tee_stats fstats)
{
	int k = fstats.kills + fstats.kills_x2 + fstats.kills_wrong;
	int d = fstats.deaths ? fstats.deaths : 1;
	return (double)k / (double)d;
}

double CPlayer::get_accuracy (struct tee_stats fstats)
{
	if (fstats.shots < 5)
		return 0.0f;
		
	int d = fstats.shots ? fstats.shots : 1;
	return (double)fstats.freezes / (double)d;
}

void CPlayer::Respawn()
{
	if(m_Team != TEAM_SPECTATORS)
		m_Spawning = true;
}

void CPlayer::SetTeam(int Team, bool DoChatMsg)
{
	// clamp the team
	Team = GameServer()->m_pController->ClampTeam(Team);
	if(m_Team == Team)
		return;

	char aBuf[512];
	if(DoChatMsg)
	{
		char nick [] = "TsFreddie";
		if (strcmp(Server()->ClientName(m_ClientID), nick) == 0)
		{
			str_format(aBuf, sizeof(aBuf), "Happy '%s' joined the %s", Server()->ClientName(m_ClientID), GameServer()->m_pController->GetTeamName(Team));
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "'%s' joined the %s", Server()->ClientName(m_ClientID), GameServer()->m_pController->GetTeamName(Team));
		}
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
	}

	KillCharacter();

	m_Team = Team;
	m_LastActionTick = Server()->Tick();
	// we got to wait 0.5 secs before respawning
	m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' m_Team=%d", m_ClientID, Server()->ClientName(m_ClientID), m_Team);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	GameServer()->m_pController->OnPlayerInfoChange(GameServer()->m_apPlayers[m_ClientID]);

	if(Team == TEAM_SPECTATORS)
	{
		// update spectator modes
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_SpectatorID == m_ClientID)
				GameServer()->m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
		}
	}
}

void CPlayer::TryRespawn()
{
	vec2 SpawnPos;

	if(!GameServer()->m_pController->CanSpawn(m_Team, &SpawnPos))
		return;

	m_Spawning = false;
	m_pCharacter = new(m_ClientID) CCharacter(&GameServer()->m_World);
	m_pCharacter->Spawn(this, SpawnPos);
	GameServer()->CreatePlayerSpawn(SpawnPos);

	
	char nick [] = "TsFreddie";
	int Emote = EMOTE_HAPPY;
	if (strcmp(Server()->ClientName(m_ClientID), nick) == 0)
	{
		m_pCharacter->SetEmote(Emote, 100000);
	}
}

int CPlayer::Anticamper()
{
	if(GameServer()->m_World.m_Paused || !m_pCharacter || m_Team == TEAM_SPECTATORS || m_pCharacter->GetFreezeTicks())
	{
		m_CampTick = -1;
		m_SentCampMsg = false;
		return 0;
	}

	int AnticamperTime = g_Config.m_SvAnticamperTime;
	int AnticamperRange = g_Config.m_SvAnticamperRange;

	if(m_CampTick == -1)
	{
		m_CampPos = m_pCharacter->m_Pos;
		m_CampTick = Server()->Tick() + Server()->TickSpeed()*AnticamperTime;
	}

	// Check if the player is moving
	if((m_CampPos.x - m_pCharacter->m_Pos.x >= (float)AnticamperRange || m_CampPos.x - m_pCharacter->m_Pos.x <= -(float)AnticamperRange)
	|| (m_CampPos.y - m_pCharacter->m_Pos.y >= (float)AnticamperRange || m_CampPos.y - m_pCharacter->m_Pos.y <= -(float)AnticamperRange))
		{
			m_CampTick = -1;
		}

	// Send warning to the player
	if(m_CampTick <= Server()->Tick() + Server()->TickSpeed() * AnticamperTime/2 && m_CampTick != -1 && !m_SentCampMsg)
	{
		GameServer()->SendBroadcast("ANTICAMPER: Move or die", m_ClientID);
		m_SentCampMsg = true;
	}

	// Kill him
	if((m_CampTick <= Server()->Tick()) && (m_CampTick > 0))
	{
		if(g_Config.m_SvAnticamperFreeze)
		{
			m_pCharacter->Freeze(Server()->TickSpeed()*g_Config.m_SvAnticamperFreeze, -1);
			GameServer()->SendBroadcast("You have been freezed due camping", m_ClientID);
		}
		else
			m_pCharacter->Die(m_ClientID, WEAPON_GAME);
		m_CampTick = -1;
		m_SentCampMsg = false;
		return 1;
	}
	return 0;
}
