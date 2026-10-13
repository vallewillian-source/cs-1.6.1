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

#pragma once
#ifndef CS_BOT_ADVISOR_H
#define CS_BOT_ADVISOR_H

/*
 * cs_bot_advisor -- the "slow brain" bridge (V1, "bots algoritmo + Jev").
 *
 * The stock CCSBot is the muscle (nav, vision, aim, weapons, combat). This
 * module lets an external sidecar (tools/botbrain, talking to the TypeSafe
 * "Jev" model) steer the *team* by choosing one intent from the closed set
 * below. The exchange is a plain key=value mailbox on disk, so the game
 * thread never blocks and a missing/dead sidecar just leaves the stock AI
 * running.
 *
 *   cstrike/advisor/observe_<t|ct>.txt   (we write: the round facts)
 *   cstrike/advisor/plan_<t|ct>.txt      (sidecar writes: intent to apply)
 *
 * Off by default: advisor_enable 0 leaves the server byte-for-byte stock.
 */

class CCSBot;

// Closed set of intents. Names are the wire format in plan_*.txt.
enum AdvisorIntent
{
	ADVISOR_INTENT_NONE = 0,		// unset
	ADVISOR_INTENT_DEFAULT,			// no override: let the stock AI decide

	// offense (Terrorist)
	ADVISOR_INTENT_RUSH_B,
	ADVISOR_INTENT_RUSH_A_LONG,
	ADVISOR_INTENT_RUSH_A_SHORT,
	ADVISOR_INTENT_SPLIT_A,
	ADVISOR_INTENT_HOLD_AFTER_PLANT,

	// defense (Counter-Terrorist)
	ADVISOR_INTENT_STACK_A,
	ADVISOR_INTENT_STACK_B,
	ADVISOR_INTENT_RETAKE_A,
	ADVISOR_INTENT_RETAKE_B,

	// shared
	ADVISOR_INTENT_ECO_SAVE,

	ADVISOR_INTENT_COUNT
};

// Lifecycle / hook entry points.
void Advisor_RegisterCVars();					// from Bot_RegisterCVars()
void Advisor_OnRoundStart();					// from CCSBotManager::RestartRound()
void Advisor_FrameUpdate();						// from CCSBotManager::StartFrame()
void Advisor_ApplyIntent( CCSBot *bot );		// from CCSBot::Update(), pre state dispatch

const char *Advisor_IntentName( AdvisorIntent intent );
AdvisorIntent Advisor_IntentFromName( const char *name );

#endif // CS_BOT_ADVISOR_H
