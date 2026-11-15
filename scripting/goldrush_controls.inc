/**
 * TF2 Gold Rush Tools: Gold Rush controls for items (hats, weapons) and class 10.
 * Included by tf2goldrush.sp.
 */

ConVar g_cvCivEnable, g_cvCivHealth, g_cvCivSpeed, g_cvCivModel, g_cvCivLoadout, g_cvHats, g_cvBotHats;
StringMap g_ItemNames;      // defindex -> schema name
StringMap g_ItemClass;      // schema name (lower) -> "<defindex>|<item_class>"
ArrayList g_HatDefs;        // item_slot "head" defindices (random bot hats)
ArrayList g_HatMasks;       // matching used_by_classes bitmask (bit n = class n; 0 = any)
bool g_bCivAttrs[MAXPLAYERS + 1];
bool g_bHatRolled[MAXPLAYERS + 1];      // random bot hat: once per life
int g_iPinnedClass[MAXPLAYERS + 1];      // class set by sm_gr_class / !civilian (0 = none)
float g_flOwnGive[MAXPLAYERS + 1];       // GenerateAndWearItem fires post_inventory_application:
                                          // events right after our own grants are ignored

void GoldRushStart()
{
	g_cvCivEnable = CreateConVar("tf2goldrush_civilian_enable", "0", "Players may become the Civilian (class 10) with !civilian.", _, true, 0.0, true, 1.0);
	g_cvCivHealth = CreateConVar("tf2goldrush_civilian_health", "0", "Civilian max health (0 = class script value, 200).", _, true, 0.0);
	g_cvCivSpeed = CreateConVar("tf2goldrush_civilian_speed", "0", "Civilian max speed (0 = class script value, 250).", _, true, 0.0);
	g_cvCivModel = CreateConVar("tf2goldrush_civilian_model", "", "Civilian player model (empty = the class script's Scout model).");
	g_cvCivLoadout = CreateConVar("tf2goldrush_civilian_loadout", "", "Comma list of item names or defindices given to Civilians on spawn (empty = none, as in old TF2).");
	g_cvHats = CreateConVar("tf2goldrush_hats", "1", "0 = remove every player's hats and misc wearables on spawn.", _, true, 0.0, true, 1.0);
	g_cvBotHats = CreateConVar("tf2goldrush_bot_hats", "0", "Chance that a bot gets a random hat on spawn.", _, true, 0.0, true, 1.0);
	RegConsoleCmd("sm_civilian", Cmd_Civilian, "Play the Civilian (class 10) when tf2goldrush_civilian_enable is 1.");
	RegAdminCmd("sm_gr_class", Cmd_GrClass, ADMFLAG_SLAY, "sm_gr_class <target> <class|civilian> - set a player's class (civilian allowed).");
	RegAdminCmd("sm_gr_give", Cmd_GrGive, ADMFLAG_CHEATS, "sm_gr_give <target> <item name|defindex> - give a weapon or hat.");
	RegAdminCmd("sm_gr_strip", Cmd_GrStrip, ADMFLAG_CHEATS, "sm_gr_strip <target> <hats|weapons|all> - remove hats and/or weapons.");
	RegAdminCmd("sm_gr_items", Cmd_GrItems, ADMFLAG_GENERIC, "sm_gr_items [filter] - list schema items (defindex and name).");
	HookEvent("post_inventory_application", Event_GrInventory);
	HookEvent("player_spawn", Event_GrSpawn);
	AddCommandListener(Listener_GrJoinClass, "joinclass");
}

