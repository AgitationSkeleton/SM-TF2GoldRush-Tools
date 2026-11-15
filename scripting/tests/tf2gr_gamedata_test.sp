/**
 * Checks the TF2: Gold Rush SDKTools/SDKHooks gamedata (gamedata: sdktools.games and sdkhooks.games custom/common.games.txt).
 *   sm_grgd_step <n>   run one check against the first living bot (0 = list)
 *   sm_grgd_hooks      counts of SDKHooks/OnPlayerRunCmd callbacks since load
 * One step per command, so a wrong offset crashes on a known call.
 */
#include <sourcemod>
#include <sdktools>
#include <sdkhooks>

#pragma semicolon 1
#pragma newdecls required

public Plugin myinfo = { name = "Gold Rush gamedata test", author = "AgitationSkeleton", description = "", version = "1.0", url = "" };

int g_iRunCmd, g_iTakeDamage, g_iPreThink, g_iPostThink, g_iTouch, g_iSwitch, g_iSpawn, g_iThink, g_iTransmit;

public void OnPluginStart()
{
	RegAdminCmd("sm_grgd_step", Cmd_Step, ADMFLAG_ROOT);
	RegAdminCmd("sm_grgd_hooks", Cmd_Hooks, ADMFLAG_ROOT);
	for (int i = 1; i <= MaxClients; i++)
		if (IsClientInGame(i))
			OnClientPutInServer(i);
}

public void OnClientPutInServer(int client)
{
	SDKHook(client, SDKHook_OnTakeDamage, Hook_TakeDamage);
	SDKHook(client, SDKHook_PreThink, Hook_PreThink);
	SDKHook(client, SDKHook_PostThink, Hook_PostThink);
	SDKHook(client, SDKHook_StartTouch, Hook_Touch);
	SDKHook(client, SDKHook_WeaponSwitchPost, Hook_Switch);
	SDKHook(client, SDKHook_SpawnPost, Hook_Spawn);
	SDKHook(client, SDKHook_SetTransmit, Hook_Transmit);
}

public Action Hook_TakeDamage(int victim, int &attacker, int &inflictor, float &damage, int &damagetype) { g_iTakeDamage++; return Plugin_Continue; }
public void Hook_PreThink(int client) { g_iPreThink++; }
public void Hook_PostThink(int client) { g_iPostThink++; }
public Action Hook_Touch(int entity, int other) { g_iTouch++; return Plugin_Continue; }
public void Hook_Switch(int client, int weapon) { g_iSwitch++; }
public void Hook_Spawn(int client) { g_iSpawn++; }
public Action Hook_Transmit(int entity, int client) { g_iTransmit++; return Plugin_Continue; }
public void OnGameFrame() { g_iThink++; }

public Action OnPlayerRunCmd(int client, int &buttons, int &impulse, float vel[3], float angles[3], int &weapon)
{
	g_iRunCmd++;
	return Plugin_Continue;
}

public Action Cmd_Hooks(int client, int args)
{
	ReplyToCommand(client, "HOOKS runcmd=%d takedamage=%d prethink=%d postthink=%d touch=%d switch=%d spawn=%d transmit=%d frames=%d",
		g_iRunCmd, g_iTakeDamage, g_iPreThink, g_iPostThink, g_iTouch, g_iSwitch, g_iSpawn, g_iTransmit, g_iThink);
	return Plugin_Handled;
}

int Bot()
{
	for (int i = 1; i <= MaxClients; i++)
		if (IsClientInGame(i) && IsPlayerAlive(i) && IsFakeClient(i))
			return i;
	return 0;
}

public Action Cmd_Step(int client, int args)
{
	int n = GetCmdArgInt(1), b = Bot();
	if (!b)
		return Plugin_Handled;
	switch (n)
	{
		case 1:
		{
			SetVariantString("self.SetHealth(77)");
			AcceptEntityInput(b, "RunScriptCode");
			ReplyToCommand(client, "STEP 1 AcceptInput: health %d (want 77)", GetClientHealth(b));
		}
		case 2:
		{
			float pos[3];
			GetClientAbsOrigin(b, pos);
			pos[2] += 64.0;
			TeleportEntity(b, pos, NULL_VECTOR, view_as<float>({ 0.0, 0.0, 0.0 }));
			float now[3];
			GetClientAbsOrigin(b, now);
			ReplyToCommand(client, "STEP 2 Teleport: dz %.1f (want 64)", now[2] - pos[2] + 64.0);
		}
		case 3:
		{
			float ang[3];
			GetClientEyeAngles(b, ang);
			ReplyToCommand(client, "STEP 3 EyeAngles: %.1f %.1f %.1f", ang[0], ang[1], ang[2]);
		}
		case 4:
		{
			int w0 = GetPlayerWeaponSlot(b, 0), w1 = GetPlayerWeaponSlot(b, 1), w2 = GetPlayerWeaponSlot(b, 2);
			char c0[64], c2[64];
			if (w0 > 0) GetEntityClassname(w0, c0, sizeof(c0));
			if (w2 > 0) GetEntityClassname(w2, c2, sizeof(c2));
			ReplyToCommand(client, "STEP 4 Weapon_GetSlot: %d(%s) %d %d(%s)", w0, c0, w1, w2, c2);
		}
		case 5:
		{
			int prop = CreateEntityByName("prop_dynamic");
			SetEntityModel(prop, "models/items/ammopack_medium.mdl");
			DispatchSpawn(prop);
			char m[128];
			GetEntPropString(prop, Prop_Data, "m_ModelName", m, sizeof(m));
			ReplyToCommand(client, "STEP 5 SetEntityModel: %s", m);
			RemoveEntity(prop);
		}
		case 6:
		{
			int w = GetPlayerWeaponSlot(b, 2);
			bool ok = w > 0 && RemovePlayerItem(b, w);
			if (ok)
				AcceptEntityInput(w, "Kill");
			ReplyToCommand(client, "STEP 6 RemovePlayerItem(melee): %d, slot now %d", ok, GetPlayerWeaponSlot(b, 2));
		}
		case 7:
		{
			IgniteEntity(b, 2.0);
			ReplyToCommand(client, "STEP 7 Ignite: done (check the bot burns)");
		}
		case 8:
		{
			ExtinguishEntity(b);
			ReplyToCommand(client, "STEP 8 Extinguish: done");
		}
		case 9:
		{
			float v[3];
			GetEntPropVector(b, Prop_Data, "m_vecAbsVelocity", v);
			ReplyToCommand(client, "STEP 9 velocity prop %.1f %.1f %.1f", v[0], v[1], v[2]);
		}
		case 10:
		{
			ForcePlayerSuicide(b);
			ReplyToCommand(client, "STEP 10 CommitSuicide: alive %d (want 0)", IsPlayerAlive(b));
		}
		default:
			ReplyToCommand(client, "steps: 1 AcceptInput, 2 Teleport, 3 EyeAngles, 4 Weapon_GetSlot, 5 SetEntityModel, 6 RemovePlayerItem, 7 Ignite, 8 Extinguish, 9 velocity, 10 CommitSuicide");
	}
	return Plugin_Handled;
}
