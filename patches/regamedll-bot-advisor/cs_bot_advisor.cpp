/*
*
*   This program is free software; you can redistribute it and/or modify it
*   under the terms of the GNU General Public License as published by the
*   Free Software Foundation; either version 2 of the License, or (at
*   your option) any later version.
*
*   This program is distributed in the hope that it will be useful, but
*   WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*   General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program; if not, write to the Free Software Foundation,
*   Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*   In addition, as a special exception, the author gives permission to
*   link the code of this program with the Half-Life Game Engine ("HL
*   Engine") and Modified Game Libraries ("MODs") developed by Valve,
*   L.L.C ("Valve").  You must obey the GNU General Public License in all
*   respects for all of the code used other than the HL Engine and MODs
*   from Valve.  If you modify this file, you may extend this exception
*   to your version of the file, but you are not obligated to do so.  If
*   you do not wish to do so, delete this exception statement from your
*   version.
*
*/

#include "precompiled.h"
#include "cs_bot_advisor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <direct.h>
#define ADVISOR_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define ADVISOR_MKDIR(p) mkdir((p), 0755)
#endif

// Per-bot table is indexed by entindex(); max clients is 32 in GoldSrc.
#define ADVISOR_MAX_BOTS (MAX_CLIENTS + 1)

// On a defuse map the bomb targets are zones; which index is site A and which
// is site B depends on map-entity order, so we read it from the entity rather
// than assuming. de_dust2 names them "tgt_a" / "tgt_b".
#define ADVISOR_ZONE_FALLBACK_A 0
#define ADVISOR_ZONE_FALLBACK_B 1

// ---- cvars --------------------------------------------------------------

static cvar_t cv_advisor_enable    = { "advisor_enable", "0", FCVAR_SERVER, 0.0f, nullptr };
static cvar_t cv_advisor_debug     = { "advisor_debug", "0", FCVAR_SERVER, 0.0f, nullptr };
static cvar_t cv_advisor_force     = { "advisor_force_intent", "", FCVAR_SERVER, 0.0f, nullptr };
static cvar_t cv_advisor_poll      = { "advisor_poll_interval", "0.5", FCVAR_SERVER, 0.0f, nullptr };
static cvar_t cv_advisor_heartbeat = { "advisor_heartbeat", "3.0", FCVAR_SERVER, 0.0f, nullptr };

// ---- state --------------------------------------------------------------

struct AdvisorTeamState
{
	int  queuedIntent;	// intent to push to this team's bots
	int  lastSeq;		// seq of the last plan file consumed
	int  history[8];	// recent intents, oldest first
	int  historyCount;
	bool dirty;			// observation needs (re)writing
};

static AdvisorTeamState g_team[3];	// index 1 = TERRORIST, 2 = CT
static int  g_round;
static float g_nextPoll;
static float g_nextHeartbeat;
static int  g_lastAlive[3];
static bool g_lastBombPlanted;
static bool g_dirReady;
static int  g_appliedByEnt[ADVISOR_MAX_BOTS];

static const char *s_intentNames[ADVISOR_INTENT_COUNT] =
{
	"NONE",
	"DEFAULT",
	"RUSH_B",
	"RUSH_A_LONG",
	"RUSH_A_SHORT",
	"SPLIT_A",
	"HOLD_AFTER_PLANT",
	"STACK_A",
	"STACK_B",
	"RETAKE_A",
	"RETAKE_B",
	"ECO_SAVE",
};

const char *Advisor_IntentName( AdvisorIntent intent )
{
	if (intent < 0 || intent >= ADVISOR_INTENT_COUNT)
		return "NONE";

	return s_intentNames[intent];
}

AdvisorIntent Advisor_IntentFromName( const char *name )
{
	if (!name || !name[0])
		return ADVISOR_INTENT_NONE;

	for (int i = 0; i < ADVISOR_INTENT_COUNT; i++)
	{
		if (Q_stricmp(name, s_intentNames[i]) == 0)
			return (AdvisorIntent)i;
	}

	return ADVISOR_INTENT_NONE;
}

// ---- paths --------------------------------------------------------------

static void Advisor_Dir( char *out, size_t n )
{
	char gd[64] = { 0 };
	GET_GAME_DIR(gd);

	if (gd[0] == '\0')
		Q_strlcpy(gd, "cstrike", sizeof(gd));

	// drop a trailing separator so gd + "/advisor" is well formed
	size_t len = strlen(gd);
	while (len > 0 && (gd[len - 1] == '/' || gd[len - 1] == '\\'))
		gd[--len] = '\0';

	Q_snprintf(out, n, "%s/advisor", gd);
}