void LoadItemNames()
{
	delete g_ItemNames;
	delete g_HatDefs;
	delete g_ItemClass;
	delete g_HatMasks;
	g_HatMasks = new ArrayList();
	g_ItemClass = new StringMap();
	g_ItemNames = new StringMap();
	g_HatDefs = new ArrayList();
	KeyValues kv = new KeyValues("items_game");
	if (kv.ImportFromFile("scripts/items/items_game.txt") && kv.JumpToKey("items") && kv.GotoFirstSubKey())
	{
		char id[12], name[64], slot[16], cls[64], lname[64], val[80];
		do
		{
			kv.GetSectionName(id, sizeof(id));
			kv.GetString("name", name, sizeof(name));
			kv.GetString("item_slot", slot, sizeof(slot));
			if (!name[0] || !IsCharNumeric(id[0]))
				continue;
			g_ItemNames.SetString(id, name);
			kv.GetString("item_class", cls, sizeof(cls));
			if (!cls[0])
				ItemClassFromPrefab(kv, cls, sizeof(cls));
			if (cls[0])
			{
				strcopy(lname, sizeof(lname), name);
				StrToLower(lname);
				FormatEx(val, sizeof(val), "%s|%s", id, cls);
				g_ItemClass.SetString(lname, val);
			}
			if (StrEqual(slot, "head"))
			{
				g_HatDefs.Push(StringToInt(id));
				int mask = 0;
				if (kv.JumpToKey("used_by_classes") && kv.GotoFirstSubKey(false))
				{
					char cname[32];
					do
					{
						kv.GetSectionName(cname, sizeof(cname));
						int c = ClassFromArg(cname);
						if (c > 0)
							mask |= 1 << c;
					}
					while (kv.GotoNextKey(false));
					kv.GoBack();
					kv.GoBack();
				}
				g_HatMasks.Push(mask);
			}
		}
		while (kv.GotoNextKey());
	}
	delete kv;
}

void StrToLower(char[] s)
{
	for (int i = 0; s[i]; i++)
		s[i] = CharToLower(s[i]);
}

/** Stock weapons inherit item_class from a prefab ("weapon_bat" etc.): look it up. */
void ItemClassFromPrefab(KeyValues kv, char[] out, int maxlen)
{
	char prefab[64];
	kv.GetString("prefab", prefab, sizeof(prefab));
	out[0] = '\0';
	if (!prefab[0])
		return;
	int sp = FindCharInString(prefab, ' ');
	if (sp > 0)
		prefab[sp] = '\0';
	KeyValues pk = new KeyValues("items_game");
	if (pk.ImportFromFile("scripts/items/items_game.txt") && pk.JumpToKey("prefabs") && pk.JumpToKey(prefab))
		pk.GetString("item_class", out, maxlen);
	delete pk;
}

/**
 * Builds and equips a weapon entity directly (classic TF2 method; needs the SDKTools gamedata
 * this repo ships). Used for classes the schema does not list, e.g. the Civilian, where
 * GenerateAndWearItem refuses the item.
 */
bool EquipWeaponEntity(int client, const char[] name)
{
	char lname[64], val[80], parts[2][64];
	strcopy(lname, sizeof(lname), name);
	StrToLower(lname);
	if (g_ItemClass == null || !g_ItemClass.GetString(lname, val, sizeof(val)) || ExplodeString(val, "|", parts, 2, 64) < 2)
		return false;
	if (StrContains(parts[1], "tf_weapon_") != 0)
		return false;   // only weapons this way (wearables need the econ system)
	int def = StringToInt(parts[0]);
	int max = GetEntPropArraySize(client, Prop_Send, "m_hMyWeapons");
	for (int i = 0; i < max; i++)
	{
		int have = GetEntPropEnt(client, Prop_Send, "m_hMyWeapons", i);
		if (have > MaxClients && GetEntProp(have, Prop_Send, "m_iItemDefinitionIndex") == def)
			return true;    // already holds it (inventory events can repeat)
	}
	int w = CreateEntityByName(parts[1]);
	if (w == -1)
		return false;
	SetEntProp(w, Prop_Send, "m_iItemDefinitionIndex", def);
	SetEntProp(w, Prop_Send, "m_bInitialized", 1);
	SetEntProp(w, Prop_Send, "m_iEntityLevel", 1);
	SetEntProp(w, Prop_Send, "m_iEntityQuality", 0);
	SetEntProp(w, Prop_Send, "m_iAccountID", GetSteamAccountID(client, false));
	if (!DispatchSpawn(w))
	{
		RemoveEntity(w);
		return false;
	}
	EquipPlayerWeapon(client, w);
	return true;
}

