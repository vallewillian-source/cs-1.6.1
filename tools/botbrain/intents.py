"""The closed intent set the Jev model may choose from, plus the coach rules.

The set is intentionally small and map-specific: V1 is de_dust2, bomb defuse.
Every intent name here must exist in the DLL (see
``src/ReGameDLL_CS/regamedll/dlls/bot/advisor/cs_bot_advisor.h``); the plan file
carries the name verbatim.
"""

# --- offense: Terrorists -------------------------------------------------
T_CRITERIA = {
    "RUSH_B": "Push B together and plant fast - all-in on B.",
    "RUSH_A_LONG": "Push A through Long together.",
    "RUSH_A_SHORT": "Push A through Short / Catwalk together.",
    "SPLIT_A": "Split A: part of the team goes Long, part goes Short, converge together.",
    "HOLD_AFTER_PLANT": "The bomb is already planted: hold angles around the site and protect the defuse.",
    "ECO_SAVE": "Save this round: avoid fights and stay alive (the team is low on money).",
    "DEFAULT": "No team-wide call - let each bot play its normal role.",
}

T_GOAL = (
    "You are the Terrorist in-game leader of a Counter-Strike 1.6 team on de_dust2 "
    "(bomb defuse). Choose the plan for THIS round only."
)

T_RULES = "\n".join([
    "You command 4-5 Terrorist bots; return exactly one plan.",
    "buy_status says what the team can afford: 'eco' = no rifles/armor, 'force' = a cheap buy, 'full' = buy properly.",
    "On 'full', take a site: RUSH_B, RUSH_A_LONG or RUSH_A_SHORT (SPLIT_A is slower and more even).",
    "On 'force', a coordinated rush at one site is usually best.",
    "On 'eco', either save (ECO_SAVE) or make a coordinated rush to force a plant - choose by the score.",
    "Never choose ECO_SAVE when buy_status is 'full'.",
    "If the phase is postplant, choose HOLD_AFTER_PLANT.",
    "Do not keep hitting a site you have lost repeatedly (see recent_plans).",
    "When the situation is unclear, DEFAULT is the safe choice.",
])

# --- defense: Counter-Terrorists -----------------------------------------
CT_CRITERIA = {
    "STACK_A": "Stack the A site: most bots hold A and its entrances.",
    "STACK_B": "Stack the B site: most bots hold B and its entrances.",
    "RETAKE_A": "The bomb is down at A: retake the A site and defuse.",
    "RETAKE_B": "The bomb is down at B: retake the B site and defuse.",
    "ECO_SAVE": "Save this round: avoid fights and stay alive (the team is low on money).",
    "DEFAULT": "No team-wide call - let each bot play its normal role.",
}

CT_GOAL = (
    "You are the Counter-Terrorist in-game leader of a Counter-Strike 1.6 team on "
    "de_dust2 (bomb defuse). Choose the plan for THIS round only."
)

CT_RULES = "\n".join([
    "You command 4-5 Counter-Terrorist bots; return exactly one plan.",
    "buy_status says what the team can afford: 'eco' = no rifles/armor, 'force' = a cheap buy, 'full' = buy properly.",
    "If the phase is postplant, choose RETAKE_A or RETAKE_B to match bomb_zone.",
    "Otherwise stack the site the enemy has NOT been attacking recently (recent_plans is their side).",
    "On 'eco', either save (ECO_SAVE) or stack one site for a cheap hold - choose by the score.",
    "Never choose ECO_SAVE when buy_status is 'full'.",
    "When the situation is unclear, DEFAULT is the safe choice.",
])


def criteria_for(team: str) -> dict:
    return T_CRITERIA if team == "T" else CT_CRITERIA


def goal_for(team: str) -> str:
    return T_GOAL if team == "T" else CT_GOAL


def rules_for(team: str) -> str:
    return T_RULES if team == "T" else CT_RULES