static void Advisor_FilePath( char *out, size_t n, int team, const char *kind )
{
	char dir[128];
	Advisor_Dir(dir, sizeof(dir));
	Q_snprintf(out, n, "%s/%s_%s.txt", dir, kind, (team == TERRORIST) ? "t" : "ct");
}

static void Advisor_AtomicRename( const char *from, const char *to )
{
	if (rename(from, to) == 0)
		return;

#ifdef _WIN32
	remove(to);
	rename(from, to);
#endif
}

// ---- bomb sites ---------------------------------------------------------
//
// A zone's site letter comes from its map entity: de_dust2 names the bomb
// targets "tgt_a" / "tgt_b", and it does so through the "target" key (not
// "targetname"). Falls back to the zone index when a map does not name them.

static char Advisor_ZoneLetter( int zoneIndex )
{
	if (zoneIndex < 0 || zoneIndex >= TheCSBots()->GetZoneCount())
		return 0;

	const CCSBotManager::Zone *z = TheCSBots()->GetZone(zoneIndex);
	if (!z || !z->m_entity)
		return 0;

	const char *name = STRING(z->m_entity->pev->target);
	if (!name || !name[0])
		name = STRING(z->m_entity->pev->targetname);
	if (!name || !name[0])
		return 0;

	char last = (char)tolower((unsigned char)name[strlen(name) - 1]);
	return (last == 'a' || last == 'b') ? last : 0;
}

static int Advisor_ZoneIndexForSite( char site )
{
	for (int i = 0; i < TheCSBots()->GetZoneCount(); i++)
	{
		if (Advisor_ZoneLetter(i) == site)
			return i;
	}

	return (site == 'a') ? ADVISOR_ZONE_FALLBACK_A : ADVISOR_ZONE_FALLBACK_B;
}

// ---- observation helpers ------------------------------------------------

static void Advisor_TeamStats( TeamName team, int *alive, int *moneyAvg, int *count )
{
	int a = 0, money = 0, n = 0;

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer *p = UTIL_PlayerByIndex(i);
		if (!p || !UTIL_IsValidPlayer(p))
			continue;

		if (p->m_iTeam != team)
			continue;

		n++;
		money += p->m_iAccount;

		if (p->IsAlive())
			a++;
	}

	*alive = a;
	*count = n;
	*moneyAvg = (n > 0) ? (money / n) : 0;
}

static void Advisor_CountAlive( int *aliveT, int *aliveCT )
{
	int a = 0, b = 0;

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer *p = UTIL_PlayerByIndex(i);
		if (!p || !UTIL_IsValidPlayer(p) || !p->IsAlive())
			continue;

		if (p->m_iTeam == TERRORIST)
			a++;
		else if (p->m_iTeam == CT)
			b++;
	}

	*aliveT = a;
	*aliveCT = b;
}

static bool Advisor_BombCarried()
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer *p = UTIL_PlayerByIndex(i);
		if (p && UTIL_IsValidPlayer(p) && p->IsAlive() && p->m_bHasC4)
			return true;
	}

	return false;
}

// Returns the zone index the planted bomb sits in, or -1.
static int Advisor_PlantedZoneIndex()
{
	if (!TheCSBots()->IsBombPlanted())
		return -1;

	CBaseEntity *pEntity = nullptr;
	while ((pEntity = UTIL_FindEntityByClassname(pEntity, "grenade")) != nullptr)
	{
		if (!((CGrenade *)pEntity)->m_bIsC4)
			continue;

		const CCSBotManager::Zone *z = TheCSBots()->GetZone(&pEntity->pev->origin);
		for (int i = 0; i < TheCSBots()->GetZoneCount(); i++)
		{
			if (z == TheCSBots()->GetZone(i))
				return i;
		}
	}

	return -1;
}

static const char *Advisor_BombZoneLabel()
{
	int idx = Advisor_PlantedZoneIndex();

	if (idx < 0)
		return "none";

	char letter = Advisor_ZoneLetter(idx);
	if (letter == 'a')
		return "A";
	if (letter == 'b')
		return "B";

	return (idx == ADVISOR_ZONE_FALLBACK_B) ? "B" : "A";
}

// ---- mailbox write ------------------------------------------------------