bool ItemNameOf(int def, char[] out, int maxlen)
{
	char key[12];
	IntToString(def, key, sizeof(key));
	return g_ItemNames != null && g_ItemNames.GetString(key, out, maxlen);
}

/** "123" or an item name -> the schema name to give. */
bool ResolveItem(const char[] arg, char[] out, int maxlen)
{
	if (arg[0] && IsCharNumeric(arg[0]))
		return ItemNameOf(StringToInt(arg), out, maxlen);
	strcopy(out, maxlen, arg);
	return arg[0] != '\0';
}

void GiveItemName(int client, const char[] name)
{
	g_flOwnGive[client] = GetEngineTime();
	if (GetEntProp(client, Prop_Send, "m_iClass") == 10 && EquipWeaponEntity(client, name))
		return;
	char q[160], code[256];
	ScriptQuote(name, q, sizeof(q));
	FormatEx(code, sizeof(code), "self.GenerateAndWearItem(\"%s\")", q);
	RunScript(client, code);
}

int StripWearables(int client)
{
	int n = 0, ent = -1;
	while ((ent = FindEntityByClassname(ent, "tf_wearable*")) != -1)
	{
		if (GetEntPropEnt(ent, Prop_Send, "m_hOwnerEntity") == client)
		{
			RemoveEntity(ent);
			n++;
		}
	}
	return n;
}

int StripWeapons(int client)
{
	int n = 0;
	for (int s = 0; s < 6; s++)
	{
		int w;
		while ((w = GetPlayerWeaponSlot(client, s)) > MaxClients)
		{
			RemovePlayerItem(client, w);
			RemoveEntity(w);
			n++;
		}
	}
	return n;
}

/** Class 10 cannot be joined through the class menu: write the desired class and respawn,
 *  which is where TF2's respawn code applies a class. */
void SetClassEx(int client, int cls, bool respawn)
{
	g_iPinnedClass[client] = cls;
	SetEntProp(client, Prop_Send, "m_iDesiredPlayerClass", cls);
	if (respawn && GetClientTeam(client) >= 2)
		RunScript(client, "self.ForceRespawn()");
}

public any Native_GiveItemByDef(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (!CheckClient(client))
		return false;
	char name[64];
	if (!ItemNameOf(GetNativeCell(2), name, sizeof(name)))
		return false;
	GiveItemName(client, name);
	return true;
}

public any Native_RemoveWearables(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	return CheckClient(client) ? StripWearables(client) : 0;
}

public any Native_SetPlayerClass(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (CheckClient(client))
		SetClassEx(client, GetNativeCell(2), GetNativeCell(3));
	return 0;
}

public any Native_GetItemName(Handle plugin, int numParams)
{
	char name[64];
	bool ok = ItemNameOf(GetNativeCell(1), name, sizeof(name));
	SetNativeString(2, name, GetNativeCell(3));
	return ok;
}

int ClassFromArg(const char[] arg)
{
	static const char names[][] = { "", "scout", "sniper", "soldier", "demoman", "medic", "heavy", "pyro", "spy", "engineer", "civilian" };
	if (IsCharNumeric(arg[0]))
		return StringToInt(arg);
	if (StrEqual(arg, "civ", false))
		return 10;
	if (StrEqual(arg, "demo", false))
		return 4;
	for (int i = 1; i < sizeof(names); i++)
		if (StrEqual(arg, names[i], false))
			return i;
	return 0;
}

public Action Cmd_Civilian(int client, int args)
{
	if (!client || !g_cvCivEnable.BoolValue)
	{
		ReplyToCommand(client, "[SM] The Civilian is not enabled on this server.");
		return Plugin_Handled;
	}
	SetClassEx(client, 10, IsPlayerAlive(client));
	return Plugin_Handled;
}

