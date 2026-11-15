/**
 * TF2 Gold Rush Tools: unrestricted field of view (replaces unrestricted_fov.smx).
 *
 * !fov <value> sets the player's FOV beyond the client's 90 limit through the networked
 * m_iDefaultFOV/m_iFOV. Older plugins set it once and lose it when the game resets the
 * FOV (unscoping, teleporting, respawning, resupply, taunts). Here a 0.2 s check re-applies
 * the saved value whenever it has drifted and the player is not zoomed in, and spawn and
 * inventory events apply it immediately. The value is saved in a client cookie.
 */

#define TFCOND_ZOOMED 1

ConVar g_cvFovEnable, g_cvFovMin, g_cvFovMax;
Cookie g_ckFov;
int g_iFov[MAXPLAYERS + 1];

void FovStart()
{
	g_cvFovEnable = CreateConVar("tf2goldrush_fov_enable", "1", "Allow !fov (unrestricted field of view).", _, true, 0.0, true, 1.0);
	g_cvFovMin = CreateConVar("tf2goldrush_fov_min", "75", "Lowest FOV !fov accepts.", _, true, 20.0, true, 179.0);
	g_cvFovMax = CreateConVar("tf2goldrush_fov_max", "130", "Highest FOV !fov accepts.", _, true, 20.0, true, 179.0);
	g_ckFov = new Cookie("tf2goldrush_fov", "Gold Rush: saved field of view (!fov).", CookieAccess_Private);
	RegConsoleCmd("sm_fov", Cmd_Fov, "sm_fov <value> - set your field of view (0 = game default).");
	RegAdminCmd("sm_gr_setfov", Cmd_SetFov, ADMFLAG_CHEATS, "sm_gr_setfov <target> <value> - set a player's saved FOV (testing).");
	CreateTimer(0.2, Timer_Fov, _, TIMER_REPEAT);
}

public void OnClientCookiesCached(int client)
{
	char v[8];
	g_ckFov.Get(client, v, sizeof(v));
	g_iFov[client] = StringToInt(v);
	GrCookiesCached(client);
}

void FovDisconnect(int client)
{
	g_iFov[client] = 0;
}

public Action Cmd_Fov(int client, int args)
{
	if (!client)
		return Plugin_Handled;
	if (!g_cvFovEnable.BoolValue)
	{
		ReplyToCommand(client, "[SM] !fov is not enabled on this server.");
		return Plugin_Handled;
	}
	if (args < 1)
	{
		ReplyToCommand(client, "[SM] Usage: !fov <%d-%d> (0 = game default). Current: %d", g_cvFovMin.IntValue, g_cvFovMax.IntValue, g_iFov[client]);
		return Plugin_Handled;
	}
	int v = GetCmdArgInt(1);
	if (v != 0 && (v < g_cvFovMin.IntValue || v > g_cvFovMax.IntValue))
	{
		ReplyToCommand(client, "[SM] FOV must be between %d and %d (or 0).", g_cvFovMin.IntValue, g_cvFovMax.IntValue);
		return Plugin_Handled;
	}
	g_iFov[client] = v;
	char s[8];
	IntToString(v, s, sizeof(s));
	if (AreClientCookiesCached(client))
		g_ckFov.Set(client, s);
	if (v == 0)
	{
		// Back to the client's own fov_desired.
		QueryClientConVar(client, "fov_desired", Query_FovDesired);
	}
	else
		FovApply(client);
	ReplyToCommand(client, "[SM] FOV set to %d.", v ? v : 90);
	return Plugin_Handled;
}

public void Query_FovDesired(QueryCookie cookie, int client, ConVarQueryResult result, const char[] name, const char[] value)
{
	if (result != ConVarQuery_Okay || !IsClientInGame(client))
		return;
	int v = StringToInt(value);
	if (v < 20 || v > 90)
		v = 90;
	SetEntProp(client, Prop_Send, "m_iDefaultFOV", v);
	if (!InCondZoomed(client))
		SetEntProp(client, Prop_Send, "m_iFOV", v);
}

bool InCondZoomed(int client)
{
	return (GetEntProp(client, Prop_Send, "m_nPlayerCond") & (1 << TFCOND_ZOOMED)) != 0;
}

/** Applies the saved FOV unless the player is zoomed in (the scope sets its own FOV). */
void FovApply(int client)
{
	int v = g_iFov[client];
	if (v <= 0 || !g_cvFovEnable.BoolValue || !IsClientInGame(client) || (IsFakeClient(client) && !g_cvDebugBots.BoolValue))
		return;
	if (GetEntProp(client, Prop_Send, "m_iDefaultFOV") != v)
		SetEntProp(client, Prop_Send, "m_iDefaultFOV", v);
	if (!InCondZoomed(client) && GetEntProp(client, Prop_Send, "m_iFOV") != v)
		SetEntProp(client, Prop_Send, "m_iFOV", v);
}

public Action Timer_Fov(Handle timer)
{
	for (int i = 1; i <= MaxClients; i++)
		if (g_iFov[i] > 0 && IsClientInGame(i) && IsPlayerAlive(i))
			FovApply(i);
	return Plugin_Continue;
}

public Action Cmd_SetFov(int client, int args)
{
	char pattern[64], tname[64];
	GetCmdArg(1, pattern, sizeof(pattern));
	int v = GetCmdArgInt(2);
	int targets[MAXPLAYERS];
	bool ml;
	int n = ProcessTargetString(pattern, client, targets, sizeof(targets), 0, tname, sizeof(tname), ml);
	for (int i = 0; i < n; i++)
	{
		g_iFov[targets[i]] = v;
		FovApply(targets[i]);
	}
	ReplyToCommand(client, "[SM] FOV %d set on %d player(s)", v, n);
	return Plugin_Handled;
}
