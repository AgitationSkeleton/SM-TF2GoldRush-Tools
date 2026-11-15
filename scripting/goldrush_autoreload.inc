/**
 * TF2 Gold Rush Tools: live TF2's autoreload, per player, server-side.
 *
 * Live TF2 (CTFWeaponBase::ItemPostFrame) presses the reload key for the player when the
 * active weapon uses clips, its clip is not full and there is reserve ammo, and the player
 * is not holding attack or alt-attack. This does exactly that in OnPlayerRunCmd, so the
 * game's own reload code supplies the timing, and the cancel (firing aborts a reload,
 * shell by shell for single-reload weapons) behaves as in TF2.
 *
 * Opt in with !autoreload, by typing cl_autoreload 1 (Gold Rush's client forwards the unknown
 * command to the server), or through a client cl_autoreload userinfo value if the client has
 * one. The choice is saved in a cookie. tf2goldrush_autoreload (on by default) allows it.
 */

ConVar g_cvAutoReload;
Cookie g_ckAutoReload;
bool g_bAutoReload[MAXPLAYERS + 1];
int g_iAutoReloadPresses;       // ticks on which reload was pressed for a player (diagnostics)
StringMap g_MaxClip;             // weapon entref -> max clip (cached from VScript GetMaxClip1)

void AutoReloadStart()
{
	g_cvAutoReload = CreateConVar("tf2goldrush_autoreload", "1", "Players may opt in to live TF2's autoreload (!autoreload or cl_autoreload 1).", _, true, 0.0, true, 1.0);
	g_ckAutoReload = new Cookie("tf2goldrush_autoreload", "Gold Rush: autoreload (!autoreload / cl_autoreload).", CookieAccess_Private);
	g_MaxClip = new StringMap();
	RegConsoleCmd("sm_autoreload", Cmd_AutoReload, "Toggle automatic reloading (like live TF2's cl_autoreload).");
	RegConsoleCmd("cl_autoreload", Cmd_ClAutoReload, "cl_autoreload <0|1> - automatic reloading (handled by the server).");
	RegAdminCmd("sm_gr_setautoreload", Cmd_SetAutoReload, ADMFLAG_CHEATS, "sm_gr_setautoreload <target> <0|1> (testing).");
	RegAdminCmd("sm_gr_autoreload_stats", Cmd_AutoReloadStats, ADMFLAG_GENERIC, "Autoreload diagnostics: reload presses so far (resets).");
}

void AutoReloadCookies(int client)
{
	char v[4];
	g_ckAutoReload.Get(client, v, sizeof(v));
	if (v[0])
		g_bAutoReload[client] = v[0] == '1';
}

void AutoReloadConnect(int client)
{
	g_bAutoReload[client] = false;
	char v[8];
	// A client that has live TF2's cl_autoreload sends it as userinfo.
	if (!IsFakeClient(client) && GetClientInfo(client, "cl_autoreload", v, sizeof(v)) && v[0])
		g_bAutoReload[client] = StringToInt(v) > 0;
}

void SetAutoReload(int client, bool on)
{
	g_bAutoReload[client] = on;
	if (!IsFakeClient(client) && AreClientCookiesCached(client))
		g_ckAutoReload.Set(client, on ? "1" : "0");
}

public Action Cmd_AutoReload(int client, int args)
{
	if (!client)
		return Plugin_Handled;
	if (!g_cvAutoReload.BoolValue)
	{
		ReplyToCommand(client, "[SM] Autoreload is not enabled on this server.");
		return Plugin_Handled;
	}
	SetAutoReload(client, !g_bAutoReload[client]);
	ReplyToCommand(client, "[SM] Autoreload %s.", g_bAutoReload[client] ? "on" : "off");
	return Plugin_Handled;
}

public Action Cmd_ClAutoReload(int client, int args)
{
	if (!client)
		return Plugin_Handled;
	if (args < 1)
	{
		ReplyToCommand(client, "\"cl_autoreload\" = \"%d\" (handled by the server%s)", g_bAutoReload[client], g_cvAutoReload.BoolValue ? "" : ", disabled here");
		return Plugin_Handled;
	}
	if (g_cvAutoReload.BoolValue)
		SetAutoReload(client, GetCmdArgInt(1) > 0);
	return Plugin_Handled;
}

public Action Cmd_SetAutoReload(int client, int args)
{
	char pattern[64], tname[64];
	GetCmdArg(1, pattern, sizeof(pattern));
	bool on = GetCmdArgInt(2) > 0;
	int targets[MAXPLAYERS];
	bool ml;
	int n = ProcessTargetString(pattern, client, targets, sizeof(targets), 0, tname, sizeof(tname), ml);
	for (int i = 0; i < n; i++)
		SetAutoReload(targets[i], on);
	ReplyToCommand(client, "[SM] autoreload %d on %d player(s)", on, n);
	return Plugin_Handled;
}

public Action Cmd_AutoReloadStats(int client, int args)
{
	ReplyToCommand(client, "[SM] autoreload presses: %d", g_iAutoReloadPresses);
	g_iAutoReloadPresses = 0;
	return Plugin_Handled;
}

/** Max clip of a weapon (VScript GetMaxClip1, cached per weapon entity). */
int MaxClipOf(int weapon)
{
	char key[12], out[16];
	int v;
	IntToString(EntIndexToEntRef(weapon), key, sizeof(key));
	if (g_MaxClip.GetValue(key, v))
		return v;
	ScriptEval(weapon, "self.GetMaxClip1()", out, sizeof(out));
	v = out[0] ? StringToInt(out) : -1;
	g_MaxClip.SetValue(key, v);
	return v;
}

public Action OnPlayerRunCmd(int client, int &buttons, int &impulse, float vel[3], float angles[3], int &weapon)
{
	if (!g_bAutoReload[client] || !g_cvAutoReload.BoolValue || (buttons & (IN_ATTACK | IN_ATTACK2)) || !IsPlayerAlive(client))
		return Plugin_Continue;
	int w = GetEntPropEnt(client, Prop_Send, "m_hActiveWeapon");
	if (w <= MaxClients)
		return Plugin_Continue;
	int clip = GetEntProp(w, Prop_Send, "m_iClip1");
	if (clip < 0)
		return Plugin_Continue;                 // does not use clips
	int max = MaxClipOf(w);
	if (max <= 0 || clip >= max)
		return Plugin_Continue;
	int type = GetEntProp(w, Prop_Send, "m_iPrimaryAmmoType");
	if (type < 0 || GetEntProp(client, Prop_Send, "m_iAmmo", _, type) <= 0)
		return Plugin_Continue;                 // no reserve: nothing to reload
	buttons |= IN_RELOAD;
	g_iAutoReloadPresses++;
	return Plugin_Changed;
}

public void OnEntityDestroyed(int entity)
{
	if (g_MaxClip != null && entity > MaxClients && entity < 4096)
	{
		char key[12];
		IntToString(EntIndexToEntRef(entity), key, sizeof(key));
		g_MaxClip.Remove(key);
	}
}
