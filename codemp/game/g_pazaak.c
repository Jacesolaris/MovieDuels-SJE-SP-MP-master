/*
===========================================================================
Pazaak - multiplayer host

Runs the Pazaak matches (pazaak_core) on the server, like the Jedi Knight Galaxies pazaak.lua did.
The Play Pazaak key ("pazaak") does one of three things, depending on what is in the crosshair:
1. nobody: a match against the AI.
2. a bot: the bot accepts, sits down with you and is the opponent on the board. Really the AI plays
   with his name, the bot only sits there (the bot AI does nothing meanwhile, ai_main.c).
3. another player: he is challenged. He accepts with his own Play Pazaak key ("pazaak", or
   "pazaak accept"), or "pazaak decline"; a challenge runs out after PZK_CHALLENGE_TIME.
A NPC in the crosshair lends his name to the AI, like a bot, but does not sit down.
Whoever is challenged must be within PZK_CHALLENGE_RANGE ("too far away" otherwise), and a player who
accepts still within PZK_ACCEPT_RANGE.
- "~pzk ...": the board's answers (card selection, use card, stand, end turn, dialogs, forfeit)
The boards get "pzk ..." server commands. Everyone at a match first sits down to meditate (BOTH_MEDITATE),
facing the opponent, and is invulnerable until it ends, then stands up again. Nobody can challenge
him meanwhile, he can't move or fight (g_active.c), and bots leave him alone (ai_main.c).
No match while it would not fit (Pzk_BusyReason): a cutscene of a coop mission (which also calls off
running matches), a duel, a vehicle, a fight, in the air or water, enemies near. "Pazaak is unavailable".
===========================================================================
*/

#include "g_local.h"
#include "pazaak_core.h"
#include "g_pazaak.h"

#define PZK_CHALLENGE_TIME	20000
#define PZK_LOOK_RANGE		2048	// who is in the crosshair (further away: nobody, the AI)
#define PZK_CHALLENGE_RANGE	256		// a player, bot or NPC must be this close to be challenged
#define PZK_ACCEPT_RANGE	384		// and still this close when the challenge is accepted
#define PZK_SIT_TIME		1500	// time to sit down before the board opens, if the anim does not tell
#define PZK_ENEMY_RANGE		1024	// no match while an NPC who is after the player is this close (and in his PVS)
#define PZK_BOT_FIGHT_RANGE	1024	// a bot fighting someone else this close turns a challenge down

extern void NPC_SetAnim(gentity_t* ent, int setAnimParts, int anim, int setAnimFlags);
extern qboolean PM_InKnockDown(const playerState_t* ps);
extern int Bot_CurrentEnemy(int client);
extern qboolean in_camera;
extern qboolean player_locked;
extern qboolean inGameCinematic;

static pzkGame_t pzkGames[MAX_CLIENTS];
static int pzkClientGame[MAX_CLIENTS];      // game index + 1, 0 = not playing
static int pzkClientPid[MAX_CLIENTS];       // 1 or 2
static int pzkClientHadGod[MAX_CLIENTS];    // had FL_GODMODE before the match
static int pzkChallenger[MAX_CLIENTS];      // client number + 1 of who challenged this client
static int pzkChallengeTime[MAX_CLIENTS];
static int pzkStartAt[MAX_CLIENTS];         // per game: time its board opens (0 = started)
static int pzkGameBot[MAX_CLIENTS];         // per game: client number + 1 of the bot who sits in for the AI
static int pzkBotGame[MAX_CLIENTS];         // per client: game index + 1 of the match he sits in for the AI
static int pzkBotSitAt[MAX_CLIENTS];        // per game: the bot accepted in the air (bots hop about), he sits once he lands (0 = sits)
static const char pzkNoGround[] = "You need solid ground to sit down.";

// the shop (2026-11-01, at the end of the file)
static void G_Shop_Init(void);
static void Shop_ClientGone(int clientNum);
static qboolean Shop_ClientCommand(gentity_t* ent, const char* cmd);

void G_Pazaak_Init(void)
{
	memset(pzkGames, 0, sizeof(pzkGames));
	memset(pzkClientGame, 0, sizeof(pzkClientGame));
	memset(pzkClientPid, 0, sizeof(pzkClientPid));
	memset(pzkClientHadGod, 0, sizeof(pzkClientHadGod));
	memset(pzkChallenger, 0, sizeof(pzkChallenger));
	memset(pzkChallengeTime, 0, sizeof(pzkChallengeTime));
	memset(pzkStartAt, 0, sizeof(pzkStartAt));
	memset(pzkGameBot, 0, sizeof(pzkGameBot));
	memset(pzkBotGame, 0, sizeof(pzkBotGame));
	memset(pzkBotSitAt, 0, sizeof(pzkBotSitAt));
	G_Shop_Init();
}

// Sits at a Pazaak match: a player at his board, or a bot who sits in for the AI
qboolean G_Pazaak_IsPlaying(const int clientNum)
{
	return clientNum >= 0 && clientNum < MAX_CLIENTS && (pzkClientGame[clientNum] || pzkBotGame[clientNum]) ? qtrue : qfalse;
}

static void Pzk_Print(const int clientNum, const char* text)
{
	trap->SendServerCommand(clientNum, va("print \"%s\n\"", text));
}

// Turns to face the other one (yaw only), before sitting down: the meditate pose then holds the view
static void Pzk_Face(gentity_t* ent, const gentity_t* other)
{
	vec3_t dir, angles;

	if (!other)
	{
		return;
	}
	VectorSubtract(other->r.currentOrigin, ent->r.currentOrigin, dir);
	dir[2] = 0;
	if (VectorLength(dir) < 1.0f)
	{
		return;
	}
	vectoangles(dir, angles);
	angles[PITCH] = 0;
	angles[ROLL] = 0;
	SetClientViewAngle(ent, angles);
}

