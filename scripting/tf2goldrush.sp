/**
 * TF2 Gold Rush Tools
 *
 * SourceMod's TF2 Tools extension refuses to load on TF2: Gold Rush ("Cannot
 * Load TF2 Extension on mods other than TF2"). This plugin provides the same
 * natives and forwards under the same names (TF2_*), so plugins written for
 * stock TF2 run after a recompile against this project's include folder.
 *
 * How it works on TF2: Gold Rush (x64, SourceMod 1.13):
 *  - Player actions go through TF2: Gold Rush's VScript API (AddCondEx,
 *    StunPlayer, ForceRespawn, ...), run synchronously with the RunScriptCode
 *    input. Direct SDKCalls on CTFPlayerShared need entity-address arithmetic
 *    that crashes under SourceMod's x64 pseudo-addresses, and DHooks detours
 *    crash on this build, so neither is used.
 *  - Condition forwards poll the networked condition bits each frame, the way
 *    the stock extension does (no detours).
 *  - Values come back from VScript through a private convar.
 *
 * Not available on TF2: Gold Rush (forwards never fire, documented in the
 * include): TF2_CalcIsAttackCritical, TF2_OnPlayerTeleport, TF2_OnGetHoliday,
 * TF2_OnIsHolidayActive.
 */
#include <sourcemod>
#include <sdktools>
#include <clientprefs>
#include <sdkhooks>
#include <dhooks>

#pragma semicolon 1
#pragma newdecls required

#define PLUGIN_VERSION "1.4.2"
#define COND_WORDS 5

public Plugin myinfo =
{
	name        = "TF2 Gold Rush Tools",
	author      = "AgitationSkeleton",
	description = "TF2 Tools natives and forwards (TF2_*) for TF2: Gold Rush.",
	version     = PLUGIN_VERSION,
	url         = "https://github.com/AgitationSkeleton/SM-TF2GoldRush-Tools"
};

bool g_bReady;
int g_iCondBits[MAXPLAYERS + 1][COND_WORDS];
bool g_bTracked[MAXPLAYERS + 1];
ConVar g_cvDebug;
GlobalForward g_fwdCondAdded;
GlobalForward g_fwdCondRemoved;
GlobalForward g_fwdWaitStart;
GlobalForward g_fwdWaitEnd;

static const char g_sCondProps[COND_WORDS][] =
{
	"m_nPlayerCond", "m_nPlayerCondEx", "m_nPlayerCondEx2", "m_nPlayerCondEx3", "m_nPlayerCondEx4"
};
bool g_bCondProp[COND_WORDS];

static const char g_sClassNames[][] =
{
	"", "scout", "sniper", "soldier", "demoman", "medic", "heavyweapons", "pyro", "spy", "engineer", "civilian"
};

public APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int err_max)
{
	char game[32];
	GetGameFolderName(game, sizeof(game));
	if (!StrEqual(game, "tf_goldrush", false))
	{
		strcopy(error, err_max, "TF2 Gold Rush Tools only runs on TF2: Gold Rush (use SourceMod's TF2 Tools on TF2).");
		return APLRes_Failure;
	}
	CreateNative("TF2_IgnitePlayer", Native_IgnitePlayer);
	CreateNative("TF2_RespawnPlayer", Native_RespawnPlayer);
	CreateNative("TF2_RegeneratePlayer", Native_RegeneratePlayer);
	CreateNative("TF2_AddCondition", Native_AddCondition);
	CreateNative("TF2_RemoveCondition", Native_RemoveCondition);
	CreateNative("TF2_SetPlayerPowerPlay", Native_SetPlayerPowerPlay);
	CreateNative("TF2_DisguisePlayer", Native_DisguisePlayer);
	CreateNative("TF2_RemovePlayerDisguise", Native_RemovePlayerDisguise);
	CreateNative("TF2_StunPlayer", Native_StunPlayer);
	CreateNative("TF2_MakeBleed", Native_MakeBleed);
	CreateNative("TF2_GetResourceEntity", Native_GetResourceEntity);
	CreateNative("TF2_GetClass", Native_GetClass);
	CreateNative("TF2_IsHolidayActive", Native_IsHolidayActive);
	CreateNative("TF2_IsPlayerInDuel", Native_IsPlayerInDuel);
	CreateNative("TF2_RemoveWearable", Native_RemoveWearable);
	CreateNative("TF2GR_RunScript", Native_RunScript);
	CreateNative("TF2GR_GenerateAndWearItem", Native_GenerateAndWearItem);
	CreateNative("TF2GR_GetCondDuration", Native_GetCondDuration);
	CreateNative("TF2GR_SetCondDuration", Native_SetCondDuration);
	CreateNative("TF2GR_RemoveAllItems", Native_RemoveAllItems);
	CreateNative("TF2GR_IsReady", Native_IsReady);
	CreateNative("TF2GR_GiveItemByDef", Native_GiveItemByDef);
	CreateNative("TF2GR_RemoveWearables", Native_RemoveWearables);
	CreateNative("TF2GR_SetPlayerClass", Native_SetPlayerClass);
	CreateNative("TF2GR_GetItemName", Native_GetItemName);

	g_fwdCondAdded = new GlobalForward("TF2_OnConditionAdded", ET_Ignore, Param_Cell, Param_Cell);
	g_fwdCondRemoved = new GlobalForward("TF2_OnConditionRemoved", ET_Ignore, Param_Cell, Param_Cell);
	g_fwdWaitStart = new GlobalForward("TF2_OnWaitingForPlayersStart", ET_Ignore);
	g_fwdWaitEnd = new GlobalForward("TF2_OnWaitingForPlayersEnd", ET_Ignore);
	RegPluginLibrary("tf2goldrush");
	return APLRes_Success;
}

public void OnPluginStart()
{
	CreateConVar("tf2goldrush_tools_version", PLUGIN_VERSION, "TF2 Gold Rush Tools version.", FCVAR_NOTIFY | FCVAR_DONTRECORD);
	g_cvDebug = CreateConVar("tf2goldrush_tools_debug", "0", "Print condition changes to the server console.", FCVAR_DONTRECORD);
	for (int i = 0; i < COND_WORDS; i++)
		g_bCondProp[i] = FindSendPropInfo("CTFPlayer", g_sCondProps[i]) > 0;
	RegAdminCmd("sm_tf2goldrush_status", Cmd_Status, ADMFLAG_ROOT, "TF2 Gold Rush Tools: show backend status.");
	GoldRushStart();
	FovStart();
	AutoReloadStart();
	BotCivHookStart();
	for (int i = 1; i <= MaxClients; i++)
		if (IsClientInGame(i))
			BotCivHookClient(i);
	CivSoundsStart();
	AutoExecConfig(true, "tf2goldrush");
	// Without this repo's gamedata SDKTools has no AcceptInput offset on Gold Rush and every
	// script call (so every native) fails with "AcceptEntityInput not supported by this mod".
	GameData gd = new GameData("sdktools.games");
	if (gd == null || gd.GetOffset("AcceptInput") == -1)
		LogError("SDKTools has no AcceptInput offset for tf_goldrush: install gamedata/sdktools.games/custom/common.games.txt and gamedata/sdkhooks.games/custom/common.games.txt from SM-TF2GoldRush-Tools, then restart the server.");
	delete gd;
	HookEvent("teamplay_waiting_begins", Event_WaitBegins, EventHookMode_PostNoCopy);
	HookEvent("teamplay_waiting_ends", Event_WaitEnds, EventHookMode_PostNoCopy);
	g_bReady = true;
}

public Action Cmd_Status(int client, int args)
{
	ReplyToCommand(client, "[TF2 Gold Rush Tools] %s | forwards: added %d, removed %d, waiting %d/%d",
		PLUGIN_VERSION, g_fwdCondAdded.FunctionCount, g_fwdCondRemoved.FunctionCount,
		g_fwdWaitStart.FunctionCount, g_fwdWaitEnd.FunctionCount);
	for (int i = 0; i < COND_WORDS; i++)
		ReplyToCommand(client, "  %s: %s", g_sCondProps[i], g_bCondProp[i] ? "ok" : "missing");
	return Plugin_Handled;
}

