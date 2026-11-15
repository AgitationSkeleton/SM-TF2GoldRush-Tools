/**
 * TF2 Gold Rush Tools: Gold Rush controls. Included by tf2goldrush.sp.
 *
 *  - items: give/strip hats and weapons, list the schema (prefab-aware item table);
 *  - weapon rules: whitelist file and force-stock, applied on every spawn/resupply;
 *  - unlocks: when tf2goldrush_givemeall is on, everyone gets a server-side unlock menu
 *    (!unlocks, alias !givemeall); a pick is equipped at once and on every spawn/resupply. It does not touch Steam achievements: granting achievements from
 *    a server would be achievement manipulation; this gives the same access to Gold Rush's
 *    unlock weapons without it;
 *  - class 10 (the Civilian): commands, joinclass civilian, bot chance, launch-TF2 stats
 *    (50 health, 240 speed, from launch TF2's civilian.ctx), model, loadout, pinning.
 */

#define SLOT_PRIMARY 0
#define SLOT_SECONDARY 1
#define SLOT_MELEE 2
#define SLOT_HEAD 10
#define SLOT_MISC 11
#define SLOT_OTHER -1
#define LAUNCH_CIV_HEALTH 50      // launch TF2 scripts/playerclasses/civilian.ctx
#define LAUNCH_CIV_SPEED 240.0
#define GR_CLASS_HEALTH 200       // Gold Rush's civilian.ctx (TF2's current one)
#define GR_CLASS_SPEED 250.0

enum struct ItemInfo
{
	int def;
	int slot;          // SLOT_* (weapon slot, head, other)
	bool isStock;      // baseitem
	int mask;          // used_by_classes bits (bit n = class n)
	char name[64];
	char cls[48];      // item_class
}

ConVar g_cvCivEnable, g_cvCivHealth, g_cvCivSpeed, g_cvCivLaunch, g_cvCivModel, g_cvCivLoadout, g_cvCivBotChance;
ConVar g_cvHats, g_cvHeadgear, g_cvBotHats, g_cvWhitelist, g_cvWhitelistFile, g_cvForceStock, g_cvGiveMeAll;
ArrayList g_Items;              // ItemInfo
StringMap g_ItemIndex;          // defindex -> index into g_Items
StringMap g_NameIndex;          // lower-case name -> index into g_Items
StringMap g_Whitelist;          // defindex -> true
Cookie g_ckPicks;               // "c<class>s<slot>=<def>;..."
bool g_bCivAttrs[MAXPLAYERS + 1];
bool g_bHatRolled[MAXPLAYERS + 1];
int g_iBotCivReturn[MAXPLAYERS + 1];  // bot rolled Civilian: its own class, handed back after the Civilian respawn
bool g_bBotCivLife[MAXPLAYERS + 1];   // this bot life is a rolled Civilian (its class-switch suicide is blocked)
bool g_bBotCivAllowDeath[MAXPLAYERS + 1];
int g_iBotCivTeam[MAXPLAYERS + 1];
int g_iPinnedClass[MAXPLAYERS + 1];
char g_sPicks[MAXPLAYERS + 1][512]; // memory copy of the picks cookie (bots have no cookies)
ConVar g_cvDebugBots;
float g_flOwnGive[MAXPLAYERS + 1];  // GenerateAndWearItem fires post_inventory_application: events
                                     // right after our own grants are ignored (loop guard)

