/**
 * TF2Attributes for TF2: Gold Rush
 *
 * Provides the TF2Attributes natives (TF2Attrib_*, library "tf2attributes") on
 * TF2: Gold Rush, so plugins written for TF2Attributes run after a recompile.
 *
 * TF2: Gold Rush exports none of the attribute functions TF2Attributes calls
 * (GetAttributeDefinitionByName, SetRuntimeAttributeValue, ...), and its x64
 * build rules out the raw-pointer approach. Instead:
 *  - Items: VScript AddAttribute / RemoveAttribute / GetAttribute /
 *    AddAttributeString / ReapplyProvision on the econ entity.
 *  - Players: VScript AddCustomAttribute / RemoveCustomAttribute /
 *    GetCustomAttribute / AddCustomAttributeString.
 *  - Names, IDs, integer flags and static item attributes: the item schema
 *    (scripts/items/custom_items_game.txt when present, which #bases
 *    items_game.txt; otherwise items_game.txt), read through the game's file
 *    system.
 *  - The Address-returning natives return tokens that the Get/Set natives
 *    understand; values are read and written live.
 *
 * Not possible on TF2: Gold Rush: TF2Attrib_HookValue* (return the initial
 * value), refundable currency, UnsafeGetStringValue, and SOC attributes
 * (TF2: Gold Rush has no item server). Each logs once when first used.
 */
#include <sourcemod>
#include <sdktools>

#pragma semicolon 1
#pragma newdecls required

#define PLUGIN_VERSION "1.0.0"
#define ABSENT         -91234.5
#define MAX_TOKENS     4096

public Plugin myinfo =
{
	name        = "TF2Attributes (TF2: Gold Rush)",
	author      = "AgitationSkeleton",
	description = "TF2Attributes natives for TF2: Gold Rush, implemented with VScript and the item schema.",
	version     = PLUGIN_VERSION,
	url         = "https://github.com/AgitationSkeleton/SM-TF2Classified-Tools"
};

StringMap g_smNameToId;      // lowercase attribute name -> defindex
StringMap g_smIdToName;      // "defindex" -> name
StringMap g_smIdInteger;     // "defindex" -> stored_as_integer
StringMap g_smItemAttrs;     // "item defindex" -> ArrayList of {defindex, value}
StringMap g_smTouched;       // "entref" -> ArrayList of defindices set through us
StringMap g_smWarned;
bool g_bSchema;
int g_iTokEnt[MAX_TOKENS];
int g_iTokDef[MAX_TOKENS];
int g_iNextTok = 1;

public APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int err_max)
{
	char game[32];
	GetGameFolderName(game, sizeof(game));
	if (!StrEqual(game, "tf_goldrush", false))
	{
		strcopy(error, err_max, "This TF2Attributes build only runs on TF2: Gold Rush.");
		return APLRes_Failure;
	}
	CreateNative("TF2Attrib_SetByName", Native_SetByName);
	CreateNative("TF2Attrib_SetByDefIndex", Native_SetByDefIndex);
	CreateNative("TF2Attrib_SetFromStringValue", Native_SetFromStringValue);
	CreateNative("TF2Attrib_GetByName", Native_GetByName);
	CreateNative("TF2Attrib_GetByDefIndex", Native_GetByDefIndex);
	CreateNative("TF2Attrib_RemoveByName", Native_RemoveByName);
	CreateNative("TF2Attrib_RemoveByDefIndex", Native_RemoveByDefIndex);
	CreateNative("TF2Attrib_RemoveAll", Native_RemoveAll);
	CreateNative("TF2Attrib_ClearCache", Native_ClearCache);
	CreateNative("TF2Attrib_SetDefIndex", Native_SetDefIndex);
	CreateNative("TF2Attrib_GetDefIndex", Native_GetDefIndex);
	CreateNative("TF2Attrib_SetValue", Native_SetValue);
	CreateNative("TF2Attrib_GetValue", Native_GetValue);
	CreateNative("TF2Attrib_UnsafeGetStringValue", Native_UnsafeGetStringValue);
	CreateNative("TF2Attrib_SetRefundableCurrency", Native_SetRefundableCurrency);
	CreateNative("TF2Attrib_GetRefundableCurrency", Native_GetRefundableCurrency);
	CreateNative("TF2Attrib_ListDefIndices", Native_ListDefIndices);
	CreateNative("TF2Attrib_GetStaticAttribs", Native_GetStaticAttribs);
	CreateNative("TF2Attrib_GetSOCAttribs", Native_GetSOCAttribs);
	CreateNative("TF2Attrib_IsIntegerValue", Native_IsIntegerValue);
	CreateNative("TF2Attrib_IsValidAttributeName", Native_IsValidAttributeName);
	CreateNative("TF2Attrib_AddCustomPlayerAttribute", Native_AddCustomPlayerAttribute);
	CreateNative("TF2Attrib_RemoveCustomPlayerAttribute", Native_RemoveCustomPlayerAttribute);
	CreateNative("TF2Attrib_HookValueFloat", Native_HookValueFloat);
	CreateNative("TF2Attrib_HookValueInt", Native_HookValueInt);
	CreateNative("TF2Attrib_HookValueString", Native_HookValueString);
	CreateNative("TF2Attrib_IsReady", Native_IsReady);
	RegPluginLibrary("tf2attributes");
	return APLRes_Success;
}

