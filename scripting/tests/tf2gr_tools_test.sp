/**
 * Self-test for TF2: Gold Rush Tools + TF2Attributes (TF2: Gold Rush).
 * Compiled against this repo's include folder exactly like a stock-TF2 plugin
 * (#include <tf2>, <tf2_stocks>, <tf2attributes>). Run: sm_tf2grtools_test
 * (needs at least one living player or bot). Prints PASS/FAIL per check.
 */
#include <sourcemod>
#include <sdktools>
#include <tf2>
#include <tf2_stocks>
#include <tf2attributes>

#pragma semicolon 1
#pragma newdecls required

public Plugin myinfo = { name = "TF2 Gold Rush tools self-test", author = "AgitationSkeleton", description = "", version = "1.0", url = "" };

int g_iPass, g_iFail, g_iAdded, g_iRemoved, g_iClient;
int g_iReply;

public void OnPluginStart()
{
	RegAdminCmd("sm_tf2grtools_test", Cmd_Test, ADMFLAG_ROOT);
}

public void TF2_OnConditionAdded(int client, TFCond cond)
{
	if (client == g_iClient && cond == TFCond_CritCola)
		g_iAdded++;
}

public void TF2_OnConditionRemoved(int client, TFCond cond)
{
	if (client == g_iClient && cond == TFCond_CritCola)
		g_iRemoved++;
}

void Check(bool ok, const char[] fmt, any ...)
{
	char buf[256];
	VFormat(buf, sizeof(buf), fmt, 3);
	if (ok) g_iPass++; else g_iFail++;
	char line[300];
	FormatEx(line, sizeof(line), "%s  %s", ok ? "PASS" : "FAIL", buf);
	LogMessage("%s", line);
	int to = g_iReply ? GetClientOfUserId(g_iReply) : 0;
	if (to) PrintToConsole(to, "%s", line); else PrintToServer("%s", line);
}

