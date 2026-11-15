// Test only: log each bot's class at spawn and death, and its weapons 1.5 s after spawning.
#include <sourcemod>
#include <sdktools>
public void OnPluginStart()
{
	HookEvent("player_spawn", E, EventHookMode_Post);
	HookEvent("player_death", E, EventHookMode_Pre);
	RegServerCmd("trace_weapons", Cmd_W);
}
public void E(Event ev, const char[] name, bool db)
{
	int c = GetClientOfUserId(ev.GetInt("userid"));
	if (!c || !IsFakeClient(c))
		return;
	LogMessage("%.2f %s %N cls=%d des=%d dmgbits=%d", GetGameTime(), name, c, GetEntProp(c, Prop_Send, "m_iClass"), GetEntProp(c, Prop_Send, "m_iDesiredPlayerClass"), ev.GetInt("damagebits"));
	if (name[7] == 's')
		CreateTimer(1.5, T, ev.GetInt("userid"));
}
public Action T(Handle t, int uid)
{
	int c = GetClientOfUserId(uid);
	if (c && IsPlayerAlive(c))
		LogW(c);
	return Plugin_Stop;
}
public Action Cmd_W(int args)
{
	for (int i = 1; i <= MaxClients; i++)
		if (IsClientInGame(i) && IsPlayerAlive(i))
			LogW(i);
	return Plugin_Handled;
}
void LogW(int c)
{
	int d[3] = { -1, -1, -1 };
	for (int s = 0; s < 3; s++)
	{
		int w = GetPlayerWeaponSlot(c, s);
		if (w > MaxClients)
			d[s] = GetEntProp(w, Prop_Send, "m_iItemDefinitionIndex");
	}
	char wear[128];
	int e = -1;
	while ((e = FindEntityByClassname(e, "tf_wearable*")) != -1)
		if (GetEntPropEnt(e, Prop_Send, "m_hOwnerEntity") == c)
			Format(wear, sizeof(wear), "%s%d,", wear, GetEntProp(e, Prop_Send, "m_iItemDefinitionIndex"));
	LogMessage("%.2f weapons %N cls=%d tauntcam=%d slots=%d/%d/%d wear=%s", GetGameTime(), c, GetEntProp(c, Prop_Send, "m_iClass"), GetEntProp(c, Prop_Send, "m_nForceTauntCam"), d[0], d[1], d[2], wear);
}