public void OnPluginStart()
{
	CreateConVar("tf2attributes_goldrush_version", PLUGIN_VERSION, "TF2Attributes (TF2: Gold Rush) version.", FCVAR_NOTIFY | FCVAR_DONTRECORD);
	g_smTouched = new StringMap();
	g_smWarned = new StringMap();
	RegAdminCmd("sm_tf2attrib_reload", Cmd_Reload, ADMFLAG_CONFIG, "Re-read the item schema (after sv_reload_item_schema).");
	LoadSchema();
}

public void OnMapStart()
{
	// Per-map schemas (scripts/items/<map>_items_game.txt) can change per map.
	LoadSchema();
}

public Action Cmd_Reload(int client, int args)
{
	LoadSchema();
	int n = g_smNameToId != null ? g_smNameToId.Size : 0;
	ReplyToCommand(client, "[TF2Attrib] schema %s: %d attributes", g_bSchema ? "loaded" : "NOT loaded", n);
	return Plugin_Handled;
}

/* ------------------------------------------------------------------ */
/* Schema                                                             */
/* ------------------------------------------------------------------ */

void LoadSchema()
{
	delete g_smNameToId;
	delete g_smIdToName;
	delete g_smIdInteger;
	if (g_smItemAttrs != null)
	{
		StringMapSnapshot snap = g_smItemAttrs.Snapshot();
		char key[16];
		for (int i = 0; i < snap.Length; i++)
		{
			snap.GetKey(i, key, sizeof(key));
			ArrayList a;
			g_smItemAttrs.GetValue(key, a);
			delete a;
		}
		delete snap;
		delete g_smItemAttrs;
	}
	g_smNameToId = new StringMap();
	g_smIdToName = new StringMap();
	g_smIdInteger = new StringMap();
	g_smItemAttrs = new StringMap();
	g_bSchema = false;

	// The game picks one schema: per-map, then custom (which #bases the
	// stock one), then stock. KeyValues files load through the game's file
	// system (VPKs included) and resolve #base.
	char map[PLATFORM_MAX_PATH], path[PLATFORM_MAX_PATH];
	GetCurrentMap(map, sizeof(map));
	KeyValues kv = new KeyValues("items_game");
	FormatEx(path, sizeof(path), "scripts/items/%s_items_game.txt", map);
	bool ok = FileExists(path, true) && kv.ImportFromFile(path);
	if (!ok)
	{
		delete kv;
		kv = new KeyValues("items_game");
		ok = FileExists("scripts/items/custom_items_game.txt", true) && kv.ImportFromFile("scripts/items/custom_items_game.txt");
	}
	if (!ok)
	{
		delete kv;
		kv = new KeyValues("items_game");
		ok = kv.ImportFromFile("scripts/items/items_game.txt");
	}
	if (!ok)
	{
		LogError("Could not read the item schema; name lookups and static attributes are unavailable.");
		delete kv;
		return;
	}

	char id[16], name[128];
	if (kv.JumpToKey("attributes") && kv.GotoFirstSubKey())
	{
		do
		{
			kv.GetSectionName(id, sizeof(id));
			kv.GetString("name", name, sizeof(name));
			if (!name[0])
				continue;
			StringToLower(name);
			g_smNameToId.SetValue(name, StringToInt(id));
			kv.GetString("name", name, sizeof(name));
			g_smIdToName.SetString(id, name);
			g_smIdInteger.SetValue(id, kv.GetNum("stored_as_integer", 0));
		}
		while (kv.GotoNextKey());
	}
	kv.Rewind();
	if (kv.JumpToKey("items") && kv.GotoFirstSubKey())
	{
		do
		{
			kv.GetSectionName(id, sizeof(id));
			ArrayList attrs = new ArrayList(2);
			ReadItemAttributes(kv, attrs);
			if (attrs.Length)
				g_smItemAttrs.SetValue(id, attrs);
			else
				delete attrs;
		}
		while (kv.GotoNextKey());
	}
	delete kv;
	g_bSchema = g_smNameToId.Size > 0;
	LogMessage("Item schema: %d attributes, %d items with static attributes.", g_smNameToId.Size, g_smItemAttrs.Size);
}