static void Advisor_WriteObservation( int team )
{
	char path[256], tmp[320];
	Advisor_FilePath(path, sizeof(path), team, "observe");
	Q_snprintf(tmp, sizeof(tmp), "%s.tmp", path);

	FILE *f = fopen(tmp, "w");
	if (!f)
		return;

	int aliveUs, moneyUs, nUs, aliveThem, moneyThem, nThem;
	Advisor_TeamStats((TeamName)team, &aliveUs, &moneyUs, &nUs);
	Advisor_TeamStats((team == TERRORIST) ? CT : TERRORIST, &aliveThem, &moneyThem, &nThem);

	int scoreUs = (team == TERRORIST) ? CSGameRules()->m_iNumTerroristWins : CSGameRules()->m_iNumCTWins;
	int scoreThem = (team == TERRORIST) ? CSGameRules()->m_iNumCTWins : CSGameRules()->m_iNumTerroristWins;

	const char *phase = "live";
	if (CSGameRules()->IsFreezePeriod())
		phase = "freeze";
	else if (TheCSBots()->IsBombPlanted())
		phase = "postplant";

	const char *bomb = "none";
	if (TheCSBots()->IsBombPlanted())
		bomb = "planted";
	else if (Advisor_BombCarried())
		bomb = "carried";

	fprintf(f, "v=1\n");
	fprintf(f, "ts=%.2f\n", gpGlobals->time);
	fprintf(f, "team=%s\n", (team == TERRORIST) ? "T" : "CT");
	fprintf(f, "round=%d\n", g_round);
	fprintf(f, "phase=%s\n", phase);
	fprintf(f, "score_us=%d\n", scoreUs);
	fprintf(f, "score_them=%d\n", scoreThem);
	fprintf(f, "alive_us=%d\n", aliveUs);
	fprintf(f, "alive_them=%d\n", aliveThem);
	fprintf(f, "players_us=%d\n", nUs);
	fprintf(f, "players_them=%d\n", nThem);
	fprintf(f, "money_avg_us=%d\n", moneyUs);
	fprintf(f, "money_avg_them=%d\n", moneyThem);
	fprintf(f, "bomb=%s\n", bomb);
	fprintf(f, "bomb_zone=%s\n", Advisor_BombZoneLabel());

	fprintf(f, "history=");
	for (int i = 0; i < g_team[team].historyCount; i++)
	{
		if (i > 0)
			fputc(',', f);
		fputs(Advisor_IntentName((AdvisorIntent)g_team[team].history[i]), f);
	}
	fputc('\n', f);

	fclose(f);
	Advisor_AtomicRename(tmp, path);
}

// ---- mailbox read -------------------------------------------------------

static void Advisor_PushHistory( int team, int intent )
{
	AdvisorTeamState *st = &g_team[team];
	const int cap = (int)(sizeof(st->history) / sizeof(st->history[0]));

	if (st->historyCount < cap)
	{
		st->history[st->historyCount++] = intent;
		return;
	}

	memmove(&st->history[0], &st->history[1], (size_t)(cap - 1) * sizeof(st->history[0]));
	st->history[cap - 1] = intent;
}

static void Advisor_PollPlan( int team )
{
	char path[256];
	Advisor_FilePath(path, sizeof(path), team, "plan");

	FILE *f = fopen(path, "r");
	if (!f)
		return;

	int planRound = -1, seq = -1, intent = ADVISOR_INTENT_NONE;
	char line[256];

	while (fgets(line, sizeof(line), f))
	{
		char *eq = strchr(line, '=');
		if (!eq)
			continue;

		*eq = '\0';

		char *key = line;
		char *val = eq + 1;

		size_t vl = strlen(val);
		while (vl > 0 && (val[vl - 1] == '\n' || val[vl - 1] == '\r' || val[vl - 1] == ' ' || val[vl - 1] == '\t'))
			val[--vl] = '\0';

		if (strcmp(key, "round") == 0)
			planRound = atoi(val);
		else if (strcmp(key, "seq") == 0)
			seq = atoi(val);
		else if (strcmp(key, "intent") == 0)
			intent = (int)Advisor_IntentFromName(val);
	}
	fclose(f);

	if (planRound != g_round)						// plan from another round
		return;
	if (seq <= g_team[team].lastSeq)				// already consumed
		return;
	if (intent <= ADVISOR_INTENT_NONE)				// unknown/empty
		return;

	g_team[team].lastSeq = seq;
	g_team[team].queuedIntent = intent;
	Advisor_PushHistory(team, intent);
	g_team[team].dirty = true;

	if (cv_advisor_debug.value != 0.0f)
	{
		CONSOLE_ECHO("ADVISOR: team %s <- %s (round %d, seq %d)\n",
			(team == TERRORIST) ? "T" : "CT",
			Advisor_IntentName((AdvisorIntent)intent), planRound, seq);
	}
}