public Action Cmd_GrClass(int client, int args)
{
	char pattern[64], arg[32], tname[64];
	GetCmdArg(1, pattern, sizeof(pattern));
	GetCmdArg(2, arg, sizeof(arg));
	int cls = ClassFromArg(arg);
	if (cls < 1 || cls > 10)
	{
		ReplyToCommand(client, "Usage: sm_gr_class <target> <scout..engineer|civilian>");
		return Plugin_Handled;
	}
	int targets[MAXPLAYERS];
	bool ml;
	int n = ProcessTargetString(pattern, client, targets, sizeof(targets), 0, tname, sizeof(tname), ml);
	for (int i = 0; i < n; i++)
		SetClassEx(targets[i], cls, IsPlayerAlive(targets[i]));
	ReplyToCommand(client, "[SM] class %d set on %d player(s)", cls, n);
	return Plugin_Handled;
}

public Action Cmd_GrGive(int client, int args)
{
	char pattern[64], arg[64], name[64], tname[64];
	GetCmdArg(1, pattern, sizeof(pattern));
	GetCmdArg(2, arg, sizeof(arg));
	if (!ResolveItem(arg, name, sizeof(name)))
	{
		ReplyToCommand(client, "[SM] unknown item %s (see sm_gr_items)", arg);
		return Plugin_Handled;
	}
	int targets[MAXPLAYERS];
	bool ml;
	int n = ProcessTargetString(pattern, client, targets, sizeof(targets), COMMAND_FILTER_ALIVE, tname, sizeof(tname), ml);
	for (int i = 0; i < n; i++)
		GiveItemName(targets[i], name);
	ReplyToCommand(client, "[SM] gave %s to %d player(s)", name, n);
	return Plugin_Handled;
}

public Action Cmd_GrStrip(int client, int args)
{
	char pattern[64], what[16], tname[64];
	GetCmdArg(1, pattern, sizeof(pattern));
	GetCmdArg(2, what, sizeof(what));
	bool hats = StrEqual(what, "hats") || StrEqual(what, "all"), weapons = StrEqual(what, "weapons") || StrEqual(what, "all");
	if (!hats && !weapons)
	{
		ReplyToCommand(client, "Usage: sm_gr_strip <target> <hats|weapons|all>");
		return Plugin_Handled;
	}
	int targets[MAXPLAYERS];
	bool ml;
	int n = ProcessTargetString(pattern, client, targets, sizeof(targets), COMMAND_FILTER_ALIVE, tname, sizeof(tname), ml);
	int removed = 0;
	for (int i = 0; i < n; i++)
	{
		if (hats)
			removed += StripWearables(targets[i]);
		if (weapons)
			removed += StripWeapons(targets[i]);
	}
	ReplyToCommand(client, "[SM] removed %d item(s) from %d player(s)", removed, n);
	return Plugin_Handled;
}

public Action Cmd_GrItems(int client, int args)
{
	char filter[64], key[12], name[64];
	GetCmdArg(1, filter, sizeof(filter));
	StringMapSnapshot snap = g_ItemNames.Snapshot();
	int shown = 0;
	for (int i = 0; i < snap.Length; i++)
	{
		snap.GetKey(i, key, sizeof(key));
		g_ItemNames.GetString(key, name, sizeof(name));
		if (filter[0] && StrContains(name, filter, false) == -1)
			continue;
		ReplyToCommand(client, "%6s  %s", key, name);
		shown++;
	}
	delete snap;
	ReplyToCommand(client, "%d item(s)", shown);
	return Plugin_Handled;
}

public void Event_GrInventory(Event event, const char[] name, bool dontBroadcast)
{
	int client = GetClientOfUserId(event.GetInt("userid"));
	if (client && GetEngineTime() - g_flOwnGive[client] > 0.25)
		RequestFrame(Frame_GrInventory, GetClientUserId(client));
}