void GoldRushStart()
{
	g_cvCivEnable = CreateConVar("tf2goldrush_civilian_enable", "0", "Players may become the Civilian (class 10) with !civilian or joinclass civilian.", _, true, 0.0, true, 1.0);
	g_cvCivLaunch = CreateConVar("tf2goldrush_civilian_launch_stats", "1", "Civilian uses launch TF2's stats (50 health, 240 speed) instead of the class script's (200 / 250).", _, true, 0.0, true, 1.0);
	g_cvCivHealth = CreateConVar("tf2goldrush_civilian_health", "0", "Civilian max health override (0 = launch or class script value).", _, true, 0.0);
	g_cvCivSpeed = CreateConVar("tf2goldrush_civilian_speed", "0", "Civilian max speed override (0 = launch or class script value).", _, true, 0.0);
	g_cvCivModel = CreateConVar("tf2goldrush_civilian_model", "", "Civilian player model (empty = the class script's Scout model).");
	g_cvCivLoadout = CreateConVar("tf2goldrush_civilian_loadout", "", "Comma list of item names or defindices given to Civilians on spawn (empty = none, as in old TF2).");
	g_cvCivBotChance = CreateConVar("tf2goldrush_civilian_bot_chance", "0", "Chance (0-1) that a bot plays the Civilian for a life (rolled on every bot spawn; needs tf2goldrush_civilian_enable).", _, true, 0.0, true, 1.0);
	g_cvHats = CreateConVar("tf2goldrush_hats", "1", "0 = remove every player's cosmetics (head and misc items) on spawn. Wearable weapons are kept.", _, true, 0.0, true, 1.0);
	g_cvHeadgear = CreateConVar("tf2goldrush_headgear", "1", "0 = remove every player's headgear (head-slot items) on spawn.", _, true, 0.0, true, 1.0);
	g_cvBotHats = CreateConVar("tf2goldrush_bot_hats", "0", "Chance that a bot gets a random hat its class may wear.", _, true, 0.0, true, 1.0);
	g_cvWhitelist = CreateConVar("tf2goldrush_whitelist", "0", "1 = only weapons listed in the whitelist file are allowed; others are replaced by the stock weapon.", _, true, 0.0, true, 1.0);
	g_cvWhitelistFile = CreateConVar("tf2goldrush_whitelist_file", "configs/tf2goldrush_whitelist.txt", "Weapon whitelist (path under addons/sourcemod): one item defindex or name per line; stock weapons are always allowed.");
	g_cvForceStock = CreateConVar("tf2goldrush_force_stock", "0", "1 = every weapon is replaced by its slot's stock weapon.", _, true, 0.0, true, 1.0);
	g_cvGiveMeAll = CreateConVar("tf2goldrush_givemeall", "0", "1 = everyone may pick any Gold Rush unlock weapon with !unlocks / !givemeall (server-side; no achievements are touched).", _, true, 0.0, true, 1.0);
	g_cvDebugBots = CreateConVar("tf2goldrush_debug_bots", "0", "Testing: let bots use unlock picks and !fov values.", FCVAR_DONTRECORD, true, 0.0, true, 1.0);
	g_cvWhitelist.AddChangeHook(Cvar_Whitelist);
	g_cvWhitelistFile.AddChangeHook(Cvar_Whitelist);
	g_ckPicks = new Cookie("tf2goldrush_picks", "Gold Rush: chosen unlock weapons (!unlocks).", CookieAccess_Private);
	RegConsoleCmd("sm_civilian", Cmd_Civilian, "Play the Civilian (class 10) when tf2goldrush_civilian_enable is 1.");
	RegConsoleCmd("sm_givemeall", Cmd_GiveMeAll, "Same as !unlocks: pick any Gold Rush unlock weapon for your class (when enabled).");
	RegConsoleCmd("sm_unlocks", Cmd_Unlocks, "Pick any Gold Rush unlock weapon for your current class (when enabled).");
	RegAdminCmd("sm_gr_class", Cmd_GrClass, ADMFLAG_SLAY, "sm_gr_class <target> <class|civilian> - set a player's class (civilian allowed).");
	RegAdminCmd("sm_gr_give", Cmd_GrGive, ADMFLAG_CHEATS, "sm_gr_give <target> <item name|defindex> - give a weapon or hat.");
	RegAdminCmd("sm_gr_strip", Cmd_GrStrip, ADMFLAG_CHEATS, "sm_gr_strip <target> <hats|weapons|all> - remove hats and/or weapons.");
	RegAdminCmd("sm_gr_items", Cmd_GrItems, ADMFLAG_GENERIC, "sm_gr_items [filter] - list schema items (defindex, slot, stock, name).");
	RegAdminCmd("sm_gr_setpick", Cmd_SetPick, ADMFLAG_CHEATS, "sm_gr_setpick <target> <slot 0-2> <defindex|0> - set a player's unlock pick for their class.");
	RegAdminCmd("sm_gr_whitelist_reload", Cmd_WhitelistReload, ADMFLAG_CONFIG, "Reload the weapon whitelist file.");
	HookEvent("post_inventory_application", Event_GrInventory);
	HookEvent("player_spawn", Event_GrSpawn);
	AddCommandListener(Listener_GrJoinClass, "joinclass");
	AddCommandListener(Listener_GrJoinClass, "join_class");
}

/* ------------------------------------------------------------------ */
/* Item table                                                         */
/* ------------------------------------------------------------------ */

int SlotFromName(const char[] slot)
{
	if (StrEqual(slot, "primary"))
		return SLOT_PRIMARY;
	if (StrEqual(slot, "secondary"))
		return SLOT_SECONDARY;
	if (StrEqual(slot, "melee"))
		return SLOT_MELEE;
	if (StrEqual(slot, "head"))
		return SLOT_HEAD;
	if (StrEqual(slot, "misc"))
		return SLOT_MISC;
	return SLOT_OTHER;
}

/** Item key with prefab inheritance (prefabs may chain; the last listed prefab wins). */
bool ItemKey(KeyValues items, KeyValues prefabs, const char[] key, char[] out, int maxlen, int depth = 0)
{
	items.GetString(key, out, maxlen, "\x01");
	if (!StrEqual(out, "\x01"))
		return true;
	out[0] = '\0';
	if (depth > 4)
		return false;
	char list[128], names[4][48];
	items.GetString("prefab", list, sizeof(list));
	int n = ExplodeString(list, " ", names, sizeof(names), sizeof(names[]));
	for (int i = n - 1; i >= 0; i--)
	{
		if (!names[i][0])
			continue;
		prefabs.Rewind();
		if (prefabs.JumpToKey("prefabs") && prefabs.JumpToKey(names[i]))
		{
			KeyValues sub = new KeyValues("p");
			sub.Import(prefabs);
			bool ok = ItemKey(sub, prefabs, key, out, maxlen, depth + 1);
			delete sub;
			if (ok)
				return true;
		}
	}
	return false;
}

int ClassMask(KeyValues items)
{
	int mask = 0;
	if (items.JumpToKey("used_by_classes"))
	{
		if (items.GotoFirstSubKey(false))
		{
			char cname[32];
			do
			{
				items.GetSectionName(cname, sizeof(cname));
				int c = ClassFromArg(cname);
				if (c > 0)
					mask |= 1 << c;
			}
			while (items.GotoNextKey(false));
			items.GoBack();
		}
		items.GoBack();
	}
	return mask;
}

