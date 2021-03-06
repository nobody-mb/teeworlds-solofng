/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_PLAYER_H
#define GAME_SERVER_PLAYER_H

// this include should perhaps be removed
#include "entities/character.h"
#include "gamecontext.h"
#include <time.h>

#define ID_NAME(id) (Server()->ClientName(id))
#define PLAYER_NUM(i) (GameServer()->m_apPlayers[i])

#define STATS_DIR "stats"
#ifndef TEE_STATS
#define TEE_STATS
struct tee_stats {
		int spree, spree_max, multi, multis[6];
		int kills, kills_wrong, kills_x2;
		int lastkilltime, frozeby, deaths, steals, suicides;
		int shots, freezes, frozen, hammers, hammered, teamhooks;
		int num_samples;
		unsigned short avg_ping;
		unsigned char ping_tick, is_bot;
		int bounce_shots, tick_count;
		time_t join_time;
		int num_games, max_multi;
	};
#endif


// player object
class CPlayer
{
	MACRO_ALLOC_POOL_ID()

public:
	CPlayer(CGameContext *pGameServer, int ClientID, int Team);
	~CPlayer();

	void Init(int CID);

	void TryRespawn();
	void Respawn();
	void SetTeam(int Team, bool DoChatMsg=true);
	int GetTeam() const { return m_Team; };
	int GetCID() const { return m_ClientID; };

	void Tick();
	void PostTick();
	void Snap(int SnappingClient);

	void OnDirectInput(CNetObj_PlayerInput *NewInput);
	void OnPredictedInput(CNetObj_PlayerInput *NewInput);
	void OnDisconnect(const char *pReason);
	
	void KillCharacter(int Weapon = WEAPON_GAME);
	CCharacter *GetCharacter();
	
	//---------------------------------------------------------
	// this is used for snapping so we know how we can clip the view for the player
	vec2 m_ViewPos;

	// states if the client is chatting, accessing a menu etc.
	int m_PlayerFlags;

	// used for snapping to just update latency if the scoreboard is active
	int m_aActLatency[MAX_CLIENTS];

	// used for spectator mode
	int m_SpectatorID;

	bool m_IsReady;

	//
	int m_Vote;
	int m_VotePos;
	//
	int m_LastVoteCall;
	int m_LastVoteTry;
	int m_LastChat;
	int m_LastSetTeam;
	int m_LastSetSpectatorMode;
	int m_LastChangeInfo;
	int m_LastEmote;
	int m_LastKill;

	// TODO: clean this up
	struct
	{
		char m_SkinName[64];
		int m_UseCustomColor;
		int m_ColorBody;
		int m_ColorFeet;
	} m_TeeInfos;

	int m_RespawnTick;
	int m_DieTick;
	int m_Score;
	int m_ScoreStartTick;
	bool m_ForceBalanced;
	int m_LastActionTick;
	int m_TeamChangeTick;
	struct
	{
		int m_TargetX;
		int m_TargetY;
	} m_LatestActivity;

	int m_ChatScore;
	
	// network latency calculations
	struct
	{
		int m_Accum;
		int m_AccumMin;
		int m_AccumMax;
		int m_Avg;
		int m_Min;
		int m_Max;
	} m_Latency;
	//Anticamper (from zCatch)
	int Anticamper();


	bool m_SentCampMsg;
	int m_CampTick;
	vec2 m_CampPos;

	//struct tee_stats gstats;
	//struct tee_stats totals;
	
	bool GetBot(int BotType) { switch (BotType) {case 0: return m_SpinBot; case 1: return m_AimBot; default: return false;} }
	void SetBot(int BotType) { switch (BotType) {case 0: m_SpinBot = true; break; case 1:m_AimBot = true; break;} }
	
	float m_avgdist;
	float m_avglen;
	
	int m_numdsamp;
	int m_numlsamp;
	
	CGameContext *GameServer() const { return m_pGameServer; }

private:
	CCharacter *m_pCharacter;
	CGameContext *m_pGameServer;

	IServer *Server() const;

	//
	bool m_Spawning;
	int m_ClientID;
	int m_Team;
	
	//Anti-Bot
	bool m_SpinBot;
	bool m_AimBot;
};

#endif