/** A class set by an admin or !civilian sticks (bots re-pick their class on respawn). */
public void Event_GrSpawn(Event event, const char[] name, bool dontBroadcast)
{
	int client = GetClientOfUserId(event.GetInt("userid"));
	if (!client)
		return;
	g_bHatRolled[client] = false;
	int pin = g_iPinnedClass[client];
	if (pin > 0 && GetEntProp(client, Prop_Send, "m_iClass") != pin && GetClientTeam(client) >= 2)
	{
		SetEntProp(client, Prop_Send, "m_iDesiredPlayerClass", pin);
		RequestFrame(Frame_GrRespawn, GetClientUserId(client));
	}
}

public void Frame_GrRespawn(int userid)
{
	int client = GetClientOfUserId(userid);
	if (client && IsPlayerAlive(client))
		RunScript(client, "self.ForceRespawn()");
}

/** A human picking a class from the menu drops the pinned class. */
public Action Listener_GrJoinClass(int client, const char[] command, int argc)
{
	if (client && !IsFakeClient(client))
		g_iPinnedClass[client] = 0;
	return Plugin_Continue;
}

public void OnClientDisconnect(int client)
{
	g_iPinnedClass[client] = 0;
	g_bCivAttrs[client] = false;
}

public void Frame_GrInventory(int userid)
{
	int client = GetClientOfUserId(userid);
	if (!client || !IsPlayerAlive(client))
		return;
	if (!g_cvHats.BoolValue)
		StripWearables(client);
	else if (IsFakeClient(client) && !g_bHatRolled[client] && g_HatDefs != null && g_HatDefs.Length
		&& (g_bHatRolled[client] = true) && GetURandomFloat() < g_cvBotHats.FloatValue)
	{
		// A hat the bot's class may wear (the Civilian, which no hat lists, may wear any).
		int cls = GetEntProp(client, Prop_Send, "m_iClass");
		ArrayList fit = new ArrayList();
		for (int i = 0; i < g_HatDefs.Length; i++)
		{
			int mask = g_HatMasks.Get(i);
			if (cls == 10 || mask == 0 || (mask & (1 << cls)))
				fit.Push(g_HatDefs.Get(i));
		}
		char hat[64];
		if (fit.Length && ItemNameOf(fit.Get(GetRandomInt(0, fit.Length - 1)), hat, sizeof(hat)))
			GiveItemName(client, hat);
		delete fit;
	}
	if (GetEntProp(client, Prop_Send, "m_iClass") != 10)
	{
		// Civilian health/speed attributes stay on the player entity: drop them on any other class.
		if (g_bCivAttrs[client])
		{
			RunScript(client, "self.RemoveCustomAttribute(\"max health additive bonus\"); self.RemoveCustomAttribute(\"move speed bonus\"); self.SetHealth(self.GetMaxHealth())");
			g_bCivAttrs[client] = false;
		}
		return;
	}
	char model[PLATFORM_MAX_PATH], list[512], parts[16][64], item[64], code[192];
	g_cvCivModel.GetString(model, sizeof(model));
	if (model[0])
	{
		if (!IsModelPrecached(model))
			PrecacheModel(model, true);
		SetVariantString(model);
		AcceptEntityInput(client, "SetCustomModelWithClassAnimations");
	}
	if (g_cvCivHealth.IntValue > 0)
	{
		FormatEx(code, sizeof(code), "self.AddCustomAttribute(\"max health additive bonus\", %d.0, -1); self.SetHealth(%d)", g_cvCivHealth.IntValue - 200, g_cvCivHealth.IntValue);
		RunScript(client, code);
		g_bCivAttrs[client] = true;
	}
	if (g_cvCivSpeed.FloatValue > 0.0)
	{
		FormatEx(code, sizeof(code), "self.AddCustomAttribute(\"move speed bonus\", %f, -1)", g_cvCivSpeed.FloatValue / 250.0);
		RunScript(client, code);
		g_bCivAttrs[client] = true;
	}
	g_cvCivLoadout.GetString(list, sizeof(list));
	int n = ExplodeString(list, ",", parts, sizeof(parts), sizeof(parts[]));
	for (int i = 0; i < n; i++)
	{
		TrimString(parts[i]);
		if (ResolveItem(parts[i], item, sizeof(item)))
			GiveItemName(client, item);
	}
}