void LoadItemNames()
{
	delete g_Items;
	delete g_ItemIndex;
	delete g_NameIndex;
	g_Items = new ArrayList(sizeof(ItemInfo));
	g_ItemIndex = new StringMap();
	g_NameIndex = new StringMap();
	KeyValues kv = new KeyValues("items_game");
	KeyValues pre = new KeyValues("items_game");
	if (!kv.ImportFromFile("scripts/items/items_game.txt") || !pre.ImportFromFile("scripts/items/items_game.txt"))
	{
		LogError("cannot read scripts/items/items_game.txt");
		delete kv;
		delete pre;
		return;
	}
	if (kv.JumpToKey("items") && kv.GotoFirstSubKey())
	{
		char id[12], buf[64];
		do
		{
			kv.GetSectionName(id, sizeof(id));
			if (!IsCharNumeric(id[0]))
				continue;
			ItemInfo it;
			it.def = StringToInt(id);
			kv.GetString("name", it.name, sizeof(it.name));
			if (!it.name[0])
				continue;
			ItemKey(kv, pre, "item_slot", buf, sizeof(buf));
			it.slot = SlotFromName(buf);
			ItemKey(kv, pre, "baseitem", buf, sizeof(buf));
			it.isStock = StringToInt(buf) != 0;
			ItemKey(kv, pre, "item_class", it.cls, sizeof(it.cls));
			it.mask = ClassMask(kv);
			int idx = g_Items.PushArray(it);
			g_ItemIndex.SetValue(id, idx);
			strcopy(buf, sizeof(buf), it.name);
			StrToLower(buf);
			g_NameIndex.SetValue(buf, idx);
		}
		while (kv.GotoNextKey());
	}
	delete kv;
	delete pre;
	LoadWhitelist();
}

void StrToLower(char[] s)
{
	for (int i = 0; s[i]; i++)
		s[i] = CharToLower(s[i]);
}

bool ItemByDef(int def, ItemInfo it)
{
	char key[12];
	int idx;
	IntToString(def, key, sizeof(key));
	if (g_ItemIndex == null || !g_ItemIndex.GetValue(key, idx))
		return false;
	g_Items.GetArray(idx, it);
	return true;
}

bool ItemByName(const char[] name, ItemInfo it)
{
	char low[64];
	int idx;
	strcopy(low, sizeof(low), name);
	StrToLower(low);
	if (g_NameIndex == null || !g_NameIndex.GetValue(low, idx))
		return false;
	g_Items.GetArray(idx, it);
	return true;
}

bool ItemNameOf(int def, char[] out, int maxlen)
{
	ItemInfo it;
	if (!ItemByDef(def, it))
		return false;
	strcopy(out, maxlen, it.name);
	return true;
}

/** "123" or an item name -> the schema name to give. */
bool ResolveItem(const char[] arg, char[] out, int maxlen)
{
	if (arg[0] && IsCharNumeric(arg[0]))
		return ItemNameOf(StringToInt(arg), out, maxlen);
	strcopy(out, maxlen, arg);
	return arg[0] != '\0';
}

/** The stock weapon a class carries in a weapon slot ("" if none). */
bool StockFor(int cls, int slot, char[] out, int maxlen)
{
	ItemInfo it;
	for (int i = 0; i < g_Items.Length; i++)
	{
		g_Items.GetArray(i, it);
		if (it.isStock && it.slot == slot && (it.mask & (1 << cls)) && StrContains(it.cls, "tf_weapon_") == 0)
		{
			strcopy(out, maxlen, it.name);
			return true;
		}
	}
	return false;
}

/* ------------------------------------------------------------------ */
/* Giving and stripping                                               */
/* ------------------------------------------------------------------ */

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

/**
 * Builds and equips a weapon entity directly (needs this repo's SDKTools gamedata). Used for
 * the Civilian, for whom the schema's item system refuses every item (none lists class 10).
 */