// Saber off, sit down to meditate (the meditate code of bg_pmove.c holds the pose while there is no input,
// which g_active.c makes sure of), invulnerable while playing
static void Pzk_SitDown(gentity_t* ent, const gentity_t* opponent)
{
	Pzk_Face(ent, opponent);
	if (ent->client->ps.weapon == WP_SABER && ent->client->ps.saberHolstered != 2)
	{
		G_Sound(ent, CHAN_WEAPON, ent->client->saber[0].soundOff);
		ent->client->ps.saberHolstered = 2;
	}
	VectorClear(ent->client->ps.velocity);
	NPC_SetAnim(ent, SETANIM_BOTH, BOTH_MEDITATE, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	pzkClientHadGod[ent->s.number] = ent->flags & FL_GODMODE ? 1 : 0;
	ent->flags |= FL_GODMODE;
}

// Stand up again, no longer invulnerable
static void Pzk_StandUp(gentity_t* ent)
{
	if (!pzkClientHadGod[ent->s.number])
	{
		ent->flags &= ~FL_GODMODE;
	}
	pzkClientHadGod[ent->s.number] = 0;
	if (ent->client->ps.legsAnim == BOTH_MEDITATE && ent->health > 0)
	{
		NPC_SetAnim(ent, SETANIM_BOTH, BOTH_MEDITATE_END, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	}
}

// The bot of a match gets up and goes back to his business (the AI may go on with his name)
static void Pzk_ReleaseBot(const int index)
{
	const int bot = pzkGameBot[index] - 1;
	gentity_t* ent = NULL;

	pzkGameBot[index] = 0;
	pzkBotSitAt[index] = 0;
	if (bot < 0 || bot >= MAX_CLIENTS)
	{
		return;
	}
	pzkBotGame[bot] = 0;
	ent = &g_entities[bot];
	if (ent->inuse && ent->client)
	{
		Pzk_StandUp(ent);
	}
	pzkClientHadGod[bot] = 0;
}

static void Pzk_HostSend(pzkGame_t* g, const int pid, const char* text)
{
	const int clientNum = g->hostData[pid - 1];
	if (clientNum >= 0 && clientNum < MAX_CLIENTS)
	{
		trap->SendServerCommand(clientNum, va("pzk %s", text));
	}
}

static void Pzk_HostFinish(pzkGame_t* g, const int winnerPid)
{
	char winnerName[64] = { 0 };
	int pid = 0;

	winnerName[0] = 0;
	if (winnerPid)
	{
		Q_strncpyz(winnerName, g->players[winnerPid - 1].name, sizeof(winnerName));
	}

	for (pid = 1; pid <= 2; pid++)
	{
		const int clientNum = g->hostData[pid - 1];
		gentity_t* ent = NULL;
		if (clientNum < 0 || clientNum >= MAX_CLIENTS)
		{
			continue;
		}
		pzkClientGame[clientNum] = 0;
		pzkClientPid[clientNum] = 0;
		ent = &g_entities[clientNum];
		if (ent->inuse && ent->client)
		{
			Pzk_StandUp(ent);
			if (winnerPid)
			{
				Pzk_Print(clientNum, va("^5Pazaak:^7 %s^7 wins the match.", winnerName));
			}
			else
			{
				Pzk_Print(clientNum, "^5Pazaak:^7 the match was called off.");
			}
		}
		pzkClientHadGod[clientNum] = 0;
	}
	Pzk_ReleaseBot(g - pzkGames);
	pzkStartAt[g - pzkGames] = 0;
	g->hostData[0] = g->hostData[1] = -1;
}

static int Pzk_FreeGame(void)
{
	int i = 0;
	for (i = 0; i < MAX_CLIENTS; i++)
	{
		if (!pzkGames[i].inUse)
		{
			return i;
		}
	}
	return -1;
}

// The entity in the player's crosshair (up to PZK_LOOK_RANGE away)
static gentity_t* Pzk_LookTarget(gentity_t* ent)
{
	vec3_t start, end, fwd;
	trace_t tr;

	VectorCopy(ent->client->ps.origin, start);
	start[2] += ent->client->ps.viewheight;
	AngleVectors(ent->client->ps.viewangles, fwd, NULL, NULL);
	VectorMA(start, PZK_LOOK_RANGE, fwd, end);
	trap->Trace(&tr, start, NULL, NULL, end, ent->s.number, MASK_SHOT, qfalse, 0, 0);
	if (tr.entityNum >= ENTITYNUM_MAX_NORMAL || tr.entityNum == ent->s.number)
	{
		return NULL;
	}
	return &g_entities[tr.entityNum];
}

// A cutscene of a singleplayer mission runs (coop): camera, a script holding the players, or a video
static qboolean Pzk_InCutscene(void)
{
	return in_camera || player_locked || inGameCinematic ? qtrue : qfalse;
}

// An NPC who is after the player is close by (the match would make him a sitting, invulnerable target)
static qboolean Pzk_EnemyNear(const gentity_t* ent)
{
	// static: too big for the stack (C6262); filled by EntitiesInBox before it is read
	static int list[MAX_GENTITIES];
	vec3_t mins = { 0 }, maxs = { 0 };
	int i = 0, num = 0;

	for (i = 0; i < 3; i++)
	{
		mins[i] = ent->r.currentOrigin[i] - PZK_ENEMY_RANGE;
		maxs[i] = ent->r.currentOrigin[i] + PZK_ENEMY_RANGE;
	}
	num = trap->EntitiesInBox(mins, maxs, list, MAX_GENTITIES);
	for (i = 0; i < num; i++)
	{
		const gentity_t* other = &g_entities[list[i]];
		if (other != ent && other->inuse && other->client && other->NPC && other->health > 0 && other->enemy == ent
			&& trap->InPVS(other->r.currentOrigin, ent->r.currentOrigin))
		{
			return qtrue;
		}
	}
	return qfalse;
}

// Why the player can't start a match now (NULL: he can)
static const char* Pzk_BusyReason(const gentity_t* ent)
{
	const playerState_t* ps = &ent->client->ps;

	if (Pzk_InCutscene())
	{
		return "A cutscene is playing.";
	}
	if (level.intermissiontime)
	{
		return "The round is over.";
	}
	if (ps->duelInProgress)
	{
		return "Not during a duel.";
	}
	if (ps->m_iVehicleNum)
	{
		return "Not while riding a vehicle.";
	}
	if (ps->emplacedIndex || ps->viewEntity > 0 && ps->viewEntity < ENTITYNUM_WORLD)
	{
		return "Not while controlling something else.";
	}
	if (ps->eFlags2 & EF2_HELD_BY_MONSTER || ps->fd.forceGripBeingGripped > level.time
		|| ps->saberLockTime > level.time || PM_InKnockDown(ps))
	{
		return "Not in the middle of a fight.";
	}
	if (ps->groundEntityNum == ENTITYNUM_NONE || ent->waterlevel >= 2)
	{
		return pzkNoGround;
	}
	if (Pzk_EnemyNear(ent))
	{
		return "Enemies are nearby.";
	}
	return NULL;
}

// Why a bot can't sit in for the AI now (NULL: he can). Bots hop about a lot: one in the air still
// accepts, he sits down once he lands.
static const char* Pzk_BotBusy(const gentity_t* bot)
{
	const char* busy = Pzk_BusyReason(bot);
	return busy == pzkNoGround && bot->waterlevel < 2 ? NULL : busy;
}

// A bot who is fighting someone other than the challenger turns the challenge down
static qboolean Pzk_BotFighting(const gentity_t* bot, const gentity_t* challenger)
{
	const int enemy = Bot_CurrentEnemy(bot->s.number);
	const gentity_t* e = NULL;

	if (enemy < 0 || enemy >= ENTITYNUM_MAX_NORMAL || enemy == challenger->s.number)
	{
		return qfalse;
	}
	e = &g_entities[enemy];
	return e->inuse && e->health > 0 && Distance(e->r.currentOrigin, bot->r.currentOrigin) < PZK_BOT_FIGHT_RANGE
		? qtrue : qfalse;
}

static void Pzk_Unavailable(const int clientNum, const char* reason)
{
	trap->SendServerCommand(clientNum, va("cp \"Pazaak is unavailable at this time.\n%s\"", reason ? reason : ""));
}

static qboolean Pzk_CanPlay(gentity_t* ent, const qboolean tell)
{
	if (!ent || !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED)
	{
		return qfalse;
	}
	if (ent->client->sess.sessionTeam == TEAM_SPECTATOR || ent->health <= 0
		|| ent->client->ps.stats[STAT_HEALTH] <= 0 || ent->client->ps.pm_type == PM_DEAD
		|| ent->client->ps.pm_type == PM_SPECTATOR || ent->client->tempSpectate >= level.time) // waiting to respawn
	{
		if (tell)
		{
			Pzk_Unavailable(ent->s.number, "");
		}
		return qfalse;
	}
	return qtrue;
}

// How long the sitting down takes (the length of BOTH_MEDITATE, which then holds its last frame)
static int Pzk_SitTime(const gentity_t* ent)
{
	const int t = ent->client->ps.legsAnim == BOTH_MEDITATE ? ent->client->ps.legsTimer : 0;
	return t >= 300 && t <= 4000 ? t : PZK_SIT_TIME;
}

// p2: the other player, or NULL for the AI (with aiName; bot: the bot who sits in for it)
static void Pzk_StartMatch(gentity_t* p1, gentity_t* p2, const char* aiName, gentity_t* bot)
{
	int cards[PZK_NUM_SIDECARDS];
	int aiDeck[PZK_SIDEDECK_SIZE];
	const int index = Pzk_FreeGame();
	pzkGame_t* g = NULL;
	int sit = 0;

	if (index < 0)
	{
		Pzk_Print(p1->s.number, "^5Pazaak:^7 too many matches at once, try again later.");
		return;
	}
	g = &pzkGames[index];
	Pzk_Init(g, Pzk_HostSend, Pzk_HostFinish, (unsigned int)(level.time * 2654435761u) ^ (unsigned int)rand());
	g->hostData[0] = p1->s.number;
	g->hostData[1] = p2 ? p2->s.number : -1;

	Pzk_DefaultCards(cards);
	Pzk_SetPlayer(g, 1, p1->client->pers.netname, 0);
	Pzk_SetCards(g, 1, cards);
	if (p2)
	{
		Pzk_SetPlayer(g, 2, p2->client->pers.netname, 0);
		Pzk_SetCards(g, 2, cards);
	}
	else
	{
		Pzk_SetPlayer(g, 2, aiName && aiName[0] ? aiName : "AI", 1);
		Pzk_SetCards(g, 2, cards);
		Pzk_DefaultAISideDeck(aiDeck);
		Pzk_SetSideDeck(g, 2, aiDeck);
	}
	Pzk_ShowCardSelection(g, 1);

	pzkClientGame[p1->s.number] = index + 1;
	pzkClientPid[p1->s.number] = 1;
	Pzk_SitDown(p1, p2 ? p2 : bot);
	sit = Pzk_SitTime(p1);
	if (p2)
	{
		pzkClientGame[p2->s.number] = index + 1;
		pzkClientPid[p2->s.number] = 2;
		Pzk_SitDown(p2, p1);
		if (Pzk_SitTime(p2) > sit)
		{
			sit = Pzk_SitTime(p2);
		}
	}
	else if (bot)
	{
		pzkGameBot[index] = bot->s.number + 1;
		pzkBotGame[bot->s.number] = index + 1;
		if (bot->client->ps.groundEntityNum == ENTITYNUM_NONE)
		{
			// in the air: his AI stops now, he sits once he lands (G_Pazaak_RunFrame)
			pzkBotSitAt[index] = level.time;
		}
		else
		{
			Pzk_SitDown(bot, p1);
			if (Pzk_SitTime(bot) > sit)
			{
				sit = Pzk_SitTime(bot);
			}
		}
	}

	// The boards open once they sit (G_Pazaak_RunFrame): BOTH_MEDITATE is the sitting down, then it holds
	pzkStartAt[index] = level.time + sit;
}

// Whether a challenge to this client is still open
static qboolean Pzk_HasChallenge(const int clientNum)
{
	return pzkChallenger[clientNum] && level.time - pzkChallengeTime[clientNum] < PZK_CHALLENGE_TIME ? qtrue : qfalse;
}

static void Pzk_Answer(gentity_t* ent, const qboolean accept);

static void Pzk_Challenge(gentity_t* ent)
{
	gentity_t* target = NULL;
	const char* busy = NULL;
	const char* aiName = "AI";

	if (!Pzk_CanPlay(ent, qtrue))
	{
		return;
	}
	if (G_Pazaak_IsPlaying(ent->s.number))
	{
		Pzk_Print(ent->s.number, "^5Pazaak:^7 you are already playing.");
		return;
	}
	if (Pzk_HasChallenge(ent->s.number))
	{
		// Somebody challenged him: his Play Pazaak key accepts
		Pzk_Answer(ent, qtrue);
		return;
	}
	busy = Pzk_BusyReason(ent);
	if (busy)
	{
		Pzk_Unavailable(ent->s.number, busy);
		return;
	}

	target = Pzk_LookTarget(ent);
	if (target && target->client && Distance(ent->r.currentOrigin, target->r.currentOrigin) > PZK_CHALLENGE_RANGE)
	{
		// Somebody in the crosshair, but too far away to sit down with
		const char* name = target->s.number < MAX_CLIENTS ? target->client->pers.netname
			: target->fullName && target->fullName[0] ? target->fullName : "He";
		trap->SendServerCommand(ent->s.number, va("cp \"%s^7 is too far away.\nYou must be closer to challenge someone to Pazaak.\"", name));
		return;
	}
	if (target && target->client && target->s.number < MAX_CLIENTS && !(target->r.svFlags & SVF_BOT))
	{
		// 3. Another player: challenge him, he answers with his own Play Pazaak key
		if (!Pzk_CanPlay(target, qfalse) || G_Pazaak_IsPlaying(target->s.number) || Pzk_BusyReason(target))
		{
			Pzk_Print(ent->s.number, va("^5Pazaak:^7 %s^7 is busy and cannot respond. Try later.", target->client->pers.netname));
			return;
		}
		if (Pzk_HasChallenge(target->s.number) && pzkChallenger[target->s.number] != ent->s.number + 1)
		{
			Pzk_Print(ent->s.number, va("^5Pazaak:^7 %s^7 has already been challenged, ask again later.", target->client->pers.netname));
			return;
		}
		pzkChallenger[target->s.number] = ent->s.number + 1;
		pzkChallengeTime[target->s.number] = level.time;
		Pzk_Print(ent->s.number, va("^5Pazaak:^7 You have challenged %s^7 to a Pazaak game.", target->client->pers.netname));
		trap->SendServerCommand(ent->s.number, va("cp \"You have challenged %s^7 to a Pazaak game.\nWaiting for an answer...\"", target->client->pers.netname));
		trap->SendServerCommand(target->s.number, va("cp \"You have been challenged to a Pazaak game by %s^7\nPress your Play Pazaak key to accept.\"", ent->client->pers.netname));
		Pzk_Print(target->s.number, va("^5Pazaak:^7 You have been challenged to a Pazaak game by %s^7. Press your Play Pazaak key to accept, or type ^3/pazaak decline^7.", ent->client->pers.netname));
		return;
	}
	if (target && target->client && target->s.number < MAX_CLIENTS)
	{
		// 2. A bot: he accepts and sits down with you, the AI plays with his name
		if (!Pzk_CanPlay(target, qfalse) || G_Pazaak_IsPlaying(target->s.number) || Pzk_BotBusy(target)
			|| Pzk_BotFighting(target, ent))
		{
			Pzk_Print(ent->s.number, va("^5Pazaak:^7 %s^7 is busy and cannot respond. Try later.", target->client->pers.netname));
			return;
		}
		Pzk_Print(ent->s.number, va("^5Pazaak:^7 You have challenged %s^7 to a Pazaak game. %s^7 accepts.", target->client->pers.netname, target->client->pers.netname));
		trap->SendServerCommand(ent->s.number, va("cp \"You have challenged %s^7 to a Pazaak game.\n%s^7 accepts.\"", target->client->pers.netname, target->client->pers.netname));
		Pzk_StartMatch(ent, NULL, target->client->pers.netname, target);
		return;
	}
	if (target && target->client)
	{
		// An NPC lends his name to the AI
		if (target->fullName && target->fullName[0])
		{
			aiName = target->fullName;
		}
		else if (target->NPC_type && target->NPC_type[0])
		{
			aiName = target->NPC_type;
		}
	}
	// 1. Nobody (or an NPC): the AI
	Pzk_StartMatch(ent, NULL, aiName, NULL);
}

static void Pzk_Answer(gentity_t* ent, const qboolean accept)
{
	const int challenger = pzkChallenger[ent->s.number] - 1;
	gentity_t* other = NULL;
	const char* busy = NULL;

	if (!Pzk_HasChallenge(ent->s.number) || challenger < 0)
	{
		pzkChallenger[ent->s.number] = 0;
		Pzk_Print(ent->s.number, "^5Pazaak:^7 nobody challenged you.");
		return;
	}
	pzkChallenger[ent->s.number] = 0;
	other = &g_entities[challenger];

	if (!accept)
	{
		Pzk_Print(ent->s.number, "^5Pazaak:^7 challenge declined.");
		if (other->inuse && other->client)
		{
			Pzk_Print(challenger, va("^5Pazaak:^7 %s^7 declined your challenge.", ent->client->pers.netname));
			trap->SendServerCommand(challenger, va("cp \"%s^7 declined your Pazaak challenge.\"", ent->client->pers.netname));
		}
		return;
	}
	if (!Pzk_CanPlay(ent, qtrue) || G_Pazaak_IsPlaying(ent->s.number))
	{
		return;
	}
	busy = Pzk_BusyReason(ent);
	if (busy)
	{
		Pzk_Unavailable(ent->s.number, busy);
		return;
	}
	if (!Pzk_CanPlay(other, qfalse) || G_Pazaak_IsPlaying(challenger) || Pzk_BusyReason(other))
	{
		Pzk_Print(ent->s.number, "^5Pazaak:^7 your challenger can't play now.");
		return;
	}
	if (Distance(ent->r.currentOrigin, other->r.currentOrigin) > PZK_ACCEPT_RANGE)
	{
		// They moved apart since the challenge
		trap->SendServerCommand(ent->s.number, va("cp \"%s^7 is too far away.\nYou must be closer to play Pazaak.\"", other->client->pers.netname));
		trap->SendServerCommand(challenger, va("cp \"%s^7 is too far away.\nYou must be closer to play Pazaak.\"", ent->client->pers.netname));
		return;
	}
	Pzk_Print(challenger, va("^5Pazaak:^7 %s^7 accepts your challenge.", ent->client->pers.netname));
	Pzk_StartMatch(other, ent, NULL, NULL);
}

// Returns qtrue if the command was ours
qboolean G_Pazaak_ClientCommand(gentity_t* ent, const char* cmd)
{
	if (Shop_ClientCommand(ent, cmd))
	{
		return qtrue; // shop, shopbuy, shoptalk, shopclosed, shopcredits
	}
	if (!Q_stricmp(cmd, "pazaak"))
	{
		char arg[MAX_TOKEN_CHARS];
		trap->Argv(1, arg, sizeof(arg));
		if (!Q_stricmp(arg, "accept"))
		{
			Pzk_Answer(ent, qtrue);
		}
		else if (!Q_stricmp(arg, "decline"))
		{
			Pzk_Answer(ent, qfalse);
		}
		else
		{
			Pzk_Challenge(ent);
		}
		return qtrue;
	}
	if (!Q_stricmp(cmd, "~pzk"))
	{
		static char args[16][64];
		const char* argv[16] = { 0 };
		const int clientNum = ent->s.number;
		int argc = 0, i = 0;

		if (!pzkClientGame[clientNum])
		{
			return qtrue;
		}
		argc = trap->Argc() - 1;
		if (argc > 16)
		{
			argc = 16;
		}
		for (i = 0; i < argc; i++)
		{
			trap->Argv(i + 1, args[i], sizeof(args[i]));
			argv[i] = args[i];
		}
		Pzk_Command(&pzkGames[pzkClientGame[clientNum] - 1], pzkClientPid[clientNum], argc, argv, level.time);
		return qtrue;
	}
	return qfalse;
}

void G_Pazaak_ClientDisconnect(const int clientNum)
{
	int i = 0;
	if (clientNum < 0 || clientNum >= MAX_CLIENTS)
	{
		return;
	}
	Shop_ClientGone(clientNum); // the shop: his open menu and talk question are forgotten
	pzkChallenger[clientNum] = 0;
	for (i = 0; i < MAX_CLIENTS; i++)
	{
		if (pzkChallenger[i] == clientNum + 1)
		{
			pzkChallenger[i] = 0;
		}
	}
	if (pzkBotGame[clientNum])
	{
		// The bot of a match left: the AI goes on without him
		pzkGameBot[pzkBotGame[clientNum] - 1] = 0;
		pzkBotGame[clientNum] = 0;
		pzkClientHadGod[clientNum] = 0;
	}
	if (pzkClientGame[clientNum])
	{
		pzkGame_t* g = &pzkGames[pzkClientGame[clientNum] - 1];
		const int pid = pzkClientPid[clientNum];
		g->hostData[pid - 1] = -1; // nothing more to send to him
		pzkClientGame[clientNum] = 0;
		pzkClientPid[clientNum] = 0;
		pzkClientHadGod[clientNum] = 0;
		if (pzkStartAt[g - pzkGames])
		{
			Pzk_Abort(g); // not started yet
			return;
		}
		Pzk_PlayerGone(g, pid, level.time);
	}
}

// Challenges nobody answered in time
static void Pzk_ExpireChallenges(void)
{
	int i = 0;
	for (i = 0; i < MAX_CLIENTS; i++)
	{
		const int challenger = pzkChallenger[i] - 1;
		if (challenger < 0 || Pzk_HasChallenge(i))
		{
			continue;
		}
		pzkChallenger[i] = 0;
		if (g_entities[challenger].inuse && g_entities[challenger].client && g_entities[i].inuse && g_entities[i].client)
		{
			Pzk_Print(challenger, va("^5Pazaak:^7 %s^7 did not answer your challenge.", g_entities[i].client->pers.netname));
		}
	}
}

void G_Pazaak_RunFrame(void)
{
	const qboolean cutscene = Pzk_InCutscene();
	int i = 0;

	Pzk_ExpireChallenges();

	for (i = 0; i < MAX_CLIENTS; i++)
	{
		pzkGame_t* g = &pzkGames[i];
		int pid = 0;
		if (!g->inUse)
		{
			continue;
		}
		if (cutscene)
		{
			// A cutscene started (coop): the match is called off, the players stand up and watch it
			for (pid = 1; pid <= 2; pid++)
			{
				const int clientNum = g->hostData[pid - 1];
				if (clientNum >= 0 && clientNum < MAX_CLIENTS)
				{
					Pzk_Unavailable(clientNum, "A cutscene is playing.");
				}
			}
			Pzk_Abort(g);
			continue;
		}
		if (pzkGameBot[i] && !Pzk_CanPlay(&g_entities[pzkGameBot[i] - 1], qfalse))
		{
			// The bot of the match went to spectator (he can't die meanwhile): the AI goes on without him
			Pzk_ReleaseBot(i);
		}
		if (pzkGameBot[i] && pzkBotSitAt[i])
		{
			// The bot accepted in the air: he sits down once he lands (or after a while anyway)
			gentity_t* bot = &g_entities[pzkGameBot[i] - 1];
			if (bot->client->ps.groundEntityNum != ENTITYNUM_NONE || level.time - pzkBotSitAt[i] > 3000)
			{
				pzkBotSitAt[i] = 0;
				Pzk_SitDown(bot, g->hostData[0] >= 0 ? &g_entities[g->hostData[0]] : NULL);
				if (pzkStartAt[i] && pzkStartAt[i] < level.time + Pzk_SitTime(bot))
				{
					pzkStartAt[i] = level.time + Pzk_SitTime(bot);
				}
			}
		}
		if (pzkStartAt[i])
		{
			// A player who died or left for spectator while sitting down: no match
			for (pid = 1; pid <= 2; pid++)
			{
				const int clientNum = g->hostData[pid - 1];
				if (clientNum >= 0 && clientNum < MAX_CLIENTS && !Pzk_CanPlay(&g_entities[clientNum], qfalse))
				{
					break;
				}
			}
			if (pid <= 2)
			{
				Pzk_Abort(g);
				continue;
			}
			// Sitting down: the match starts (and the boards open) when the time is up
			if (level.time >= pzkStartAt[i])
			{
				const char* err = NULL;
				pzkStartAt[i] = 0;
				err = Pzk_StartGame(g, level.time);
				if (err)
				{
					Pzk_Print(g->hostData[0], va("^5Pazaak:^7 could not start (%s).", err));
					g->inUse = 0;
					Pzk_HostFinish(g, 0);
				}
			}
			continue;
		}
		// A player who died or went to spectator leaves the match
		for (pid = 1; pid <= 2 && g->inUse; pid++)
		{
			const int clientNum = g->hostData[pid - 1];
			if (clientNum >= 0 && clientNum < MAX_CLIENTS && g->players[pid - 1].present && !Pzk_CanPlay(&g_entities[clientNum], qfalse))
			{
				trap->SendServerCommand(clientNum, "pzk stop"); // close his board, the core sends him nothing more
				Pzk_PlayerGone(g, pid, level.time);
			}
		}
		if (g->inUse)
		{
			Pzk_Frame(g, level.time);
		}
	}
}

void G_Pazaak_Shutdown(void)
{
	int i = 0;
	for (i = 0; i < MAX_CLIENTS; i++)
	{
		if (pzkGames[i].inUse)
		{
			Pzk_Abort(&pzkGames[i]);
		}
	}
}

/*
===========================================================================
The shop (2026-11-01, DH: the SJE shop, credits and talk menu with MovieDuels' own weapons; MD 9 SP + MP, SP twin in
code/game/g_pazaak.cpp)

Per player and per map (pers, ClientConnect clears it): credits (start g_shopStartCredits). Kills of enemies
(players, bots, NPCs; not team mates or allied NPCs) give g_shopKillCredits times the kills in a row (each within
g_shopStreakTime ms, up to g_shopStreakMax), double for a saber user; dying costs g_shopDeathLoss percent. Bots don't
shop.

What is sold: MovieDuels' own weapons, listed in ext_data/md_shop.dat (the UI reads the same file; the item number is
the block's place in the file). A weapon comes with half its ammo; explosives are a pack of 3; an ammo refill; a medpac.

The menu runs in the UI (ui_pazaak.c shop code). The server talks to it through the "pzk" server command that the
cgame passes on as "uipzk": "pzk shop <open|state> <credits> <owned bits> <in hand bits> <ammo price> <medpac price>
"<message>"" and "pzk talk "<question>" <pazaak allowed>". The UI answers with the client commands shopbuy / shoptalk.
Talking: using a friendly NPC asks "Challenge to pazaak" / "Buy items" / "Leave" (g_npcTalk 0 = the old use).
===========================================================================
*/

#define SHOP_MAX_CREDITS	9999999
#define SHOP_PACK			3		// explosives in a pack
#define SHOP_FILE			"ext_data/md_shop.dat"
#define SHOP_FILE_MAX		32768
#define SHOP_MAX_ITEMS		255		// the UI gets owned / in hand as 256 bits
#define SHOP_ITEM_AMMO		1000
#define SHOP_ITEM_BACTA		1001
#define SHOP_CAT_EXPLOSIVES	3

// What ext_data/md_shop.dat sells (item number = place in the file, from 1)
typedef struct
{
	int weapon; // WP_NONE: not sold in MP (an SP-only weapon, or an unknown name)
	int price;
	int cat;
	char name[64];
} shopItem_t;

static shopItem_t shopItems[SHOP_MAX_ITEMS + 1];
static int shopNumItems = -1; // -1 = the file was not read yet
static char shopFileBuf[SHOP_FILE_MAX + 1];

static vmCvar_t g_shop, g_shopStartCredits, g_shopKillCredits, g_shopStreakTime, g_shopStreakMax, g_shopDeathLoss,
g_shopAmmoPrice, g_shopBactaPrice, g_npcTalk;
static int shopTalkNPC[MAX_CLIENTS];    // entity number + 1 of the NPC of the open talk question
static int shopIsOpen[MAX_CLIENTS];     // the shop menu is open (it closes when he is attacked)
static int shopOpenDamage[MAX_CLIENTS]; // ps.damageEvent when the shop opened (health was used, but MP health above max ticks down every second)
static int shopNextCheck[MAX_CLIENTS];

static void G_Shop_Init(void)
{
	trap->Cvar_Register(&g_shop, "g_shop", "1", CVAR_ARCHIVE);
	trap->Cvar_Register(&g_shopStartCredits, "g_shopStartCredits", "1000", CVAR_ARCHIVE);
	trap->Cvar_Register(&g_shopKillCredits, "g_shopKillCredits", "25", CVAR_ARCHIVE);
	trap->Cvar_Register(&g_shopStreakTime, "g_shopStreakTime", "5000", CVAR_ARCHIVE);
	trap->Cvar_Register(&g_shopStreakMax, "g_shopStreakMax", "5", CVAR_ARCHIVE);
	trap->Cvar_Register(&g_shopDeathLoss, "g_shopDeathLoss", "10", CVAR_ARCHIVE);
	trap->Cvar_Register(&g_shopAmmoPrice, "g_shopAmmoPrice", "150", CVAR_ARCHIVE);
	trap->Cvar_Register(&g_shopBactaPrice, "g_shopBactaPrice", "200", CVAR_ARCHIVE);
	trap->Cvar_Register(&g_npcTalk, "g_npcTalk", "1", CVAR_ARCHIVE);
	memset(shopTalkNPC, 0, sizeof(shopTalkNPC));
	memset(shopIsOpen, 0, sizeof(shopIsOpen));
	memset(shopOpenDamage, 0, sizeof(shopOpenDamage));
	memset(shopNextCheck, 0, sizeof(shopNextCheck));
	shopNumItems = -1; // read again on this map (the file may have changed)
}

// A client left: his open menu and talk question are forgotten (a bot may get this slot)
static void Shop_ClientGone(const int clientNum)
{
	if (clientNum < 0 || clientNum >= MAX_CLIENTS)
	{
		return;
	}
	shopIsOpen[clientNum] = 0;
	shopTalkNPC[clientNum] = 0;
}

static int Shop_Cvar(vmCvar_t* cv)
{
	trap->Cvar_Update(cv);
	return cv->integer;
}

static qboolean Shop_IsHuman(const gentity_t* ent)
{
	return ent && ent->client && ent->s.number < MAX_CLIENTS && !(ent->r.svFlags & SVF_BOT) ? qtrue : qfalse;
}

// The MP weapon of a md_shop.dat weapon name (WP_NONE: not in MP). The SP blaster pistol is MP's WP_BRYAR_PISTOL.
static int Shop_WeaponByName(const char* name)
{
	static const struct
	{
		const char* name;
		int weapon;
	} table[] = {
		{ "WP_BLASTER_PISTOL", WP_BRYAR_PISTOL },
		{ "WP_CLONEPISTOL", WP_CLONEPISTOL }, { "WP_REY", WP_REY }, { "WP_JANGO", WP_JANGO },
		{ "WP_BLASTER", WP_BLASTER }, { "WP_BATTLEDROID", WP_BATTLEDROID }, { "WP_REBELBLASTER", WP_REBELBLASTER },
		{ "WP_CLONECARBINE", WP_CLONECARBINE }, { "WP_CLONERIFLE", WP_CLONERIFLE }, { "WP_REBELRIFLE", WP_REBELRIFLE },
		{ "WP_THEFIRSTORDER", WP_THEFIRSTORDER }, { "WP_BOBA", WP_BOBA }, { "WP_CLONECOMMANDO", WP_CLONECOMMANDO },
		{ "WP_BOWCASTER", WP_BOWCASTER }, { "WP_DISRUPTOR", WP_DISRUPTOR }, { "WP_TUSKEN_RIFLE", WP_TUSKEN_RIFLE },
		{ "WP_REPEATER", WP_REPEATER }, { "WP_Z6_ROTARY_CANNON", WP_Z6_ROTARY_CANNON }, { "WP_DEMP2", WP_DEMP2 },
		{ "WP_FLECHETTE", WP_FLECHETTE }, { "WP_CONCUSSION", WP_CONCUSSION }, { "WP_ROCKET_LAUNCHER", WP_ROCKET_LAUNCHER },
		{ "WP_THERMAL", WP_THERMAL }, { "WP_TRIP_MINE", WP_TRIP_MINE }, { "WP_DET_PACK", WP_DET_PACK },
		{ "WP_STUN_BATON", WP_STUN_BATON }, { "WP_NOGHRI_STICK", WP_NOGHRI_STICK }, { "WP_TUSKEN_STAFF", WP_TUSKEN_STAFF },
	};
	int i;
	for (i = 0; i < (int)ARRAY_LEN(table); i++)
	{
		if (!Q_stricmp(name, table[i].name))
		{
			return table[i].weapon;
		}
	}
	return WP_NONE;
}

// The tab of a md_shop.dat "cat" (the UI has the same list)
static int Shop_CatByName(const char* name)
{
	static const char* cats[] = { "pistols", "rifles", "heavy", "explosives", "melee" };
	int i;
	for (i = 0; i < (int)ARRAY_LEN(cats); i++)
	{
		if (!Q_stricmp(name, cats[i]))
		{
			return i;
		}
	}
	return 1;
}

// A small tokenizer for md_shop.dat: words, "quoted strings", { }, // and /* */ comments (as the UI's)
static const char* Shop_Token(const char** p, char* out, const int size)
{
	const char* s = *p;
	int n = 0;

	out[0] = 0;
	for (;;)
	{
		while (*s && (unsigned char)*s <= ' ')
		{
			s++;
		}
		if (s[0] == '/' && s[1] == '/')
		{
			while (*s && *s != '\n')
			{
				s++;
			}
			continue;
		}
		if (s[0] == '/' && s[1] == '*')
		{
			s += 2;
			while (*s && !(s[0] == '*' && s[1] == '/'))
			{
				s++;
			}
			if (*s)
			{
				s += 2;
			}
			continue;
		}
		break;
	}
	if (!*s)
	{
		*p = s;
		return NULL;
	}
	if (*s == '"')
	{
		s++;
		while (*s && *s != '"')
		{
			if (n < size - 1)
			{
				out[n++] = *s;
			}
			s++;
		}
		if (*s)
		{
			s++;
		}
	}
	else
	{
		while (*s && (unsigned char)*s > ' ')
		{
			if (n < size - 1)
			{
				out[n++] = *s;
			}
			s++;
		}
	}
	out[n] = 0;
	*p = s;
	return out;
}

static void Shop_LoadItems(void)
{
	fileHandle_t f = 0;
	int len;
	const char* p;
	char tok[MAX_QPATH], key[64];

	if (shopNumItems >= 0)
	{
		return;
	}
	shopNumItems = 0;
	memset(shopItems, 0, sizeof(shopItems));
	len = trap->FS_Open(SHOP_FILE, &f, FS_READ);
	if (len <= 0 || !f)
	{
		if (f)
		{
			trap->FS_Close(f);
		}
		trap->Print(S_COLOR_YELLOW "Shop: %s not found, the shop only sells supplies\n", SHOP_FILE);
		return;
	}
	if (len > SHOP_FILE_MAX)
	{
		len = SHOP_FILE_MAX;
	}
	trap->FS_Read(shopFileBuf, len, f);
	trap->FS_Close(f);
	shopFileBuf[len] = 0;

	p = shopFileBuf;
	while (Shop_Token(&p, tok, sizeof(tok)) && shopNumItems < SHOP_MAX_ITEMS)
	{
		shopItem_t* it;
		qboolean spOnly = qfalse;

		// <anything> { key value ... }: every block is an item, numbered from 1 in file order (as the UI does)
		if (!Shop_Token(&p, tok, sizeof(tok)) || Q_stricmp(tok, "{"))
		{
			break;
		}
		it = &shopItems[++shopNumItems];
		it->price = 300;
		it->cat = 1;
		Q_strncpyz(it->name, "?", sizeof(it->name));
		while (Shop_Token(&p, key, sizeof(key)) && Q_stricmp(key, "}"))
		{
			if (!Shop_Token(&p, tok, sizeof(tok)))
			{
				break;
			}
			if (!Q_stricmp(key, "name"))
			{
				Q_strncpyz(it->name, tok, sizeof(it->name));
			}
			else if (!Q_stricmp(key, "weapon"))
			{
				it->weapon = Shop_WeaponByName(tok);
			}
			else if (!Q_stricmp(key, "cat"))
			{
				it->cat = Shop_CatByName(tok);
			}
			else if (!Q_stricmp(key, "price"))
			{
				const int v = atoi(tok);
				it->price = v < 0 ? 0 : v > 100000 ? 100000 : v;
			}
			else if (!Q_stricmp(key, "only"))
			{
				spOnly = !Q_stricmp(tok, "sp") ? qtrue : qfalse;
			}
		}
		if (spOnly || it->weapon <= WP_NONE || it->weapon >= WP_NUM_WEAPONS || it->weapon == WP_SABER)
		{
			it->weapon = WP_NONE; // keeps its number, never sold here
		}
	}
}

static void Shop_AddCredits(gentity_t* ent, const int n)
{
	int* c = &ent->client->pers.shopCredits;
	*c += n;
	if (*c < 0)
	{
		*c = 0;
	}
	else if (*c > SHOP_MAX_CREDITS)
	{
		*c = SHOP_MAX_CREDITS;
	}
}

// g_active.c ClientEndFrame, every frame of a human player: the starting credits, the open menu's attack check
void G_Shop_PlayerThink(gentity_t* ent)
{
	gclient_t* cl;
	int n;

	if (!ent || !ent->client || ent->s.number >= MAX_CLIENTS)
	{
		return;
	}
	cl = ent->client;
	n = ent->s.number;
	if (!cl->pers.shopInit)
	{
		cl->pers.shopInit = 1;
		cl->pers.shopCredits = Shop_Cvar(&g_shopStartCredits) > 0 ? g_shopStartCredits.integer : 0;
		cl->pers.shopStreak = 0;
		cl->pers.shopStreakTime = 0;
	}

	// the shop menu is open: it closes when he gets hurt or an enemy NPC comes for him
	if (shopIsOpen[n] && level.time >= shopNextCheck[n])
	{
		shopNextCheck[n] = level.time + 500;
		if (ent->health <= 0 || ent->client->ps.damageEvent != shopOpenDamage[n] || Pzk_EnemyNear(ent))
		{
			shopIsOpen[n] = 0;
			trap->SendServerCommand(n, "pzk shop close");
			trap->SendServerCommand(n, "cp \"The shop closed: you are under attack.\"");
		}
		shopOpenDamage[n] = ent->client->ps.damageEvent;
	}
}

// g_combat.c player_die: credits for a player's kills, the loss when a player dies
void G_Shop_Died(gentity_t* victim, gentity_t* attacker)
{
	gclient_t* cl;
	int max, streak, reward;

	if (!Shop_Cvar(&g_shop) || !victim || !victim->client)
	{
		return;
	}
	if (Shop_IsHuman(victim))
	{
		const int loss = victim->client->pers.shopCredits * (Shop_Cvar(&g_shopDeathLoss) > 0 ? g_shopDeathLoss.integer : 0) / 100;
		victim->client->pers.shopStreak = 0;
		if (loss > 0)
		{
			Shop_AddCredits(victim, -loss);
			Pzk_Print(victim->s.number, va("^1-%i credits^7 (killed)", loss));
		}
	}
	if (!Shop_IsHuman(attacker) || attacker == victim || victim->s.NPC_class == CLASS_VEHICLE
		|| Shop_Cvar(&g_shopKillCredits) <= 0)
	{
		return;
	}
	if (OnSameTeam(attacker, victim))
	{
		return; // team mates earn nothing
	}
	if (victim->s.eType == ET_NPC && (victim->client->playerTeam == attacker->client->playerTeam
		|| victim->client->playerTeam == NPCTEAM_NEUTRAL) && victim->enemy != attacker)
	{
		return; // allied NPCs and bystanders neither
	}

	cl = attacker->client;
	max = Shop_Cvar(&g_shopStreakMax) > 1 ? g_shopStreakMax.integer : 1;
	if (cl->pers.shopStreak > 0 && level.time - cl->pers.shopStreakTime <= Shop_Cvar(&g_shopStreakTime)
		&& level.time >= cl->pers.shopStreakTime)
	{
		cl->pers.shopStreak++;
	}
	else
	{
		cl->pers.shopStreak = 1;
	}
	cl->pers.shopStreakTime = level.time;

	streak = cl->pers.shopStreak < max ? cl->pers.shopStreak : max;
	reward = g_shopKillCredits.integer * streak;
	if (BG_HasWeapon(victim->client->ps.stats, WP_SABER))
	{
		reward *= 2; // a saber user
	}
	Shop_AddCredits(attacker, reward);
	if (cl->pers.shopStreak > 1)
	{
		Pzk_Print(attacker->s.number, va("^3+%i credits^7 (%i kills in a row)", reward, cl->pers.shopStreak));
	}
	else
	{
		Pzk_Print(attacker->s.number, va("^3+%i credits", reward));
	}
}

// Why this player can't shop now (NULL: he can)
static const char* Shop_BusyReason(gentity_t* ent)
{
	const char* busy;

	if (!Pzk_CanPlay(ent, qfalse))
	{
		return "";
	}
	if (G_Pazaak_IsPlaying(ent->s.number))
	{
		return "You are playing pazaak.";
	}
	busy = Pzk_BusyReason(ent);
	return busy == pzkNoGround ? NULL : busy; // in the air is fine for shopping
}

static qboolean Shop_HasWeapon(const gentity_t* ent, const int weapon)
{
	return weapon > WP_NONE && weapon < WP_NUM_WEAPONS && BG_HasWeapon(ent->client->ps.stats, weapon) ? qtrue : qfalse;
}

// What the UI shows: "pzk shop <what> <credits> <owned bits> <in hand bits> <ammo price> <medpac price> "<message>""
// (64 hex digits each: bit n = item n of md_shop.dat)
static void Shop_Send(const gentity_t* ent, const char* what, const char* message)
{
	const gclient_t* cl = ent->client;
	unsigned int owned[8], inHand[8];
	char ownedHex[8 * 8 + 1], handHex[8 * 8 + 1];
	char msg[128];
	char* q;
	int i;

	Shop_LoadItems();
	memset(owned, 0, sizeof(owned));
	memset(inHand, 0, sizeof(inHand));
	for (i = 1; i <= shopNumItems && i <= SHOP_MAX_ITEMS; i++)
	{
		const int w = shopItems[i].weapon;
		if (Shop_HasWeapon(ent, w))
		{
			owned[i >> 5] |= 1u << (i & 31);
			if (cl->ps.weapon == w)
			{
				inHand[i >> 5] |= 1u << (i & 31);
			}
		}
	}
	ownedHex[0] = handHex[0] = 0;
	for (i = 0; i < 8; i++)
	{
		Q_strcat(ownedHex, sizeof(ownedHex), va("%08x", owned[i]));
		Q_strcat(handHex, sizeof(handHex), va("%08x", inHand[i]));
	}
	Q_strncpyz(msg, message ? message : "", sizeof(msg));
	for (q = msg; *q; q++)
	{
		if (*q == '"')
		{
			*q = '\'';
		}
	}
	trap->SendServerCommand(ent->s.number, va("pzk shop %s %i %s %s %i %i \"%s\"", what, cl->pers.shopCredits, ownedHex, handHex,
		Shop_Cvar(&g_shopAmmoPrice) > 0 ? g_shopAmmoPrice.integer : 0, Shop_Cvar(&g_shopBactaPrice) > 0 ? g_shopBactaPrice.integer : 0, msg));
}

static void Shop_Open(gentity_t* ent, const char* message)
{
	const char* busy;

	if (!Shop_Cvar(&g_shop))
	{
		Pzk_Print(ent->s.number, "The shop is switched off (g_shop 0).");
		return;
	}
	busy = Shop_BusyReason(ent);
	if (busy)
	{
		trap->SendServerCommand(ent->s.number, va("cp \"The shop is unavailable at this time.\n%s\"", busy));
		return;
	}
	Shop_Send(ent, "open", message);
	shopIsOpen[ent->s.number] = 1;
	shopOpenDamage[ent->s.number] = ent->client->ps.damageEvent;
	shopNextCheck[ent->s.number] = level.time + 500;
}

// The ammo of every gun he carries, to the full (qfalse: nothing to fill)
static qboolean Shop_FillAmmo(gentity_t* ent)
{
	qboolean filled = qfalse;
	playerState_t* ps = &ent->client->ps;
	int w;

	for (w = WP_NONE + 1; w < WP_NUM_WEAPONS; w++)
	{
		int a;
		if (w == WP_SABER || w == WP_MELEE || w == WP_STUN_BATON || !BG_HasWeapon(ps->stats, w))
		{
			continue;
		}
		a = weaponData[w].ammoIndex;
		if (a <= AMMO_NONE || a >= AMMO_MAX || a == AMMO_FORCE)
		{
			continue;
		}
		if (ps->ammo[a] < ammoData[a].max)
		{
			ps->ammo[a] = ammoData[a].max;
			filled = qtrue;
		}
	}
	return filled;
}

static void Shop_Buy(gentity_t* ent, const int item)
{
	gclient_t* cl = ent->client;
	const shopItem_t* it;
	int weapon, price;

	if (!Shop_Cvar(&g_shop) || Shop_BusyReason(ent))
	{
		return;
	}
	if (item == SHOP_ITEM_AMMO)
	{
		price = Shop_Cvar(&g_shopAmmoPrice) > 0 ? g_shopAmmoPrice.integer : 0;
		if (cl->pers.shopCredits < price)
		{
			Shop_Send(ent, "state", va("You need %i credits for an ammo refill.", price));
			return;
		}
		if (!Shop_FillAmmo(ent))
		{
			Shop_Send(ent, "state", "Your ammo is full.");
			return;
		}
		Shop_AddCredits(ent, -price);
		Shop_Send(ent, "state", "Ammo refilled.");
		return;
	}
	if (item == SHOP_ITEM_BACTA)
	{
		price = Shop_Cvar(&g_shopBactaPrice) > 0 ? g_shopBactaPrice.integer : 0;
		if (cl->pers.shopCredits < price)
		{
			Shop_Send(ent, "state", va("You need %i credits for a medpac.", price));
			return;
		}
		if (cl->ps.stats[STAT_HOLDABLE_ITEMS] & 1 << HI_MEDPAC)
		{
			Shop_Send(ent, "state", "You already carry a medpac.");
			return;
		}
		cl->ps.stats[STAT_HOLDABLE_ITEMS] |= 1 << HI_MEDPAC;
		if (!cl->ps.stats[STAT_HOLDABLE_ITEM])
		{
			cl->ps.stats[STAT_HOLDABLE_ITEM] = BG_GetItemIndexByTag(HI_MEDPAC, IT_HOLDABLE);
		}
		Shop_AddCredits(ent, -price);
		Shop_Send(ent, "state", "Bought a medpac.");
		return;
	}
	Shop_LoadItems();
	if (item <= 0 || item > shopNumItems || item > SHOP_MAX_ITEMS)
	{
		return;
	}

	it = &shopItems[item];
	weapon = it->weapon;
	price = it->price;
	if (weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS || weapon == WP_SABER)
	{
		return;
	}
	if (it->cat == SHOP_CAT_EXPLOSIVES)
	{
		// a pack of explosives
		const int a = weaponData[weapon].ammoIndex;
		if (cl->pers.shopCredits < price)
		{
			Shop_Send(ent, "state", va("You need %i credits for %s.", price, it->name));
			return;
		}
		if (Shop_HasWeapon(ent, weapon) && a > AMMO_NONE && a < AMMO_MAX && cl->ps.ammo[a] >= ammoData[a].max)
		{
			Shop_Send(ent, "state", va("You can't carry more %s.", it->name));
			return;
		}
		Shop_AddCredits(ent, -price);
		BG_GiveWeapon(cl->ps.stats, weapon);
		G_AddEvent(ent, EV_WEAPINVCHANGE, cl->ps.stats[STAT_WEAPONS]);
		if (a > AMMO_NONE && a < AMMO_MAX)
		{
			Add_Ammo(ent, a, SHOP_PACK);
		}
		Shop_Send(ent, "state", va("Bought %i x %s.", SHOP_PACK, it->name));
		return;
	}
	if (Shop_HasWeapon(ent, weapon))
	{
		// he has it already (pick it on the weapon wheel)
		Shop_Send(ent, "state", va("You already have the %s.", it->name));
		return;
	}
	if (cl->pers.shopCredits < price)
	{
		Shop_Send(ent, "state", va("You need %i credits for the %s.", price, it->name));
		return;
	}
	Shop_AddCredits(ent, -price);
	BG_GiveWeapon(cl->ps.stats, weapon);
	G_AddEvent(ent, EV_WEAPINVCHANGE, cl->ps.stats[STAT_WEAPONS]);
	{
		const int a = weaponData[weapon].ammoIndex;
		if (a > AMMO_NONE && a < AMMO_MAX && a != AMMO_FORCE && cl->ps.ammo[a] < ammoData[a].max / 2)
		{
			cl->ps.ammo[a] = ammoData[a].max / 2;
		}
	}
	Shop_Send(ent, "state", va("Bought the %s.", it->name));
}

// Classes that don't talk (droids, animals, monsters, vehicles)
static qboolean Shop_SilentClass(const int c)
{
	switch (c)
	{
	case CLASS_VEHICLE:
	case CLASS_GONK:
	case CLASS_MOUSE:
	case CLASS_R2D2:
	case CLASS_R5D2:
	case CLASS_PROBE:
	case CLASS_SEEKER:
	case CLASS_REMOTE:
	case CLASS_SENTRY:
	case CLASS_INTERROGATOR:
	case CLASS_MARK1:
	case CLASS_MARK2:
	case CLASS_ATST:
	case CLASS_RANCOR:
	case CLASS_WAMPA:
	case CLASS_SAND_CREATURE:
	case CLASS_HOWLER:
	case CLASS_MINEMONSTER:
	case CLASS_SWAMP:
		return qtrue;
	default:
		return qfalse;
	}
}

// g_utils.c TryUse: a friendly NPC asks "challenge to pazaak / buy items / leave" (qtrue: asked)
qboolean G_Shop_TryTalk(gentity_t* player, const gentity_t* npc)
{
	char who[64];
	char* q;

	if (!Shop_Cvar(&g_npcTalk) || !Shop_IsHuman(player) || !npc || npc->s.eType != ET_NPC || !npc->NPC || !npc->client
		|| npc->health <= 0 || npc->client->ps.pm_type == PM_DEAD || Pzk_InCutscene())
	{
		return qfalse;
	}
	if (npc->client->playerTeam != player->client->playerTeam && npc->client->playerTeam != NPCTEAM_NEUTRAL)
	{
		return qfalse;
	}
	if (npc->NPC->scriptFlags & SCF_NO_RESPONSE || npc->enemy && npc->enemy->health > 0 || Shop_SilentClass(npc->client->NPC_class)
		|| G_Pazaak_IsPlaying(player->s.number))
	{
		return qfalse;
	}
	Q_strncpyz(who, npc->fullName && npc->fullName[0] ? npc->fullName : npc->NPC_type && npc->NPC_type[0] ? npc->NPC_type
		: "this character", sizeof(who));
	for (q = who; *q; q++)
	{
		if (*q == '"')
		{
			*q = '\'';
		}
	}
	shopTalkNPC[player->s.number] = npc->s.number + 1;
	trap->SendServerCommand(player->s.number, va("pzk talk \"You talk to %s^7. What do you want to do?\" 1", who));
	return qtrue;
}

// "shoptalk <1 = pazaak, 2 = shop, 0 = leave>"
static void Shop_TalkAnswer(gentity_t* ent, const int answer)
{
	const int n = shopTalkNPC[ent->s.number] - 1;
	const gentity_t* npc;
	const char* busy;
	const char* name;

	shopTalkNPC[ent->s.number] = 0;
	if (n < MAX_CLIENTS || n >= ENTITYNUM_MAX_NORMAL)
	{
		return;
	}
	npc = &g_entities[n];
	if (!npc->inuse || !npc->client || npc->health <= 0)
	{
		return;
	}
	name = npc->fullName && npc->fullName[0] ? npc->fullName : npc->NPC_type && npc->NPC_type[0] ? npc->NPC_type : "AI";
	if (answer == 2)
	{
		Shop_Open(ent, va("%s^7 shows you the goods.", name));
		return;
	}
	if (answer != 1 || !Pzk_CanPlay(ent, qtrue))
	{
		return;
	}
	// challenge him: an NPC lends his name to the AI (Pzk_StartMatch)
	if (G_Pazaak_IsPlaying(ent->s.number))
	{
		Pzk_Print(ent->s.number, "^5Pazaak:^7 you are already playing.");
		return;
	}
	busy = Pzk_BusyReason(ent);
	if (busy)
	{
		Pzk_Unavailable(ent->s.number, busy);
		return;
	}
	Pzk_StartMatch(ent, NULL, name, NULL);
}

// The shop's commands (G_Pazaak_ClientCommand passes them on): shop, shopbuy, shoptalk, shopclosed, shopcredits (cheat)
static qboolean Shop_ClientCommand(gentity_t* ent, const char* cmd)
{
	char arg[MAX_TOKEN_CHARS];

	if (!Shop_IsHuman(ent))
	{
		return qfalse;
	}
	trap->Argv(1, arg, sizeof(arg));
	if (!Q_stricmp(cmd, "shop"))
	{
		Shop_Open(ent, "");
		return qtrue;
	}
	if (!Q_stricmp(cmd, "shopbuy"))
	{
		Shop_Buy(ent, atoi(arg));
		return qtrue;
	}
	if (!Q_stricmp(cmd, "shoptalk"))
	{
		Shop_TalkAnswer(ent, atoi(arg));
		return qtrue;
	}
	if (!Q_stricmp(cmd, "shopclosed"))
	{
		shopIsOpen[ent->s.number] = 0; // the player closed the menu
		return qtrue;
	}
	if (!Q_stricmp(cmd, "shopcredits"))
	{
		// shopcredits [amount] (cheat): shows, or sets, the credits
		if (arg[0])
		{
			if (!trap->Cvar_VariableIntegerValue("sv_cheats"))
			{
				Pzk_Print(ent->s.number, "Cheats are not enabled on this server.");
				return qtrue;
			}
			ent->client->pers.shopCredits = 0;
			Shop_AddCredits(ent, atoi(arg));
		}
		Pzk_Print(ent->s.number, va("Credits: %i", ent->client->pers.shopCredits));
		return qtrue;
	}
	return qfalse;
}