/** Reads an item's "attributes" block: "name" "value" or "name" { "value" ... }. */
void ReadItemAttributes(KeyValues kv, ArrayList attrs)
{
	if (!kv.JumpToKey("attributes"))
		return;
	if (kv.GotoFirstSubKey(false))
	{
		char name[128], lower[128];
		do
		{
			kv.GetSectionName(name, sizeof(name));
			strcopy(lower, sizeof(lower), name);
			StringToLower(lower);
			int def;
			if (!g_smNameToId.GetValue(lower, def))
				continue;
			float value = kv.GetDataType(NULL_STRING) == KvData_None ? kv.GetFloat("value") : kv.GetFloat(NULL_STRING);
			int row = attrs.Push(def);
			attrs.Set(row, value, 1);
		}
		while (kv.GotoNextKey(false));
		kv.GoBack();
	}
	kv.GoBack();
}

void StringToLower(char[] s)
{
	for (int i = 0; s[i]; i++)
		s[i] = CharToLower(s[i]);
}

bool NameFromDef(int def, char[] name, int maxlen)
{
	char key[16];
	IntToString(def, key, sizeof(key));
	return g_smIdToName.GetString(key, name, maxlen);
}

int DefFromName(const char[] name)
{
	char lower[128];
	strcopy(lower, sizeof(lower), name);
	StringToLower(lower);
	int def;
	return g_smNameToId.GetValue(lower, def) ? def : -1;
}

void WarnOnce(const char[] what)
{
	bool seen;
	if (g_smWarned.GetValue(what, seen))
		return;
	g_smWarned.SetValue(what, true);
	LogMessage("%s is not supported on TF2: Gold Rush.", what);
}

/* ------------------------------------------------------------------ */
/* VScript bridge                                                     */
/* ------------------------------------------------------------------ */

void RunScript(int entity, const char[] code)
{
	SetVariantString(code);
	AcceptEntityInput(entity, "RunScriptCode");
}

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

bool IsPlayerEnt(int entity)
{
	return entity >= 1 && entity <= MaxClients;
}

bool ValidTarget(int entity)
{
	if (entity <= 0 || !IsValidEntity(entity))
		return false;
	if (IsPlayerEnt(entity))
		return IsClientInGame(entity);
	return HasEntProp(entity, Prop_Send, "m_AttributeManager");
}

void ScriptSet(int entity, const char[] name, float value)
{
	char q[256], code[384];
	ScriptQuote(name, q, sizeof(q));
	if (IsPlayerEnt(entity))
		FormatEx(code, sizeof(code), "self.AddCustomAttribute(\"%s\", %f, -1.0)", q, value);
	else
		FormatEx(code, sizeof(code), "self.AddAttribute(\"%s\", %f, -1.0)", q, value);
	RunScript(entity, code);
}

void ScriptRemove(int entity, const char[] name)
{
	char q[256], code[320];
	ScriptQuote(name, q, sizeof(q));
	if (IsPlayerEnt(entity))
		FormatEx(code, sizeof(code), "self.RemoveCustomAttribute(\"%s\")", q);
	else
		FormatEx(code, sizeof(code), "self.RemoveAttribute(\"%s\")", q);
	RunScript(entity, code);
}

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