/* ------------------------------------------------------------------ */
/* VScript bridge                                                     */
/* ------------------------------------------------------------------ */

void RunScript(int entity, const char[] code)
{
	SetVariantString(code);
	AcceptEntityInput(entity, "RunScriptCode");
}

/** Runs an expression in the entity's scope and returns it as a string. */
/**
 * Return channel: the script writes its result into a private entity's targetname, read
 * back from m_iName. (Live-TF2-style VScript only lets Convars.SetValue touch convars listed
 * in cfg/vscript_convar_allowlist.txt, so a convar channel would need server config edits.)
 */
int g_iRetEnt = INVALID_ENT_REFERENCE;
int RetEnt()
{
	int e = EntRefToEntIndex(g_iRetEnt);
	if (e > 0)
		return e;
	e = CreateEntityByName("info_target");
	DispatchSpawn(e);
	g_iRetEnt = EntIndexToEntRef(e);
	return e;
}

void ScriptEval(int entity, const char[] expr, char[] out, int maxlen)
{
	int ret = RetEnt();
	DispatchKeyValue(ret, "targetname", "");
	char code[1024];
	FormatEx(code, sizeof(code), "EntIndexToHScript(%d).KeyValueFromString(\"targetname\", (%s).tostring())", ret, expr);
	RunScript(entity, code);
	GetEntPropString(ret, Prop_Data, "m_iName", out, maxlen);
}

/** "null" or EntIndexToHScript(n) for an optional entity argument. */
void ScriptEntity(int entity, char[] out, int maxlen)
{
	if (entity > 0 && IsValidEntity(entity))
		FormatEx(out, maxlen, "EntIndexToHScript(%d)", entity);
	else
		strcopy(out, maxlen, "null");
}

/** Escapes a string for use inside a Squirrel "..." literal. */
void ScriptQuote(const char[] src, char[] out, int maxlen)
{
	int j = 0;
	for (int i = 0; src[i] != '\0' && j < maxlen - 2; i++)
	{
		if (src[i] == '"' || src[i] == '\\')
			out[j++] = '\\';
		out[j++] = src[i];
	}
	out[j] = '\0';
}

bool CheckClient(int client)
{
	if (client < 1 || client > MaxClients || !IsClientInGame(client))
	{
		ThrowNativeError(SP_ERROR_NATIVE, "Client index %d is invalid or not in game", client);
		return false;
	}
	return true;
}

/* ------------------------------------------------------------------ */
/* Natives                                                            */
/* ------------------------------------------------------------------ */

public any Native_IgnitePlayer(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (!CheckClient(client))
		return 0;
	float duration = GetNativeCell(3);
	// TF2: Gold Rush's VScript IgnitePlayer takes no attacker or duration.
	RunScript(client, "self.IgnitePlayer()");
	if (duration > 0.0)
	{
		char code[96];
		FormatEx(code, sizeof(code), "if (self.InCond(22)) self.SetCondDuration(22, %.3f)", duration);
		RunScript(client, code);
	}
	return 0;
}

public any Native_RespawnPlayer(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (CheckClient(client))
		RunScript(client, "self.ForceRespawn()");
	return 0;
}

public any Native_RegeneratePlayer(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (CheckClient(client))
		RunScript(client, "self.Regenerate(true)");
	return 0;
}

public any Native_AddCondition(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (!CheckClient(client))
		return 0;
	int cond = GetNativeCell(2);
	float duration = GetNativeCell(3);
	char provider[48], code[160];
	ScriptEntity(GetNativeCell(4), provider, sizeof(provider));
	FormatEx(code, sizeof(code), "self.AddCondEx(%d, %.3f, %s)", cond, duration, provider);
	RunScript(client, code);
	return 0;
}