public Action Cmd_Test(int client, int args)
{
	g_iReply = client ? GetClientUserId(client) : 0;
	g_iPass = g_iFail = g_iAdded = g_iRemoved = 0;
	g_iClient = 0;
	for (int i = 1; i <= MaxClients; i++)
		if (IsClientInGame(i) && IsPlayerAlive(i) && GetClientTeam(i) >= 2) { g_iClient = i; break; }
	if (!g_iClient)
	{
		ReplyToCommand(client, "no living player");
		return Plugin_Handled;
	}
	int c = g_iClient;
	Check(TF2GR_IsReady(), "TF2GR_IsReady");
	int res = TF2_GetResourceEntity();
	Check(res > MaxClients, "TF2_GetResourceEntity = %d", res);
	Check(TF2_GetClass("heavy") == TFClass_Heavy && TF2_GetClass("civilian") == TFClass_Civilian, "TF2_GetClass heavy/civilian");
	TF2_IsHolidayActive(TFHoliday_Birthday);
	Check(true, "TF2_IsHolidayActive returns without error");

	// Conditions (Crit-a-Cola mini-crits = 19).
	TF2_AddCondition(c, TFCond_CritCola, 4.0);
	Check(TF2_IsPlayerInCondition(c, TFCond_CritCola), "TF2_AddCondition -> in condition (stock netprop check)");
	float d = TF2GR_GetCondDuration(c, TFCond_CritCola);
	Check(d > 3.0 && d <= 4.0, "TF2GR_GetCondDuration = %.2f", d);
	TF2GR_SetCondDuration(c, TFCond_CritCola, 10.0);
	d = TF2GR_GetCondDuration(c, TFCond_CritCola);
	Check(d > 9.0, "TF2GR_SetCondDuration -> %.2f", d);
	TF2_RemoveCondition(c, TFCond_CritCola);
	Check(!TF2_IsPlayerInCondition(c, TFCond_CritCola), "TF2_RemoveCondition");

	TF2_StunPlayer(c, 1.5, 0.5, TF_STUNFLAG_SLOWDOWN, 0);
	Check(TF2_IsPlayerInCondition(c, TFCond_Dazed), "TF2_StunPlayer -> Dazed");
	TF2_IgnitePlayer(c, c, 3.0);
	Check(TF2_IsPlayerInCondition(c, TFCond_OnFire), "TF2_IgnitePlayer -> OnFire");
	TF2_MakeBleed(c, c, 2.0);
	Check(TF2_IsPlayerInCondition(c, TFCond_Bleeding), "TF2_MakeBleed -> Bleeding");
	TF2_SetPlayerPowerPlay(c, true);
	Check(TF2_IsPlayerInCondition(c, TFCond_Kritzkrieged) && TF2_IsPlayerInCondition(c, TFCond_Ubercharged), "TF2_SetPlayerPowerPlay(true)");
	TF2_SetPlayerPowerPlay(c, false);
	Check(!TF2_IsPlayerInCondition(c, TFCond_Ubercharged), "TF2_SetPlayerPowerPlay(false)");
	TF2_RemoveCondition(c, TFCond_OnFire);
	TF2_RemoveCondition(c, TFCond_Bleeding);

	// Regenerate restores health.
	int maxhp = GetEntProp(c, Prop_Data, "m_iMaxHealth");
	SetEntityHealth(c, 1);
	TF2_RegeneratePlayer(c);
	Check(GetClientHealth(c) >= maxhp - 1, "TF2_RegeneratePlayer -> health %d/%d", GetClientHealth(c), maxhp);

	// Attributes on the active weapon and on the player.
	int w = GetEntPropEnt(c, Prop_Send, "m_hActiveWeapon");
	Check(TF2Attrib_IsReady(), "TF2Attrib_IsReady (schema parsed)");
	Check(TF2Attrib_IsValidAttributeName("damage bonus") && !TF2Attrib_IsValidAttributeName("definitely not an attribute"), "TF2Attrib_IsValidAttributeName");
	if (w > MaxClients)
	{
		Check(TF2Attrib_SetByName(w, "damage bonus", 2.5), "TF2Attrib_SetByName(weapon)");
		Address a = TF2Attrib_GetByName(w, "damage bonus");
		Check(a != Address_Null, "TF2Attrib_GetByName(weapon) present");
		Check(FloatAbs(TF2Attrib_GetValue(a) - 2.5) < 0.01, "TF2Attrib_GetValue = %.2f", TF2Attrib_GetValue(a));
		TF2Attrib_SetValue(a, 1.75);
		Check(FloatAbs(TF2Attrib_GetValue(a) - 1.75) < 0.01, "TF2Attrib_SetValue -> %.2f", TF2Attrib_GetValue(a));
		int ids[8];
		int n = TF2Attrib_ListDefIndices(w, ids, sizeof(ids));
		Check(n >= 1 && ids[0] == TF2Attrib_GetDefIndex(a), "TF2Attrib_ListDefIndices = %d (def %d)", n, n ? ids[0] : -1);
		Check(TF2Attrib_RemoveByName(w, "damage bonus") && TF2Attrib_GetByName(w, "damage bonus") == Address_Null, "TF2Attrib_RemoveByName(weapon)");
		Check(TF2Attrib_SetByDefIndex(w, 2, 3.0) && TF2Attrib_GetByDefIndex(w, 2) != Address_Null, "TF2Attrib_SetByDefIndex(weapon, 2)");
		Check(TF2Attrib_RemoveAll(w) && TF2Attrib_GetByDefIndex(w, 2) == Address_Null, "TF2Attrib_RemoveAll(weapon)");
		Check(TF2Attrib_ClearCache(w), "TF2Attrib_ClearCache(weapon)");
		int def = GetEntProp(w, Prop_Send, "m_iItemDefinitionIndex");
		int sids[16];
		float svals[16];
		int sn = TF2Attrib_GetStaticAttribs(def, sids, svals, sizeof(sids));
		Check(sn >= 0, "TF2Attrib_GetStaticAttribs(def %d) = %d", def, sn);
	}
	Check(TF2Attrib_SetByName(c, "move speed bonus", 1.3), "TF2Attrib_SetByName(player)");
	Address pa = TF2Attrib_GetByName(c, "move speed bonus");
	Check(pa != Address_Null && FloatAbs(TF2Attrib_GetValue(pa) - 1.3) < 0.01, "player attribute readback = %.2f", pa != Address_Null ? TF2Attrib_GetValue(pa) : -1.0);
	TF2Attrib_RemoveByName(c, "move speed bonus");
	Check(TF2Attrib_GetByName(c, "move speed bonus") == Address_Null, "TF2Attrib_RemoveByName(player)");
	TF2Attrib_AddCustomPlayerAttribute(c, "max health additive bonus", 50.0);
	Check(GetEntProp(c, Prop_Data, "m_iMaxHealth") != maxhp || TF2Attrib_GetByName(c, "max health additive bonus") != Address_Null, "TF2Attrib_AddCustomPlayerAttribute (max health %d -> %d)", maxhp, GetEntProp(c, Prop_Data, "m_iMaxHealth"));
	TF2Attrib_RemoveCustomPlayerAttribute(c, "max health additive bonus");

	// Forward check: added is seen on the next frame, removed when it expires.
	// (Done last: Regenerate clears conditions in the same tick.)
	TF2_AddCondition(c, TFCond_CritCola, 0.5);
	CreateTimer(1.5, Timer_Forwards, GetClientUserId(c));
	return Plugin_Handled;
}

public Action Timer_Forwards(Handle timer, int userid)
{
	int c = GetClientOfUserId(userid);
	Check(g_iAdded >= 1, "TF2_OnConditionAdded fired (%d)", g_iAdded);
	Check(g_iRemoved >= 1, "TF2_OnConditionRemoved fired (%d)", g_iRemoved);
	if (c)
	{
		TF2_RespawnPlayer(c);
		Check(IsPlayerAlive(c), "TF2_RespawnPlayer -> alive");
	}
	char line[96];
	FormatEx(line, sizeof(line), "RESULT: %d passed, %d failed", g_iPass, g_iFail);
	LogMessage("%s", line);
	int to = g_iReply ? GetClientOfUserId(g_iReply) : 0;
	if (to) PrintToConsole(to, "%s", line); else PrintToServer("%s", line);
	return Plugin_Stop;
}