/** The attribute's current value on the entity, or ABSENT. */
float ScriptGet(int entity, const char[] name)
{
	char q[256], code[512], out[32];
	ScriptQuote(name, q, sizeof(q));
	int ret = RetEnt();
	DispatchKeyValue(ret, "targetname", "");
	if (IsPlayerEnt(entity))
		FormatEx(code, sizeof(code), "EntIndexToHScript(%d).KeyValueFromString(\"targetname\", self.GetCustomAttribute(\"%s\", %f).tostring())", ret, q, ABSENT);
	else
		FormatEx(code, sizeof(code), "EntIndexToHScript(%d).KeyValueFromString(\"targetname\", self.GetAttribute(\"%s\", %f).tostring())", ret, q, ABSENT);
	RunScript(entity, code);
	GetEntPropString(ret, Prop_Data, "m_iName", out, sizeof(out));
	if (!out[0])
		return ABSENT;
	float v = StringToFloat(out);
	return FloatAbs(v - ABSENT) < 0.01 ? ABSENT : v;
}

void Touch(int entity, int def, bool add)
{
	char key[16];
	IntToString(EntIndexToEntRef(entity), key, sizeof(key));
	ArrayList list;
	if (!g_smTouched.GetValue(key, list))
	{
		if (!add)
			return;
		list = new ArrayList();
		g_smTouched.SetValue(key, list);
	}
	int at = list.FindValue(def);
	if (add && at == -1)
		list.Push(def);
	else if (!add && at != -1)
		list.Erase(at);
}

public void OnEntityDestroyed(int entity)
{
	if (entity <= 0 || g_smTouched == null)
		return;
	char key[16];
	IntToString(EntIndexToEntRef(entity), key, sizeof(key));
	ArrayList list;
	if (g_smTouched.GetValue(key, list))
	{
		delete list;
		g_smTouched.Remove(key);
	}
}

Address MakeToken(int entity, int def)
{
	int t = g_iNextTok;
	g_iNextTok = g_iNextTok % (MAX_TOKENS - 1) + 1;
	g_iTokEnt[t] = EntIndexToEntRef(entity);
	g_iTokDef[t] = def;
	return view_as<Address>(t);
}

bool ReadToken(Address token, int &entity, int &def)
{
	int t = view_as<int>(token);
	if (t <= 0 || t >= MAX_TOKENS)
		return false;
	entity = EntRefToEntIndex(g_iTokEnt[t]);
	def = g_iTokDef[t];
	return entity > 0;
}

/* ------------------------------------------------------------------ */
/* Natives                                                            */
/* ------------------------------------------------------------------ */

bool SetByName(int entity, const char[] name, float value)
{
	if (!ValidTarget(entity))
		return false;
	ScriptSet(entity, name, value);
	int def = DefFromName(name);
	if (def != -1)
		Touch(entity, def, true);
	return true;
}

public any Native_SetByName(Handle plugin, int numParams)
{
	char name[128];
	GetNativeString(2, name, sizeof(name));
	return SetByName(GetNativeCell(1), name, GetNativeCell(3));
}

public any Native_SetByDefIndex(Handle plugin, int numParams)
{
	char name[128];
	if (!NameFromDef(GetNativeCell(2), name, sizeof(name)))
		return false;
	return SetByName(GetNativeCell(1), name, GetNativeCell(3));
}

public any Native_SetFromStringValue(Handle plugin, int numParams)
{
	int entity = GetNativeCell(1);
	if (!ValidTarget(entity))
		return false;
	char name[128], value[256], qn[256], qv[512], code[900];
	GetNativeString(2, name, sizeof(name));
	GetNativeString(3, value, sizeof(value));
	ScriptQuote(name, qn, sizeof(qn));
	ScriptQuote(value, qv, sizeof(qv));
	if (IsPlayerEnt(entity))
		FormatEx(code, sizeof(code), "self.AddCustomAttributeString(\"%s\", \"%s\", -1.0)", qn, qv);
	else
		FormatEx(code, sizeof(code), "self.AddAttributeString(\"%s\", \"%s\")", qn, qv);
	RunScript(entity, code);
	int def = DefFromName(name);
	if (def != -1)
		Touch(entity, def, true);
	return true;
}