bool EquipWeaponEntity(int client, const char[] name)
{
	ItemInfo it;
	if (!ItemByName(name, it) || StrContains(it.cls, "tf_weapon_") != 0)
		return false;
	int max = GetEntPropArraySize(client, Prop_Send, "m_hMyWeapons");
	for (int i = 0; i < max; i++)
	{
		int have = GetEntPropEnt(client, Prop_Send, "m_hMyWeapons", i);
		if (have > MaxClients && GetEntProp(have, Prop_Send, "m_iItemDefinitionIndex") == it.def)
			return true;    // already holds it (inventory events can repeat)
	}
	int w = CreateEntityByName(it.cls);
	if (w == -1)
		return false;
	SetEntProp(w, Prop_Send, "m_iItemDefinitionIndex", it.def);
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

/** Removes the player's cosmetics: head items, and misc items too unless headOnly. */
int StripCosmetics(int client, bool headOnly)
{
	int n = 0, ent = -1;
	ItemInfo it;
	while ((ent = FindEntityByClassname(ent, "tf_wearable*")) != -1)
	{
		if (GetEntPropEnt(ent, Prop_Send, "m_hOwnerEntity") != client || !HasEntProp(ent, Prop_Send, "m_iItemDefinitionIndex"))
			continue;
		if (!ItemByDef(GetEntProp(ent, Prop_Send, "m_iItemDefinitionIndex"), it))
			continue;
		if (it.slot == SLOT_HEAD || (!headOnly && it.slot == SLOT_MISC))
		{
			RemoveEntity(ent);
			n++;
		}
	}
	return n;
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

void RemoveSlot(int client, int slot)
{
	int w;
	while ((w = GetPlayerWeaponSlot(client, slot)) > MaxClients)
	{
		RemovePlayerItem(client, w);
		RemoveEntity(w);
	}
}

/** Wearable weapons (Gunboats, Razorback, shields, boots...) are not in a weapon slot: find
 *  the player's wearable whose schema slot is <slot>; with def >= 0, only that item. */
int SlotWearable(int client, int slot, int def = -1)
{
	int e = -1;
	ItemInfo it;
	while ((e = FindEntityByClassname(e, "tf_wearable*")) != -1)
	{
		if (GetEntPropEnt(e, Prop_Send, "m_hOwnerEntity") != client)
			continue;
		int d = GetEntProp(e, Prop_Send, "m_iItemDefinitionIndex");
		if ((def < 0 || d == def) && ItemByDef(d, it) && it.slot == slot)
			return e;
	}
	return -1;
}

void RemoveSlotWearables(int client, int slot)
{
	int e;
	while ((e = SlotWearable(client, slot)) != -1)
		RemoveEntity(e);
}

int StripWeapons(int client)
{
	int n = 0;
	for (int s = 0; s < 6; s++)
	{
		if (GetPlayerWeaponSlot(client, s) > MaxClients)
			n++;
		RemoveSlot(client, s);
	}
	return n;
}

/* ------------------------------------------------------------------ */
/* Weapon rules: whitelist, force stock, unlock picks                 */
/* ------------------------------------------------------------------ */

public void Cvar_Whitelist(ConVar cv, const char[] o, const char[] n)
{
	LoadWhitelist();
}

void LoadWhitelist()
{
	delete g_Whitelist;
	g_Whitelist = new StringMap();
	char rel[PLATFORM_MAX_PATH], path[PLATFORM_MAX_PATH], line[128], key[12];
	g_cvWhitelistFile.GetString(rel, sizeof(rel));
	BuildPath(Path_SM, path, sizeof(path), "%s", rel);
	File f = OpenFile(path, "r");
	if (f == null)
		return;
	while (f.ReadLine(line, sizeof(line)))
	{
		int c = StrContains(line, "//");
		if (c >= 0)
			line[c] = '\0';
		TrimString(line);
		if (!line[0])
			continue;
		ItemInfo it;
		if ((IsCharNumeric(line[0]) && ItemByDef(StringToInt(line), it)) || ItemByName(line, it))
		{
			IntToString(it.def, key, sizeof(key));
			g_Whitelist.SetValue(key, true);
		}
	}
	delete f;
}

public Action Cmd_WhitelistReload(int client, int args)
{
	LoadWhitelist();
	ReplyToCommand(client, "[SM] weapon whitelist: %d item(s)", g_Whitelist.Size);
	return Plugin_Handled;
}

bool WeaponAllowed(int def)
{
	ItemInfo it;
	if (!ItemByDef(def, it) || it.isStock)
		return true;
	if (g_cvForceStock.BoolValue)
		return false;
	if (g_cvWhitelist.BoolValue)
	{
		char key[12];
		bool v;
		IntToString(def, key, sizeof(key));
		return g_Whitelist.GetValue(key, v);
	}
	return true;
}

/** Unlock pick for (class, slot) from the player's cookie, or 0. */
int PickFor(int client, int cls, int slot)
{
	char buf[512], want[16];
	strcopy(buf, sizeof(buf), g_sPicks[client]);
	FormatEx(want, sizeof(want), "c%ds%d=", cls, slot);
	int p = StrContains(buf, want);
	return p >= 0 ? StringToInt(buf[p + strlen(want)]) : 0;
}

void SetPick(int client, int cls, int slot, int def)
{
	char buf[512], out[512], want[16], parts[32][24];
	strcopy(buf, sizeof(buf), g_sPicks[client]);
	FormatEx(want, sizeof(want), "c%ds%d=", cls, slot);
	int n = ExplodeString(buf, ";", parts, sizeof(parts), sizeof(parts[]));
	for (int i = 0; i < n; i++)
	{
		if (parts[i][0] && StrContains(parts[i], want) != 0)
			Format(out, sizeof(out), "%s%s;", out, parts[i]);
	}
	if (def > 0)
		Format(out, sizeof(out), "%s%s%d;", out, want, def);
	strcopy(g_sPicks[client], sizeof(g_sPicks[]), out);
	if (!IsFakeClient(client) && AreClientCookiesCached(client))
		g_ckPicks.Set(client, out);
}

bool UnlocksOn(int client)
{
	return g_cvGiveMeAll.BoolValue && (!IsFakeClient(client) || g_cvDebugBots.BoolValue);
}

void GrCookiesCached(int client)
{
	g_ckPicks.Get(client, g_sPicks[client], sizeof(g_sPicks[]));
}

public Action Cmd_SetPick(int client, int args)
{
	char pattern[64], tname[64];
	GetCmdArg(1, pattern, sizeof(pattern));
	int slot = GetCmdArgInt(2), def = GetCmdArgInt(3);
	int targets[MAXPLAYERS];
	bool ml;
	int n = ProcessTargetString(pattern, client, targets, sizeof(targets), 0, tname, sizeof(tname), ml);
	for (int i = 0; i < n; i++)
	{
		SetPick(targets[i], GetEntProp(targets[i], Prop_Send, "m_iClass"), slot, def);
	}
	ReplyToCommand(client, "[SM] pick set on %d player(s)", n);
	return Plugin_Handled;
}

/** Unlock picks first, then the rules: non-allowed weapons become the slot's stock weapon. */
void ApplyWeaponRules(int client)
{
	int cls = GetEntProp(client, Prop_Send, "m_iClass");
	if (cls < 1 || cls > 9)
		return;
	char stockName[64], name[64];
	if (UnlocksOn(client))
	{
		for (int slot = 0; slot < 3; slot++)
		{
			int def = PickFor(client, cls, slot);
			int w = GetPlayerWeaponSlot(client, slot);
			ItemInfo pick;
			if (def <= 0 || !ItemByDef(def, pick) || !(pick.mask & (1 << cls)) || pick.slot != slot || !WeaponAllowed(def))
				continue;
			if (StrContains(pick.cls, "tf_wearable") == 0)
			{
				if (SlotWearable(client, slot, def) == -1 && ItemNameOf(def, name, sizeof(name)))
				{
					RemoveSlot(client, slot);
					RemoveSlotWearables(client, slot);
					GiveItemName(client, name);
				}
				continue;
			}
			if ((w <= MaxClients || GetEntProp(w, Prop_Send, "m_iItemDefinitionIndex") != def) && ItemNameOf(def, name, sizeof(name)))
			{
				RemoveSlot(client, slot);
				RemoveSlotWearables(client, slot);
				GiveItemName(client, name);
			}
		}
	}
	if (!g_cvForceStock.BoolValue && !g_cvWhitelist.BoolValue)
		return;
	for (int slot = 0; slot < 3; slot++)
	{
		int w = GetPlayerWeaponSlot(client, slot);
		if (w <= MaxClients || WeaponAllowed(GetEntProp(w, Prop_Send, "m_iItemDefinitionIndex")))
			continue;
		RemoveSlot(client, slot);
		if (StockFor(cls, slot, stockName, sizeof(stockName)))
			GiveItemName(client, stockName);
	}
}

public Action Cmd_GiveMeAll(int client, int args)
{
	if (!client)
		return Plugin_Handled;
	if (!g_cvGiveMeAll.BoolValue)
	{
		ReplyToCommand(client, "[SM] !givemeall is not enabled on this server.");
		return Plugin_Handled;
	}
	PrintToChat(client, "[SM] Every Gold Rush unlock is available to you on this server. Pick them here or with !unlocks.");
	ShowUnlockMenu(client);
	return Plugin_Handled;
}

public Action Cmd_Unlocks(int client, int args)
{
	if (!client)
		return Plugin_Handled;
	if (!UnlocksOn(client))
	{
		ReplyToCommand(client, "[SM] Unlocks are not enabled on this server.");
		return Plugin_Handled;
	}
	ShowUnlockMenu(client);
	return Plugin_Handled;
}

void ShowUnlockMenu(int client)
{
	int cls = GetEntProp(client, Prop_Send, "m_iClass");
	if (cls < 1 || cls > 9)
	{
		PrintToChat(client, "[SM] Pick one of the nine classes first.");
		return;
	}
	Menu m = new Menu(Menu_UnlockSlot);
	m.SetTitle("Unlocks for this class: choose a slot");
	static const char slotNames[][] = { "Primary", "Secondary", "Melee" };
	char info[8], label[96], name[64];
	for (int slot = 0; slot < 3; slot++)
	{
		int def = PickFor(client, cls, slot);
		if (def <= 0 || !ItemNameOf(def, name, sizeof(name)))
			strcopy(name, sizeof(name), "your loadout");
		FormatEx(label, sizeof(label), "%s: %s", slotNames[slot], name);
		IntToString(slot, info, sizeof(info));
		m.AddItem(info, label);
	}
	m.Display(client, 30);
}

public int Menu_UnlockSlot(Menu m, MenuAction action, int client, int item)
{
	if (action == MenuAction_End)
		delete m;
	else if (action == MenuAction_Select)
	{
		char info[8];
		m.GetItem(item, info, sizeof(info));
		ShowUnlockChoices(client, StringToInt(info));
	}
	return 0;
}

void ShowUnlockChoices(int client, int slot)
{
	int cls = GetEntProp(client, Prop_Send, "m_iClass");
	Menu m = new Menu(Menu_UnlockPick);
	m.SetTitle("Choose a weapon");
	char info[24];
	FormatEx(info, sizeof(info), "%d:0", slot);
	m.AddItem(info, "Your own loadout");
	ItemInfo it;
	for (int i = 0; i < g_Items.Length; i++)
	{
		g_Items.GetArray(i, it);
		if (it.isStock || it.slot != slot || !(it.mask & (1 << cls)) || (StrContains(it.cls, "tf_weapon_") != 0 && StrContains(it.cls, "tf_wearable") != 0) || !WeaponAllowed(it.def))
			continue;
		FormatEx(info, sizeof(info), "%d:%d", slot, it.def);
		m.AddItem(info, it.name);
	}
	m.ExitBackButton = true;
	m.Display(client, 30);
}

public int Menu_UnlockPick(Menu m, MenuAction action, int client, int item)
{
	if (action == MenuAction_End)
		delete m;
	else if (action == MenuAction_Cancel && item == MenuCancel_ExitBack)
		ShowUnlockMenu(client);
	else if (action == MenuAction_Select)
	{
		char info[24], parts[2][12];
		m.GetItem(item, info, sizeof(info));
		ExplodeString(info, ":", parts, 2, 12);
		int cls = GetEntProp(client, Prop_Send, "m_iClass");
		int slot = StringToInt(parts[0]), def = StringToInt(parts[1]);
		SetPick(client, cls, slot, def);
		if (def > 0 && IsPlayerAlive(client))
		{
			ApplyWeaponRules(client);
			PrintToChat(client, "[SM] Equipped. It stays picked for this class (spawns and resupply).");
		}
		else
			PrintToChat(client, "[SM] Saved. Your own loadout returns on your next spawn or resupply.");
		ShowUnlockMenu(client);
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* Class 10                                                           */
/* ------------------------------------------------------------------ */

/** Class 10 cannot be joined through the class menu: write the desired class and respawn,
 *  which is where TF2's respawn code applies a class. */
void SetClassEx(int client, int cls, bool respawn)
{
	g_iPinnedClass[client] = cls;
	SetEntProp(client, Prop_Send, "m_iDesiredPlayerClass", cls);
	if (respawn && GetClientTeam(client) >= 2)
		RunScript(client, "self.ForceRespawn()");
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
	if (StrEqual(arg, "heavyweapons", false))
		return 6;
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

/** joinclass civilian works when the Civilian is enabled; any other class drops the pin. */
public Action Listener_GrJoinClass(int client, const char[] command, int argc)
{
	if (!client)
		return Plugin_Continue;
	char arg[32];
	GetCmdArg(1, arg, sizeof(arg));
	if (ClassFromArg(arg) == 10)
	{
		if (!g_cvCivEnable.BoolValue)
			return Plugin_Continue;
		SetClassEx(client, 10, IsPlayerAlive(client));
		return Plugin_Handled;
	}
	if (!IsFakeClient(client))
		g_iPinnedClass[client] = 0;
	return Plugin_Continue;
}

void CivStats(int client)
{
	int hp = g_cvCivHealth.IntValue;
	float speed = g_cvCivSpeed.FloatValue;
	if (hp <= 0 && g_cvCivLaunch.BoolValue)
		hp = LAUNCH_CIV_HEALTH;
	if (speed <= 0.0 && g_cvCivLaunch.BoolValue)
		speed = LAUNCH_CIV_SPEED;
	char code[192];
	if (hp > 0 && hp != GR_CLASS_HEALTH)
	{
		FormatEx(code, sizeof(code), "self.AddCustomAttribute(\"max health additive bonus\", %d.0, -1); self.SetHealth(%d)", hp - GR_CLASS_HEALTH, hp);
		RunScript(client, code);
		g_bCivAttrs[client] = true;
	}
	if (speed > 0.0 && speed != GR_CLASS_SPEED)
	{
		FormatEx(code, sizeof(code), "self.AddCustomAttribute(\"move speed bonus\", %f, -1)", speed / GR_CLASS_SPEED);
		RunScript(client, code);
		g_bCivAttrs[client] = true;
	}
}

/* ------------------------------------------------------------------ */
/* Admin commands                                                     */
/* ------------------------------------------------------------------ */

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
	char filter[64];
	GetCmdArg(1, filter, sizeof(filter));
	static const char slotNames[][] = { "primary", "secondary", "melee" };
	int shown = 0;
	ItemInfo it;
	for (int i = 0; i < g_Items.Length; i++)
	{
		g_Items.GetArray(i, it);
		if (filter[0] && StrContains(it.name, filter, false) == -1)
			continue;
		ReplyToCommand(client, "%6d  %-9s %s %s", it.def, it.slot >= 0 && it.slot <= 2 ? slotNames[it.slot] : (it.slot == SLOT_HEAD ? "head" : (it.slot == SLOT_MISC ? "misc" : "other")),
			it.isStock ? "stock" : "     ", it.name);
		shown++;
	}
	ReplyToCommand(client, "%d item(s)", shown);
	return Plugin_Handled;
}

/* ------------------------------------------------------------------ */
/* Spawn / inventory                                                  */
/* ------------------------------------------------------------------ */

public void Event_GrInventory(Event event, const char[] name, bool dontBroadcast)
{
	int client = GetClientOfUserId(event.GetInt("userid"));
	if (client && GetEngineTime() - g_flOwnGive[client] > 0.25)
		RequestFrame(Frame_GrInventory, GetClientUserId(client));
}

/** A class set by an admin, !civilian or the bot chance sticks (bots re-pick on respawn). */
public void Event_GrSpawn(Event event, const char[] name, bool dontBroadcast)
{
	int client = GetClientOfUserId(event.GetInt("userid"));
	if (!client || GetClientTeam(client) < 2)
		return;
	g_bHatRolled[client] = false;
	// post_inventory_application does not always reach us (Gold Rush's Regenerate does not fire
	// it): apply the rules a moment after every spawn as well.
	CreateTimer(0.2, Timer_GrApply, GetClientUserId(client), TIMER_FLAG_NO_MAPCHANGE);
	CreateTimer(1.0, Timer_GrApply, GetClientUserId(client), TIMER_FLAG_NO_MAPCHANGE);   // late loadout items
	if (IsFakeClient(client) && g_iPinnedClass[client] == 0)
		BotCivSpawn(client);
	int pin = g_iPinnedClass[client];
	if (pin > 0 && GetEntProp(client, Prop_Send, "m_iClass") != pin)
	{
		SetEntProp(client, Prop_Send, "m_iDesiredPlayerClass", pin);
		RequestFrame(Frame_GrRespawn, GetClientUserId(client));
	}
}

/** Bot Civilian chance, per life. Pinning a bot to class 10 makes TFBot fight it (it re-picks
 *  its class, suicides, gets forced back, forever), so the bot is respawned as the Civilian
 *  once and its desired class is then set back to the class it chose itself: the AI is
 *  content, and its next life is that class again (and rolls again). */
void BotCivSpawn(int client)
{
	int cls = GetEntProp(client, Prop_Send, "m_iClass");
	g_bBotCivLife[client] = false;
	if (g_iBotCivReturn[client] > 0)
	{
		if (cls == 10)
		{
			SetEntProp(client, Prop_Send, "m_iDesiredPlayerClass", g_iBotCivReturn[client]);
			g_bBotCivLife[client] = true;
			g_iBotCivTeam[client] = GetClientTeam(client);
		}
		g_iBotCivReturn[client] = 0;
		return;
	}
	if (cls < 1 || cls > 9 || !g_cvCivEnable.BoolValue || GetURandomFloat() >= g_cvCivBotChance.FloatValue)
		return;
	g_iBotCivReturn[client] = cls;
	SetEntProp(client, Prop_Send, "m_iDesiredPlayerClass", 10);
	RequestFrame(Frame_GrRespawn, GetClientUserId(client));
}

/** TFBot still switches back to its own class during a Civilian life: HandleCommand_JoinClass
 *  calls CommitSuicide, which sets health to 0 and kills directly (no TakeDamage), so it is
 *  hooked with DHooks (vtable offset from our sdktools gamedata). For a rolled Civilian bot the
 *  call is skipped; its desired class is already its own, so its next real death respawns it
 *  normally. A team change also uses CommitSuicide: if the team changed, the bot is allowed to
 *  die a frame later. */
DynamicHook g_hCommitSuicide;

void BotCivHookStart()
{
	GameData gd = new GameData("sdktools.games");
	int off = gd == null ? -1 : gd.GetOffset("CommitSuicide");
	delete gd;
	if (off < 0)
	{
		LogError("CommitSuicide offset missing: bot Civilians will not stay Civilian.");
		return;
	}
	g_hCommitSuicide = new DynamicHook(off, HookType_Entity, ReturnType_Void, ThisPointer_CBaseEntity);
	g_hCommitSuicide.AddParam(HookParamType_Bool);    // bExplode
	g_hCommitSuicide.AddParam(HookParamType_Bool);    // bForce
}

void BotCivHookClient(int client)
{
	if (g_hCommitSuicide != null && IsFakeClient(client))
		g_hCommitSuicide.HookEntity(Hook_Pre, client, DH_BotCommitSuicide);
}

public MRESReturn DH_BotCommitSuicide(int client)
{
	if (client < 1 || client > MaxClients || !g_bBotCivLife[client] || g_bBotCivAllowDeath[client])
		return MRES_Ignored;
	RequestFrame(Frame_BotCivTeamCheck, GetClientUserId(client));
	return MRES_Supercede;
}

public void Frame_BotCivTeamCheck(int userid)
{
	int client = GetClientOfUserId(userid);
	if (client && IsPlayerAlive(client) && g_bBotCivLife[client] && GetClientTeam(client) != g_iBotCivTeam[client])
	{
		g_bBotCivAllowDeath[client] = true;
		ForcePlayerSuicide(client);
		g_bBotCivAllowDeath[client] = false;
	}
}

public Action Timer_GrApply(Handle timer, int userid)
{
	Frame_GrInventory(userid);
	return Plugin_Stop;
}

/** Resupply lockers: apply the rules after a touch (Regenerate may not fire the inventory event). */
public void OnEntityCreated(int entity, const char[] classname)
{
	if (StrEqual(classname, "func_regenerate"))
		SDKHook(entity, SDKHook_StartTouchPost, Hook_GrRegenTouch);
}

public void Hook_GrRegenTouch(int entity, int other)
{
	if (other >= 1 && other <= MaxClients && IsClientInGame(other))
		CreateTimer(0.15, Timer_GrApply, GetClientUserId(other), TIMER_FLAG_NO_MAPCHANGE);
}

public void Frame_GrRespawn(int userid)
{
	int client = GetClientOfUserId(userid);
	if (client && IsPlayerAlive(client))
		RunScript(client, "self.ForceRespawn()");
}

public void OnClientDisconnect(int client)
{
	g_iPinnedClass[client] = 0;
	g_bCivAttrs[client] = false;
	g_iBotCivReturn[client] = 0;
	g_bBotCivLife[client] = false;
	g_sPicks[client][0] = '\0';
	FovDisconnect(client);
}

public void Frame_GrInventory(int userid)
{
	int client = GetClientOfUserId(userid);
	if (!client || !IsPlayerAlive(client))
		return;
	FovApply(client);
	if (!g_cvHats.BoolValue)
		StripCosmetics(client, false);
	else if (!g_cvHeadgear.BoolValue)
		StripCosmetics(client, true);
	else if (IsFakeClient(client) && !g_bHatRolled[client])
	{
		g_bHatRolled[client] = true;
		if (GetURandomFloat() < g_cvBotHats.FloatValue)
			GiveRandomHat(client);
	}
	int cls = GetEntProp(client, Prop_Send, "m_iClass");
	if (cls != 10)
	{
		// Civilian health/speed attributes stay on the player entity: drop them on any other class.
		if (g_bCivAttrs[client])
		{
			RunScript(client, "self.RemoveCustomAttribute(\"max health additive bonus\"); self.RemoveCustomAttribute(\"move speed bonus\"); self.SetHealth(self.GetMaxHealth())");
			g_bCivAttrs[client] = false;
		}
		ApplyWeaponRules(client);
		return;
	}
	char model[PLATFORM_MAX_PATH], list[512], parts[16][64], item[64];
	g_cvCivModel.GetString(model, sizeof(model));
	if (model[0])
	{
		if (!IsModelPrecached(model))
			PrecacheModel(model, true);
		SetVariantString(model);
		AcceptEntityInput(client, "SetCustomModelWithClassAnimations");
	}
	CivStats(client);
	g_cvCivLoadout.GetString(list, sizeof(list));
	int n = ExplodeString(list, ",", parts, sizeof(parts), sizeof(parts[]));
	for (int i = 0; i < n; i++)
	{
		TrimString(parts[i]);
		if (ResolveItem(parts[i], item, sizeof(item)))
			GiveItemName(client, item);
	}
}

void GiveRandomHat(int client)
{
	int cls = GetEntProp(client, Prop_Send, "m_iClass");
	ArrayList fit = new ArrayList();
	ItemInfo it;
	for (int i = 0; i < g_Items.Length; i++)
	{
		g_Items.GetArray(i, it);
		if (it.slot == SLOT_HEAD && (cls == 10 || it.mask == 0 || (it.mask & (1 << cls))))
			fit.Push(i);
	}
	if (fit.Length)
	{
		g_Items.GetArray(fit.Get(GetRandomInt(0, fit.Length - 1)), it);
		GiveItemName(client, it.name);
	}
	delete fit;
}

/* ------------------------------------------------------------------ */
/* Natives                                                            */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* Civilian pain/death sounds                                          */
/* ------------------------------------------------------------------ */

/*
 * Launch TF2's Civilian (tf_port) has no death sounds in its class script, so it uses the
 * generic Player.Death set; Gold Rush's (TF2's current civilian.ctx) uses Scout voice lines.
 * Mode 1: TF2's generic set every client has (player/pain.wav, player/death.wav).
 * Mode 2: launch TF2's generic set (player/pain6-9.wav, melee death player/death3/5/6/7.wav),
 *         which needs those files on the server and its FastDL (added to downloads if present).
 */
ConVar g_cvCivSounds;
float g_flNextCivPain[MAXPLAYERS + 1], g_flCivVoiceAt[MAXPLAYERS + 1];
bool g_bCivSoundEmit;
static const char g_sGenPain1[][] = { "player/pain.wav" };
static const char g_sGenDeath1[][] = { "player/death.wav" };
static const char g_sGenPain2[][] = { "player/pain6.wav", "player/pain7.wav", "player/pain8.wav", "player/pain9.wav" };
static const char g_sGenDeath2[][] = { "player/death3.wav", "player/death5.wav", "player/death6.wav", "player/death7.wav" };

void CivSoundsStart()
{
	g_cvCivSounds = CreateConVar("tf2goldrush_civilian_generic_sounds", "1", "Civilian pain/death sounds: 0 = class script (Scout voice), 1 = TF2's generic player sounds, 2 = launch TF2's generic sounds (tf_port; needs player/pain6-9 and death3/5/6/7 on the server and FastDL).", _, true, 0.0, true, 2.0);
	AddNormalSoundHook(Hook_CivSound);
	HookEvent("player_hurt", Event_CivHurt);
	HookEvent("player_death", Event_CivDeath);
}

void CivSoundsMapStart()
{
	for (int i = 0; i < sizeof(g_sGenPain1); i++) PrecacheSound(g_sGenPain1[i], true);
	for (int i = 0; i < sizeof(g_sGenDeath1); i++) PrecacheSound(g_sGenDeath1[i], true);
	char path[PLATFORM_MAX_PATH];
	for (int i = 0; i < sizeof(g_sGenPain2); i++)
	{
		PrecacheSound(g_sGenPain2[i], true);
		FormatEx(path, sizeof(path), "sound/%s", g_sGenPain2[i]);
		if (FileExists(path))
			AddFileToDownloadsTable(path);
	}
	for (int i = 0; i < sizeof(g_sGenDeath2); i++)
	{
		PrecacheSound(g_sGenDeath2[i], true);
		FormatEx(path, sizeof(path), "sound/%s", g_sGenDeath2[i]);
		if (FileExists(path))
			AddFileToDownloadsTable(path);
	}
}

void GenericSound(bool death, bool critOrMelee, char[] out, int maxlen)
{
	if (g_cvCivSounds.IntValue == 2)
	{
		if (death && critOrMelee)
			strcopy(out, maxlen, g_sGenDeath2[GetRandomInt(0, sizeof(g_sGenDeath2) - 1)]);
		else
			strcopy(out, maxlen, g_sGenPain2[GetRandomInt(0, sizeof(g_sGenPain2) - 1)]);
	}
	else
		strcopy(out, maxlen, (death && critOrMelee) ? g_sGenDeath1[0] : g_sGenPain1[0]);
}

bool IsCivilianPlayer(int entity)
{
	return entity >= 1 && entity <= MaxClients && IsClientInGame(entity) && GetEntProp(entity, Prop_Send, "m_iClass") == 10;
}

/** The Civilian's voice (Scout pain/death lines from the class script) becomes the generic set. */
public Action Hook_CivSound(int clients[MAXPLAYERS], int &numClients, char sample[PLATFORM_MAX_PATH], int &entity, int &channel, float &volume, int &level, int &pitch, int &flags, char soundEntry[PLATFORM_MAX_PATH], int &seed)
{
	if (g_bCivSoundEmit || g_cvCivSounds.IntValue == 0 || !IsCivilianPlayer(entity))
		return Plugin_Continue;
	int start = 0;
	while (sample[start] == ')' || sample[start] == '^' || sample[start] == '*' || sample[start] == '#' || sample[start] == '<' || sample[start] == '>')
		start++;
	if (StrContains(sample[start], "vo/", false) != 0)
		return Plugin_Continue;
	bool death = !IsPlayerAlive(entity);
	bool crit = StrContains(sample, "CrticialDeath", false) != -1;
	GenericSound(death, crit, sample, sizeof(sample));
	g_flCivVoiceAt[entity] = GetEngineTime();
	level = 75;     // SNDLVL_NORM, as the generic Player.Death sounds
	return Plugin_Changed;
}

/** Hurt Civilians make a generic pain sound (the class has no pain responses of its own). */
public void Event_CivHurt(Event event, const char[] name, bool dontBroadcast)
{
	int client = GetClientOfUserId(event.GetInt("userid"));
	if (!client || g_cvCivSounds.IntValue == 0 || !IsCivilianPlayer(client) || event.GetInt("health") <= 0)
		return;
	float now = GetEngineTime();
	if (now < g_flNextCivPain[client] || now - g_flCivVoiceAt[client] < 0.3)
		return;
	g_flNextCivPain[client] = now + 0.8;
	char snd[PLATFORM_MAX_PATH];
	GenericSound(false, false, snd, sizeof(snd));
	g_bCivSoundEmit = true;
	EmitSoundToAll(snd, client, SNDCHAN_VOICE, 75);
	g_bCivSoundEmit = false;
}

/**
 * The Civilian dies silently on Gold Rush (TF2's DeathSound uses the class's death sound
 * scripts, which are only precached for the nine classes). Play the generic death sound the
 * way CTFPlayer::DeathSound picks it: crit and melee deaths use the death set, others the pain set.
 */
public void Event_CivDeath(Event event, const char[] name, bool dontBroadcast)
{
	int client = GetClientOfUserId(event.GetInt("userid"));
	if (!client || g_cvCivSounds.IntValue == 0 || !IsCivilianPlayer(client) || (event.GetInt("death_flags") & 32))
		return;
	if (GetEngineTime() - g_flCivVoiceAt[client] < 0.3)
		return;     // a rewritten death line already played
	int bits = event.GetInt("damagebits");
	bool critOrMelee = (bits & (1 << 20)) != 0 || (bits & (1 << 7)) != 0 || event.GetInt("customkill") == 1;   // DMG_CRITICAL, DMG_CLUB
	char snd[PLATFORM_MAX_PATH];
	GenericSound(true, critOrMelee, snd, sizeof(snd));
	g_bCivSoundEmit = true;
	EmitSoundToAll(snd, client, SNDCHAN_VOICE, 75);
	g_bCivSoundEmit = false;
}