public any Native_RemoveCondition(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (!CheckClient(client))
		return 0;
	char code[64];
	FormatEx(code, sizeof(code), "self.RemoveCondEx(%d, true)", GetNativeCell(2));
	RunScript(client, code);
	return 0;
}

/** Stock power play = crit boost + uber; reproduced with those conditions. */
public any Native_SetPlayerPowerPlay(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (!CheckClient(client))
		return 0;
	if (GetNativeCell(2))
		RunScript(client, "self.AddCondEx(11, -1.0, null); self.AddCondEx(5, -1.0, null)");
	else
		RunScript(client, "self.RemoveCondEx(11, true); self.RemoveCondEx(5, true)");
	return 0;
}

/**
 * TF2: Gold Rush's VScript has no Disguise; Spies disguise through the game's
 * own "disguise <class> <team>" command, which is what players type.
 */
public any Native_DisguisePlayer(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (!CheckClient(client))
		return 0;
	int team = GetNativeCell(2), cls = GetNativeCell(3);
	if (GetEntProp(client, Prop_Send, "m_iClass") != 8)
		return 0;   // only Spies can disguise
	FakeClientCommandEx(client, "disguise %d %d", cls, team);
	return 0;
}

public any Native_RemovePlayerDisguise(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (CheckClient(client))
		RunScript(client, "self.RemoveDisguise()");
	return 0;
}

public any Native_StunPlayer(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (!CheckClient(client))
		return 0;
	float duration = GetNativeCell(2), slowdown = GetNativeCell(3);
	int flags = GetNativeCell(4);
	char attacker[48], code[160];
	ScriptEntity(GetNativeCell(5), attacker, sizeof(attacker));
	FormatEx(code, sizeof(code), "self.StunPlayer(%.3f, %.3f, %d, %s)", duration, slowdown, flags, attacker);
	RunScript(client, code);
	return 0;
}

public any Native_MakeBleed(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (!CheckClient(client))
		return 0;
	// VScript BleedPlayer takes only the duration (no attacker).
	char code[64];
	FormatEx(code, sizeof(code), "self.BleedPlayer(%.3f)", view_as<float>(GetNativeCell(3)));
	RunScript(client, code);
	return 0;
}

public any Native_GetResourceEntity(Handle plugin, int numParams)
{
	return FindEntityByClassname(-1, "tf_player_manager");
}

public any Native_GetClass(Handle plugin, int numParams)
{
	char name[32];
	GetNativeString(1, name, sizeof(name));
	for (int i = 1; i < sizeof(g_sClassNames); i++)
		if (StrEqual(name, g_sClassNames[i], false))
			return i;
	if (StrEqual(name, "heavy", false))
		return 6;
	if (StrEqual(name, "demo", false))
		return 4;
	return 0;
}

public any Native_IsHolidayActive(Handle plugin, int numParams)
{
	char expr[48], out[16];
	FormatEx(expr, sizeof(expr), "IsHolidayActive(%d)", GetNativeCell(1));
	ScriptEval(Helper(), expr, out, sizeof(out));
	return StrEqual(out, "true");
}

/** A private point entity for global (non-entity) script calls. */
int g_iHelper = INVALID_ENT_REFERENCE;
int Helper()
{
	int e = EntRefToEntIndex(g_iHelper);
	if (e > 0)
		return e;
	e = CreateEntityByName("info_target");
	DispatchKeyValue(e, "targetname", "tf2goldrush_tools_helper");
	DispatchSpawn(e);
	g_iHelper = EntIndexToEntRef(e);
	return e;
}

public any Native_IsPlayerInDuel(Handle plugin, int numParams)
{
	return false;   // TF2: Gold Rush has no dueling minigame
}

public any Native_RemoveWearable(Handle plugin, int numParams)
{
	int wearable = GetNativeCell(2);
	if (wearable > MaxClients && IsValidEntity(wearable))
		RemoveEntity(wearable);
	return 0;
}