Address GetByName(int entity, const char[] name)
{
	if (!ValidTarget(entity))
		return Address_Null;
	int def = DefFromName(name);
	if (def == -1 || ScriptGet(entity, name) == ABSENT)
		return Address_Null;
	return MakeToken(entity, def);
}

public any Native_GetByName(Handle plugin, int numParams)
{
	char name[128];
	GetNativeString(2, name, sizeof(name));
	return GetByName(GetNativeCell(1), name);
}

public any Native_GetByDefIndex(Handle plugin, int numParams)
{
	char name[128];
	if (!NameFromDef(GetNativeCell(2), name, sizeof(name)))
		return Address_Null;
	return GetByName(GetNativeCell(1), name);
}

bool RemoveByName(int entity, const char[] name)
{
	if (!ValidTarget(entity))
		return false;
	ScriptRemove(entity, name);
	int def = DefFromName(name);
	if (def != -1)
		Touch(entity, def, false);
	return true;
}

public any Native_RemoveByName(Handle plugin, int numParams)
{
	char name[128];
	GetNativeString(2, name, sizeof(name));
	return RemoveByName(GetNativeCell(1), name);
}

public any Native_RemoveByDefIndex(Handle plugin, int numParams)
{
	char name[128];
	if (!NameFromDef(GetNativeCell(2), name, sizeof(name)))
		return false;
	return RemoveByName(GetNativeCell(1), name);
}

/** Removes the runtime attributes set through this plugin (static ones stay, as in TF2Attributes). */
public any Native_RemoveAll(Handle plugin, int numParams)
{
	int entity = GetNativeCell(1);
	if (!ValidTarget(entity))
		return false;
	char key[16], name[128];
	IntToString(EntIndexToEntRef(entity), key, sizeof(key));
	ArrayList list;
	if (g_smTouched.GetValue(key, list))
	{
		for (int i = 0; i < list.Length; i++)
			if (NameFromDef(list.Get(i), name, sizeof(name)))
				ScriptRemove(entity, name);
		list.Clear();
	}
	return true;
}

public any Native_ClearCache(Handle plugin, int numParams)
{
	int entity = GetNativeCell(1);
	if (!ValidTarget(entity))
		return false;
	if (!IsPlayerEnt(entity))
		RunScript(entity, "self.ReapplyProvision()");
	return true;   // VScript attribute changes already invalidate the game's cache
}

public any Native_SetDefIndex(Handle plugin, int numParams)
{
	int entity, def;
	if (!ReadToken(GetNativeCell(1), entity, def))
		return 0;
	int newDef = GetNativeCell(2);
	char oldName[128], newName[128];
	if (!NameFromDef(def, oldName, sizeof(oldName)) || !NameFromDef(newDef, newName, sizeof(newName)))
		return 0;
	float v = ScriptGet(entity, oldName);
	ScriptRemove(entity, oldName);
	ScriptSet(entity, newName, v == ABSENT ? 0.0 : v);
	g_iTokDef[view_as<int>(GetNativeCell(1))] = newDef;
	return 0;
}

public any Native_GetDefIndex(Handle plugin, int numParams)
{
	int entity, def;
	return ReadToken(GetNativeCell(1), entity, def) ? def : -1;
}

public any Native_SetValue(Handle plugin, int numParams)
{
	int entity, def;
	char name[128];
	if (ReadToken(GetNativeCell(1), entity, def) && NameFromDef(def, name, sizeof(name)))
		ScriptSet(entity, name, GetNativeCell(2));
	return 0;
}

public any Native_GetValue(Handle plugin, int numParams)
{
	int entity, def;
	char name[128];
	if (!ReadToken(GetNativeCell(1), entity, def) || !NameFromDef(def, name, sizeof(name)))
		return 0.0;
	float v = ScriptGet(entity, name);
	return v == ABSENT ? 0.0 : v;
}

public any Native_UnsafeGetStringValue(Handle plugin, int numParams)
{
	WarnOnce("TF2Attrib_UnsafeGetStringValue");
	SetNativeString(2, "", GetNativeCell(3));
	return 0;
}