// ---- intent execution ---------------------------------------------------

static void Advisor_GoToZone( CCSBot *bot, int zoneIndex )
{
	if (zoneIndex < 0 || zoneIndex >= TheCSBots()->GetZoneCount())
		return;

	const Vector *pos = TheCSBots()->GetRandomPositionInZone(TheCSBots()->GetZone(zoneIndex));
	if (!pos)
	{
		if (cv_advisor_debug.value >= 1.0f)
			CONSOLE_ECHO("ADVISOR: zone %d has no usable position (areas=%d)\n",
				zoneIndex, TheCSBots()->GetZone(zoneIndex)->m_areaCount);
		return;
	}

	if (cv_advisor_debug.value >= 2.0f)
		CONSOLE_ECHO("ADVISOR: %s -> zone %d goal %.0f %.0f %.0f\n",
			STRING(bot->pev->netname), zoneIndex, pos->x, pos->y, pos->z);

	bot->SetTask(CCSBot::SEEK_AND_DESTROY);
	bot->SetDisposition(CCSBot::ENGAGE_AND_INVESTIGATE);
	bot->MoveTo(pos);
}

static void Advisor_StackInZone( CCSBot *bot, int zoneIndex )
{
	if (zoneIndex < 0 || zoneIndex >= TheCSBots()->GetZoneCount())
		return;

	const Vector *pos = TheCSBots()->GetRandomPositionInZone(TheCSBots()->GetZone(zoneIndex));
	if (!pos)
		return;

	bot->SetTask(CCSBot::GUARD_BOMB_ZONE);
	bot->SetDisposition(CCSBot::OPPORTUNITY_FIRE);
	bot->Hide(pos, 20.0f, false);
}

static void Advisor_ExecuteIntent( CCSBot *bot, AdvisorIntent intent )
{
	switch (intent)
	{
	case ADVISOR_INTENT_RUSH_B:
	case ADVISOR_INTENT_RETAKE_B:
		Advisor_GoToZone(bot, Advisor_ZoneIndexForSite('b'));
		break;

	case ADVISOR_INTENT_RUSH_A_LONG:
	case ADVISOR_INTENT_RUSH_A_SHORT:
	case ADVISOR_INTENT_SPLIT_A:
	case ADVISOR_INTENT_RETAKE_A:
		Advisor_GoToZone(bot, Advisor_ZoneIndexForSite('a'));
		break;

	case ADVISOR_INTENT_STACK_A:
		Advisor_StackInZone(bot, Advisor_ZoneIndexForSite('a'));
		break;

	case ADVISOR_INTENT_STACK_B:
		Advisor_StackInZone(bot, Advisor_ZoneIndexForSite('b'));
		break;

	case ADVISOR_INTENT_HOLD_AFTER_PLANT:
		Advisor_StackInZone(bot, Advisor_PlantedZoneIndex());
		break;

	case ADVISOR_INTENT_ECO_SAVE:
		bot->SetDisposition(CCSBot::SELF_DEFENSE);
		break;

	default:
		break;
	}
}

// ---- public hooks -------------------------------------------------------

void Advisor_RegisterCVars()
{
	CVAR_REGISTER(&cv_advisor_enable);
	CVAR_REGISTER(&cv_advisor_debug);
	CVAR_REGISTER(&cv_advisor_force);
	CVAR_REGISTER(&cv_advisor_poll);
	CVAR_REGISTER(&cv_advisor_heartbeat);
}