public any Native_RunScript(Handle plugin, int numParams)
{
	int entity = GetNativeCell(1);
	int len;
	GetNativeStringLength(2, len);
	char[] code = new char[len + 1];
	GetNativeString(2, code, len + 1);
	if (entity >= 0 && IsValidEntity(entity))
		RunScript(entity, code);
	return 0;
}

public any Native_GenerateAndWearItem(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (!CheckClient(client))
		return 0;
	char name[128], quoted[256], code[320];
	GetNativeString(2, name, sizeof(name));
	ScriptQuote(name, quoted, sizeof(quoted));
	FormatEx(code, sizeof(code), "self.GenerateAndWearItem(\"%s\")", quoted);
	RunScript(client, code);
	return 0;
}

public any Native_GetCondDuration(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (!CheckClient(client))
		return 0.0;
	char expr[64], out[32];
	FormatEx(expr, sizeof(expr), "self.InCond(%d) ? self.GetCondDuration(%d) : 0.0", GetNativeCell(2), GetNativeCell(2));
	ScriptEval(client, expr, out, sizeof(out));
	return StringToFloat(out);
}

public any Native_SetCondDuration(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (!CheckClient(client))
		return 0;
	char code[80];
	FormatEx(code, sizeof(code), "self.SetCondDuration(%d, %.3f)", GetNativeCell(2), view_as<float>(GetNativeCell(3)));
	RunScript(client, code);
	return 0;
}

public any Native_RemoveAllItems(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (!CheckClient(client))
		return 0;
	RunScript(client, GetNativeCell(2) ? "self.RemoveAllItems(true)" : "self.RemoveAllItems(false)");
	return 0;
}

public any Native_IsReady(Handle plugin, int numParams)
{
	return g_bReady;
}

/* ------------------------------------------------------------------ */
/* Forwards                                                           */
/* ------------------------------------------------------------------ */

public void OnClientPutInServer(int client)
{
	AutoReloadConnect(client);
	g_bTracked[client] = false;
	BotCivHookClient(client);
}

/**
 * Condition forwards: compare the networked condition bits each frame (the
 * stock extension's approach). Conditions 0-31 also live in _condition_bits.
 */
public void OnGameFrame()
{
	if (g_fwdCondAdded.FunctionCount == 0 && g_fwdCondRemoved.FunctionCount == 0)
		return;
	for (int client = 1; client <= MaxClients; client++)
	{
		if (!IsClientInGame(client) || !IsPlayerAlive(client))
		{
			g_bTracked[client] = false;
			continue;
		}
		for (int w = 0; w < COND_WORDS; w++)
		{
			if (!g_bCondProp[w])
				continue;
			int bits = GetEntProp(client, Prop_Send, g_sCondProps[w]);
			if (w == 0)
				bits |= GetEntProp(client, Prop_Send, "_condition_bits");
			int old = g_bTracked[client] ? g_iCondBits[client][w] : 0;
			if (bits == old)
				continue;
			g_iCondBits[client][w] = bits;
			int diff = bits ^ old;
			for (int b = 0; b < 32; b++)
			{
				if (!(diff & (1 << b)))
					continue;
				if (g_cvDebug.BoolValue)
					PrintToServer("[TF2 Gold Rush Tools] client %d cond %d %s", client, w * 32 + b, (bits & (1 << b)) ? "added" : "removed");
				Call_StartForward((bits & (1 << b)) ? g_fwdCondAdded : g_fwdCondRemoved);
				Call_PushCell(client);
				Call_PushCell(w * 32 + b);
				Call_Finish();
			}
		}
		g_bTracked[client] = true;
	}
}

public void Event_WaitBegins(Event event, const char[] name, bool dontBroadcast)
{
	Call_StartForward(g_fwdWaitStart);
	Call_Finish();
}

public void Event_WaitEnds(Event event, const char[] name, bool dontBroadcast)
{
	Call_StartForward(g_fwdWaitEnd);
	Call_Finish();
}

#include "goldrush_controls.inc"
#include "goldrush_fov.inc"
#include "goldrush_autoreload.inc"

public void OnMapStart()
{
	LoadItemNames();
	CivSoundsMapStart();
}