public any Native_SetRefundableCurrency(Handle plugin, int numParams)
{
	WarnOnce("TF2Attrib_SetRefundableCurrency");
	return 0;
}

public any Native_GetRefundableCurrency(Handle plugin, int numParams)
{
	WarnOnce("TF2Attrib_GetRefundableCurrency");
	return 0;
}

/** Runtime attributes set through this plugin that are still present. */
public any Native_ListDefIndices(Handle plugin, int numParams)
{
	int entity = GetNativeCell(1);
	int maxlen = GetNativeCell(3);
	if (!ValidTarget(entity) || maxlen <= 0)
		return -1;
	int[] out = new int[maxlen];
	int n = 0;
	char key[16], name[128];
	IntToString(EntIndexToEntRef(entity), key, sizeof(key));
	ArrayList list;
	if (g_smTouched.GetValue(key, list))
	{
		for (int i = 0; i < list.Length && n < maxlen; i++)
		{
			int def = list.Get(i);
			if (NameFromDef(def, name, sizeof(name)) && ScriptGet(entity, name) != ABSENT)
				out[n++] = def;
		}
	}
	SetNativeArray(2, out, maxlen);
	return n;
}

public any Native_GetStaticAttribs(Handle plugin, int numParams)
{
	char key[16];
	IntToString(GetNativeCell(1), key, sizeof(key));
	int maxlen = GetNativeCell(4);
	ArrayList attrs;
	if (maxlen <= 0 || !g_smItemAttrs.GetValue(key, attrs))
		return 0;
	int n = attrs.Length < maxlen ? attrs.Length : maxlen;
	int[] ids = new int[maxlen];
	float[] vals = new float[maxlen];
	for (int i = 0; i < n; i++)
	{
		ids[i] = attrs.Get(i, 0);
		vals[i] = attrs.Get(i, 1);
	}
	SetNativeArray(2, ids, maxlen);
	SetNativeArray(3, vals, maxlen);
	return n;
}

public any Native_GetSOCAttribs(Handle plugin, int numParams)
{
	return 0;   // no item server on TF2: Gold Rush: items carry no SOC attributes
}

public any Native_IsIntegerValue(Handle plugin, int numParams)
{
	char key[16];
	IntToString(GetNativeCell(1), key, sizeof(key));
	int v;
	return g_smIdInteger.GetValue(key, v) && v != 0;
}

public any Native_IsValidAttributeName(Handle plugin, int numParams)
{
	char name[128];
	GetNativeString(1, name, sizeof(name));
	return DefFromName(name) != -1;
}

public any Native_AddCustomPlayerAttribute(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (!IsPlayerEnt(client) || !IsClientInGame(client))
		return 0;
	char name[128], q[256], code[384];
	GetNativeString(2, name, sizeof(name));
	ScriptQuote(name, q, sizeof(q));
	FormatEx(code, sizeof(code), "self.AddCustomAttribute(\"%s\", %f, %f)", q, view_as<float>(GetNativeCell(3)), view_as<float>(GetNativeCell(4)));
	RunScript(client, code);
	return 0;
}

public any Native_RemoveCustomPlayerAttribute(Handle plugin, int numParams)
{
	int client = GetNativeCell(1);
	if (!IsPlayerEnt(client) || !IsClientInGame(client))
		return 0;
	char name[128];
	GetNativeString(2, name, sizeof(name));
	ScriptRemove(client, name);
	return 0;
}

public any Native_HookValueFloat(Handle plugin, int numParams)
{
	WarnOnce("TF2Attrib_HookValueFloat");
	return GetNativeCell(1);
}

public any Native_HookValueInt(Handle plugin, int numParams)
{
	WarnOnce("TF2Attrib_HookValueInt");
	return GetNativeCell(1);
}

public any Native_HookValueString(Handle plugin, int numParams)
{
	WarnOnce("TF2Attrib_HookValueString");
	char initial[256];
	GetNativeString(1, initial, sizeof(initial));
	SetNativeString(4, initial, GetNativeCell(5));
	return strlen(initial);
}

public any Native_IsReady(Handle plugin, int numParams)
{
	return g_bSchema;
}
