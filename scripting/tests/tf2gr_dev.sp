/**
 * Developer helper for TF2: Gold Rush experiments (test servers only).
*   (inside <code>, write ' wherever a double quote is needed)
 *   sm_vs <target> <code>   run VScript in each target's scope (self = player)
 *   sm_vsw <target> <code>  same, on each target's active weapon
 */
#include <sourcemod>
#include <sdktools>
#include <tf2>

#pragma semicolon 1
#pragma newdecls required

public Plugin myinfo = { name = "TF2 Gold Rush dev helper", author = "AgitationSkeleton", description = "", version = "1.0", url = "" };

public void OnPluginStart()
{
	RegAdminCmd("sm_vs", Cmd_VS, ADMFLAG_ROOT);
	RegAdminCmd("sm_vsw", Cmd_VS, ADMFLAG_ROOT);
	RegAdminCmd("sm_fake", Cmd_Fake, ADMFLAG_ROOT, "sm_fake <target> <command> - run a client command as the target (testing).");
}

public Action Cmd_VS(int client, int args)
{
	char cmd[8], pattern[64], code[1024], name[64];
	GetCmdArg(0, cmd, sizeof(cmd));
	GetCmdArg(1, pattern, sizeof(pattern));
	GetCmdArgString(code, sizeof(code));
	int skip = StrContains(code, pattern) + strlen(pattern);
	strcopy(code, sizeof(code), code[skip]);
	TrimString(code);
	StripQuotes(code);
	ReplaceString(code, sizeof(code), "'", "\"");   // console has no escapes: write ' for "
	int targets[MAXPLAYERS];
	bool ml;
	int n = ProcessTargetString(pattern, client, targets, sizeof(targets), 0, name, sizeof(name), ml);
	for (int i = 0; i < n; i++)
	{
		int ent = targets[i];
		if (StrEqual(cmd, "sm_vsw"))
			ent = GetEntPropEnt(ent, Prop_Send, "m_hActiveWeapon");
		if (ent > 0)
			TF2GR_RunScript(ent, code);
	}
	return Plugin_Handled;
}

public Action Cmd_Fake(int client, int args)
{
	char pattern[64], cmd[256], name[64];
	GetCmdArg(1, pattern, sizeof(pattern));
	GetCmdArgString(cmd, sizeof(cmd));
	int skip = StrContains(cmd, pattern) + strlen(pattern);
	strcopy(cmd, sizeof(cmd), cmd[skip]);
	TrimString(cmd);
	StripQuotes(cmd);
	int targets[MAXPLAYERS];
	bool ml;
	int n = ProcessTargetString(pattern, client, targets, sizeof(targets), 0, name, sizeof(name), ml);
	for (int i = 0; i < n; i++)
		FakeClientCommand(targets[i], "%s", cmd);
	return Plugin_Handled;
}