void Advisor_OnRoundStart()
{
	g_round++;

	for (int t = TERRORIST; t <= CT; t++)
	{
		// play stock until a plan for *this* round arrives (plans carry their round)
		g_team[t].queuedIntent = ADVISOR_INTENT_DEFAULT;
		g_team[t].dirty = true;
	}

	g_lastBombPlanted = false;
	g_lastAlive[TERRORIST] = 0;
	g_lastAlive[CT] = 0;
	g_nextHeartbeat = 0.0f;
	g_nextPoll = 0.0f;

	for (int i = 0; i < ADVISOR_MAX_BOTS; i++)
		g_appliedByEnt[i] = ADVISOR_INTENT_NONE;

	if (cv_advisor_debug.value != 0.0f)
	{
		CONSOLE_ECHO("ADVISOR: round %d start\n", g_round);

		for (int i = 0; i < TheCSBots()->GetZoneCount(); i++)
		{
			const CCSBotManager::Zone *z = TheCSBots()->GetZone(i);
			char letter = Advisor_ZoneLetter(i);
			const char *name = (z && z->m_entity) ? STRING(z->m_entity->pev->target) : "";
			CONSOLE_ECHO("ADVISOR: zone %d (%c) areas=%d target='%s' center %.0f %.0f %.0f\n",
				i, letter ? (char)toupper((unsigned char)letter) : '-',
				z ? z->m_areaCount : -1, name,
				z ? z->m_center.x : 0.0f, z ? z->m_center.y : 0.0f, z ? z->m_center.z : 0.0f);
		}
	}
}

void Advisor_FrameUpdate()
{
	if (cv_advisor_enable.value == 0.0f)
		return;

	if (!TheCSBots() || !CSGameRules())
		return;

	if (!g_dirReady)
	{
		char dir[128];
		Advisor_Dir(dir, sizeof(dir));
		ADVISOR_MKDIR(dir);		// best effort; ignore EEXIST
		g_dirReady = true;
	}

	const float now = gpGlobals->time;

	// event detection: alive counts and the bomb state drive mid-round re-planning
	int aliveT = 0, aliveCT = 0;
	Advisor_CountAlive(&aliveT, &aliveCT);
	const bool planted = TheCSBots()->IsBombPlanted();

	if (aliveT != g_lastAlive[TERRORIST] || aliveCT != g_lastAlive[CT])
	{
		g_lastAlive[TERRORIST] = aliveT;
		g_lastAlive[CT] = aliveCT;
		g_team[TERRORIST].dirty = true;
		g_team[CT].dirty = true;
	}

	if (planted != g_lastBombPlanted)
	{
		g_lastBombPlanted = planted;
		g_team[TERRORIST].dirty = true;
		g_team[CT].dirty = true;
	}

	float heartbeat = cv_advisor_heartbeat.value;
	if (heartbeat < 0.5f)
		heartbeat = 0.5f;

	if (now >= g_nextHeartbeat)
	{
		g_nextHeartbeat = now + heartbeat;
		g_team[TERRORIST].dirty = true;
		g_team[CT].dirty = true;
	}

	// advisor_force_intent <NAME>: drive the DLL half with no sidecar attached
	if (cv_advisor_force.string[0] != '\0')
	{
		AdvisorIntent forced = Advisor_IntentFromName(cv_advisor_force.string);
		if (forced > ADVISOR_INTENT_NONE)
		{
			g_team[TERRORIST].queuedIntent = forced;
			g_team[CT].queuedIntent = forced;
		}
	}

	for (int t = TERRORIST; t <= CT; t++)
	{
		if (g_team[t].dirty)
		{
			Advisor_WriteObservation(t);
			g_team[t].dirty = false;
		}
	}

	float poll = cv_advisor_poll.value;
	if (poll < 0.1f)
		poll = 0.1f;

	if (now >= g_nextPoll)
	{
		g_nextPoll = now + poll;
		Advisor_PollPlan(TERRORIST);
		Advisor_PollPlan(CT);
	}
}

void Advisor_ApplyIntent( CCSBot *bot )
{
	if (cv_advisor_enable.value == 0.0f || !bot)
		return;

	const TeamName team = bot->m_iTeam;
	if (team != TERRORIST && team != CT)
		return;

	const int queued = g_team[team].queuedIntent;
	if (queued <= ADVISOR_INTENT_NONE)
		return;

	const int ent = bot->entindex();
	if (ent < 0 || ent >= ADVISOR_MAX_BOTS)
		return;

	// DEFAULT = no override; just remember so a later intent re-applies cleanly
	if (queued == ADVISOR_INTENT_DEFAULT)
	{
		g_appliedByEnt[ent] = ADVISOR_INTENT_DEFAULT;
		return;
	}

	if (g_appliedByEnt[ent] == queued)
		return;		// already applied: hand control back to the stock AI

	Advisor_ExecuteIntent(bot, (AdvisorIntent)queued);
	g_appliedByEnt[ent] = queued;

	if (cv_advisor_debug.value != 0.0f)
	{
		CONSOLE_ECHO("ADVISOR: %s applied %s\n",
			STRING(bot->pev->netname), Advisor_IntentName((AdvisorIntent)queued));
	}
}
