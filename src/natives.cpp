#include "natives.h"
#include "script_context.h"
#include "variant_marshal.h"
#include "callback_manager.h"

HScriptHandle *ReadHScriptHandle(IPluginContext *pContext, Handle_t hndl)
{
	HandleSecurity sec(pContext->GetIdentity(), myself->GetIdentity());
	HScriptHandle *pHandle = nullptr;
	HandleError err = handlesys->ReadHandle(hndl, g_VScriptExt.GetHScriptHandleType(), &sec, (void **)&pHandle);

	if (err != HandleError_None)
	{
		pContext->ThrowNativeError("Invalid ScriptHandle (error %d)", err);
		return nullptr;
	}

	return pHandle;
}

Handle_t CreateHScriptHandle(IPluginContext *pContext, HSCRIPT hScript, HScriptType type, HScriptOwnership ownership)
{
	if (!hScript)
		return BAD_HANDLE;

	HScriptHandle *pHandle = new HScriptHandle(hScript, type, ownership);

	Handle_t hndl = handlesys->CreateHandle(
		g_VScriptExt.GetHScriptHandleType(),
		pHandle,
		pContext->GetIdentity(),
		myself->GetIdentity(),
		nullptr);

	if (hndl == BAD_HANDLE)
	{
		if (ownership == HScriptOwnership::Owned && g_VScriptExt.GetVM() && hScript)
			ReleaseOwnedHScript(g_VScriptExt.GetVM(), hScript, type);

		delete pHandle;
	}

	return hndl;
}

static bool ReadHScriptParam(IPluginContext *pContext, cell_t param, HSCRIPT &out)
{
	if (param == 0)
	{
		pContext->ThrowNativeError("Invalid ScriptHandle (null)");
		return false;
	}

	HScriptHandle *pHandle = ReadHScriptHandle(pContext, (Handle_t)param);
	if (!pHandle)
		return false;

	out = pHandle->hScript;
	return true;
}

static bool ReadOptionalHScriptParam(IPluginContext *pContext, cell_t param, HSCRIPT &out)
{
	if (param == 0)
	{
		out = nullptr;
		return true;
	}
	return ReadHScriptParam(pContext, param, out);
}

static bool IsValidSPFieldType(int value)
{
	return value >= (int)SPFieldType::Void && value <= (int)SPFieldType::Variant;
}

template <int N>
static void CellsToFloats(const cell_t *cells, float *out)
{
	for (int i = 0; i < N; i++)
		out[i] = sp_ctof(cells[i]);
}

template <int N>
static void FloatsToCells(const float *in, cell_t *out)
{
	for (int i = 0; i < N; i++)
		out[i] = sp_ftoc(in[i]);
}

static ScriptIteratorState *ReadScriptIterator(IPluginContext *pContext, Handle_t hndl)
{
	HandleSecurity sec(pContext->GetIdentity(), myself->GetIdentity());
	ScriptIteratorState *pIter = nullptr;
	HandleError err = handlesys->ReadHandle(hndl, g_VScriptExt.GetScriptIteratorHandleType(), &sec, (void **)&pIter);

	if (err != HandleError_None)
	{
		pContext->ThrowNativeError("Invalid ScriptIterator (error %d)", err);
		return nullptr;
	}

	return pIter;
}

static int HScriptToEntityIndex(IScriptVM *pVM, HSCRIPT h)
{
	void *pInstance = pVM->GetInstanceValue(h, nullptr);
	if (!pInstance)
		return -1;

	if (!g_VScriptExt.IsKnownEntity(pInstance))
		return -1;

	return gamehelpers->EntityToBCompatRef(static_cast<CBaseEntity *>(pInstance));
}

static int VariantToEntityIndex(IScriptVM *pVM, const ScriptVariant_t &variant)
{
	int engineType = variant.GetType();

	if (engineType == FIELD_HSCRIPT)
	{
		HSCRIPT h = VariantMarshal::ReadVariantHScript(variant);
		if (h)
			return HScriptToEntityIndex(pVM, h);
	}
	else if (engineType == FIELD_EHANDLE)
	{
		CBaseHandle handle = VariantMarshal::ReadVariantEHandle(variant);
		if (handle.IsValid())
		{
			int entIndex = handle.GetEntryIndex();
			edict_t *pEdict = engine->PEntityOfEntIndex(entIndex);
			if (pEdict && pEdict->m_NetworkSerialNumber == (handle.GetSerialNumber() & ((1 << NUM_NETWORKED_EHANDLE_SERIAL_NUMBER_BITS) - 1)))
			{
				CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(entIndex);
				if (pEntity)
					return gamehelpers->EntityToBCompatRef(pEntity);
			}
		}
	}

	return -1;
}


// native bool VScript_IsVMInitialized();
static cell_t Native_IsVMInitialized(IPluginContext *pContext, const cell_t *params)
{
	return g_VScriptExt.GetVM() != nullptr;
}

// native ScriptStatus VScript_Run(const char[] code, any ...);
static cell_t Native_Run(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;

	if (params[0] > 1)
	{
		char buffer[4096];
		smutils->FormatString(buffer, sizeof(buffer), pContext, params, 1);
		return (cell_t)pVM->Run(buffer);
	}

	char *code;
	pContext->LocalToString(params[1], &code);
	return (cell_t)pVM->Run(code);
}

// native ScriptHandle VScript_CompileScript(const char[] code, const char[] id = "");
static cell_t Native_CompileScript(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;

	char *code, *id;
	pContext->LocalToString(params[1], &code);
	pContext->LocalToString(params[2], &id);

	HSCRIPT hScript = pVM->CompileScript(code, id[0] ? id : nullptr);
	if (!hScript)
		return BAD_HANDLE;

	return (cell_t)CreateHScriptHandle(pContext, hScript, HScriptType::Script, HScriptOwnership::Owned);
}

// native ScriptStatus VScript_RunScript(ScriptHandle script, ScriptHandle scope);
static cell_t Native_RunScript(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;

	HSCRIPT hScript;
	if (!ReadHScriptParam(pContext, params[1], hScript))
		return (cell_t)SCRIPT_ERROR;
	if (!hScript)
		return (cell_t)SCRIPT_ERROR;

	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[2], hScope))
		return (cell_t)SCRIPT_ERROR;

	return (cell_t)pVM->Run(hScript, hScope, true);
}

// native ScriptHandle VScript_CreateScope(const char[] name, ScriptHandle parent);
static cell_t Native_CreateScope(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;

	char *name;
	pContext->LocalToString(params[1], &name);

	HSCRIPT hParent;
	if (!ReadHScriptParam(pContext, params[2], hParent))
		return 0;

	HSCRIPT hScope = pVM->CreateScope(name, hParent);
	if (!hScope)
		return BAD_HANDLE;

	return (cell_t)CreateHScriptHandle(pContext, hScope, HScriptType::Scope, HScriptOwnership::Owned);
}

// native ScriptHandle VScript_CreateTable();
static cell_t Native_CreateTable(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;

	ScriptVariant_t table;
	pVM->CreateTable(table);

	HSCRIPT hTable = (HSCRIPT)table;
	if (!hTable)
		return BAD_HANDLE;

	return (cell_t)CreateHScriptHandle(pContext, hTable, HScriptType::Table, HScriptOwnership::Owned);
}

// native bool VScript_RegisterFunction(const char[] name, ScriptCallback callback, const char[] description = "", ScriptFieldType returnType = ScriptField_Void, ScriptFieldType ...);
static cell_t Native_RegisterFunction(IPluginContext *pContext, const cell_t *params)
{
	char *name, *description;
	pContext->LocalToString(params[1], &name);

	IPluginFunction *pCallback = pContext->GetFunctionById(params[2]);
	if (!pCallback)
		return pContext->ThrowNativeError("Invalid callback function");

	pContext->LocalToString(params[3], &description);

	if (!IsValidSPFieldType(params[4]))
		return pContext->ThrowNativeError("Invalid return type %d", params[4]);
	SPFieldType returnType = (SPFieldType)params[4];

	int numParams = params[0] - 4;
	std::vector<SPFieldType> paramTypes;
	for (int i = 0; i < numParams; i++)
	{
		cell_t *addr;
		pContext->LocalToPhysAddr(params[5 + i], &addr);
		if (!IsValidSPFieldType(*addr))
			return pContext->ThrowNativeError("Invalid parameter type %d at index %d", *addr, i);
		paramTypes.push_back((SPFieldType)*addr);
	}

	RegisteredFunction *pReg = g_CallbackManager.RegisterFunction(
		name, description, pCallback, returnType, paramTypes);

	if (!pReg)
		return pContext->ThrowNativeError("Failed to register VScript function '%s'", name);

	return 1;
}

// native bool VScript_RegisterClassFunction(const char[] className, const char[] name, ScriptCallback callback, const char[] description = "", ScriptFieldType returnType = ScriptField_Void, ScriptFieldType ...);
static cell_t Native_RegisterClassFunction(IPluginContext *pContext, const cell_t *params)
{
	char *className, *name, *description;
	pContext->LocalToString(params[1], &className);
	pContext->LocalToString(params[2], &name);

	IPluginFunction *pCallback = pContext->GetFunctionById(params[3]);
	if (!pCallback)
		return pContext->ThrowNativeError("Invalid callback function");

	pContext->LocalToString(params[4], &description);

	if (!IsValidSPFieldType(params[5]))
		return pContext->ThrowNativeError("Invalid return type %d", params[5]);
	SPFieldType returnType = (SPFieldType)params[5];

	int numParams = params[0] - 5;
	std::vector<SPFieldType> paramTypes;
	for (int i = 0; i < numParams; i++)
	{
		cell_t *addr;
		pContext->LocalToPhysAddr(params[6 + i], &addr);
		if (!IsValidSPFieldType(*addr))
			return pContext->ThrowNativeError("Invalid parameter type %d at index %d", *addr, i);
		paramTypes.push_back((SPFieldType)*addr);
	}

	RegisteredFunction *pReg = g_CallbackManager.RegisterClassFunction(
		className, name, description, pCallback, returnType, paramTypes);

	if (!pReg)
		return pContext->ThrowNativeError("Failed to register class function '%s' on '%s'", name, className);

	return 1;
}

// native bool VScript_UnregisterFunction(const char[] name);
static cell_t Native_UnregisterFunction(IPluginContext *pContext, const cell_t *params)
{
	char *name;
	pContext->LocalToString(params[1], &name);

	return g_CallbackManager.UnregisterFunction(name);
}

// native bool VScript_UnregisterClassFunction(const char[] className, const char[] name);
static cell_t Native_UnregisterClassFunction(IPluginContext *pContext, const cell_t *params)
{
	char *className, *name;
	pContext->LocalToString(params[1], &className);
	pContext->LocalToString(params[2], &name);

	return g_CallbackManager.UnregisterClassFunction(className, name);
}

// Entity script offset helpers

static int s_offsetScriptScope = -1;     // m_ScriptScope
static int s_offsetHScriptInstance = -1; // m_hScriptInstance

static void FindEntityScriptOffsets()
{
	if (s_offsetScriptScope != -1)
		return;

	CBaseEntity *pWorld = gamehelpers->ReferenceToEntity(0);
	if (!pWorld)
		return;

	datamap_t *pMap = gamehelpers->GetDataMap(pWorld);
	if (!pMap)
		return;

	sm_datatable_info_t infoThink, infoId;
	bool foundThink = gamehelpers->FindDataMapInfo(pMap, "m_iszScriptThinkFunction", &infoThink);
	bool foundId = gamehelpers->FindDataMapInfo(pMap, "m_iszScriptId", &infoId);

	if (foundThink && foundId)
	{
		int T = infoThink.actual_offset;
		int S = infoId.actual_offset;

		// m_ScriptScope starts after m_iszScriptThinkFunction, aligned to pointer size
		int scopeOffset = (T + sizeof(string_t) + (sizeof(void *) - 1)) & ~(sizeof(void *) - 1);

		// m_hScriptInstance is right before m_iszScriptId
		int instanceOffset = S - sizeof(HSCRIPT);

		if (instanceOffset - scopeOffset == sizeof(CScriptScope))
		{
			s_offsetScriptScope = scopeOffset;
			s_offsetHScriptInstance = instanceOffset;
		}
	}
}

static HSCRIPT ReadEntityScriptScope(CBaseEntity *pEntity)
{
	return *(HSCRIPT *)((uint8_t *)pEntity + s_offsetScriptScope);
}

static HSCRIPT ReadEntityHScriptInstance(CBaseEntity *pEntity)
{
	return *(HSCRIPT *)((uint8_t *)pEntity + s_offsetHScriptInstance);
}

static HSCRIPT EnsureEntityScriptInstance(IScriptVM *pVM, CBaseEntity *pEntity)
{
	HSCRIPT hInstance = ReadEntityHScriptInstance(pEntity);
	if (hInstance)
		return hInstance;

	HSCRIPT hFunc = VariantMarshal::LookupFunction(pVM, "EntIndexToHScript", nullptr);
	if (!hFunc)
		return nullptr;

	int entindex = gamehelpers->ReferenceToIndex(gamehelpers->EntityToBCompatRef(pEntity));
	ScriptVariant_t arg = entindex;
	pVM->ExecuteFunction(hFunc, &arg, 1, nullptr, nullptr, true);
	pVM->ReleaseFunction(hFunc);

	// Don't use ExecuteFunction's return, its heap SQObject can't be kept or safely released.
	// EntIndexToHScript sets m_hScriptInstance as a side effect, so read it directly.
	return ReadEntityHScriptInstance(pEntity);
}

static HSCRIPT EnsureEntityScriptScope(IScriptVM *pVM, CBaseEntity *pEntity)
{
	HSCRIPT hScope = ReadEntityScriptScope(pEntity);
	if (hScope && hScope != INVALID_HSCRIPT)
		return hScope;

	HSCRIPT hInstance = EnsureEntityScriptInstance(pVM, pEntity);
	if (!hInstance)
		return nullptr;

	HSCRIPT hValidate = VariantMarshal::LookupFunction(pVM, "ValidateScriptScope", hInstance);
	if (!hValidate)
		return nullptr;

	ScriptVariant_t ret;
	pVM->ExecuteFunction(hValidate, nullptr, 0, &ret, hInstance, true);
	pVM->ReleaseFunction(hValidate);

	bool validated = (bool)ret;
	pVM->ReleaseValue(ret);

	if (!validated)
		return nullptr;

	hScope = ReadEntityScriptScope(pEntity);
	return (hScope && hScope != INVALID_HSCRIPT) ? hScope : nullptr;
}

// native ScriptHandle VScript_GetEntityScriptScope(int entity, bool create = false);
static cell_t Native_GetEntityScriptScope(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;

	FindEntityScriptOffsets();
	if (s_offsetScriptScope == -1)
		return pContext->ThrowNativeError("Could not find entity script scope offset");

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if (!pEntity)
		return pContext->ThrowNativeError("Invalid entity index %d", params[1]);

	HSCRIPT hScope;
	if (params[2])
		hScope = EnsureEntityScriptScope(pVM, pEntity);
	else
	{
		hScope = ReadEntityScriptScope(pEntity);
		if (!hScope || hScope == INVALID_HSCRIPT)
			return BAD_HANDLE;
	}

	if (!hScope)
		return BAD_HANDLE;

	return (cell_t)g_VScriptExt.GetOrCreateCachedEntityHandle(pEntity, hScope, HScriptType::Scope);
}

// native ScriptHandle VScript_EntityToHScript(int entity, bool create = false);
static cell_t Native_EntityToHScript(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;

	FindEntityScriptOffsets();
	if (s_offsetHScriptInstance == -1)
		return pContext->ThrowNativeError("Could not find entity hscript instance offset");

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if (!pEntity)
		return pContext->ThrowNativeError("Invalid entity index %d", params[1]);

	HSCRIPT hInstance;
	if (params[2])
		hInstance = EnsureEntityScriptInstance(pVM, pEntity);
	else
	{
		hInstance = ReadEntityHScriptInstance(pEntity);
		if (!hInstance)
			return BAD_HANDLE;
	}

	if (!hInstance)
		return BAD_HANDLE;

	return (cell_t)g_VScriptExt.GetOrCreateCachedEntityHandle(pEntity, hInstance, HScriptType::Value);
}

// native int VScript_HScriptToEntity(ScriptHandle hscript);
static cell_t Native_HScriptToEntity(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return -1;

	HSCRIPT h;
	if (!ReadHScriptParam(pContext, params[1], h))
		return -1;
	if (!h)
		return -1;

	return HScriptToEntityIndex(pVM, h);
}

// native bool VScript_GenerateUniqueKey(const char[] root, char[] buffer, int maxlen);
static cell_t Native_GenerateUniqueKey(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;

	char *root;
	pContext->LocalToString(params[1], &root);

	char *buffer;
	pContext->LocalToString(params[2], &buffer);
	int maxlen = params[3];

	return pVM->GenerateUniqueKey(root, buffer, maxlen);
}

// native void VScript_AddSearchPath(const char[] path);
static cell_t Native_AddSearchPath(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;

	char *path;
	pContext->LocalToString(params[1], &path);

	pVM->AddSearchPath(path);
	return 0;
}

// ScriptHandle methodmap

template <int N, void (*ReadFn)(const ScriptVariant_t &, float *)>
static cell_t Native_GetValueFloatArray(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);

	cell_t *out;
	pContext->LocalToPhysAddr(params[3], &out);

	ScriptVariant_t value;
	if (!pVM->GetValue(hScope, key, &value))
	{
		for (int i = 0; i < N; i++) out[i] = 0;
		return 0;
	}

	float fbuf[N];
	ReadFn(value, fbuf);
	pVM->ReleaseValue(value);

	FloatsToCells<N>(fbuf, out);
	return 0;
}

template <int N, void (*WriteFn)(ScriptVariant_t &, const float *)>
static cell_t Native_SetValueFloatArray(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);

	cell_t *cells;
	pContext->LocalToPhysAddr(params[3], &cells);

	float fbuf[N];
	CellsToFloats<N>(cells, fbuf);

	ScriptVariant_t variant;
	WriteFn(variant, fbuf);

	bool result = pVM->SetValue(hScope, key, variant);
	variant.Free();
	return result;
}

// native ScriptFieldType ScriptHandle.GetType(const char[] key);
static cell_t Native_GetValueType(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);

	ScriptVariant_t value;
	if (!pVM->GetValue(hScope, key, &value))
		return (cell_t)SPFieldType::Void;

	SPFieldType type = VariantMarshal::ReadVariantType(value);
	pVM->ReleaseValue(value);
	return (cell_t)type;
}

// native bool ScriptHandle.HasKey(const char[] key);
static cell_t Native_ValueExists(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);
	return pVM->ValueExists(hScope, key);
}

// native int ScriptHandle.Length.get();
static cell_t Native_GetNumTableEntries(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;

	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	return pVM->GetNumTableEntries(hScope);
}

// native int ScriptHandle.GetInt(const char[] key);
static cell_t Native_GetValueInt(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);

	ScriptVariant_t value;
	if (!pVM->GetValue(hScope, key, &value))
		return 0;

	cell_t result = VariantMarshal::ReadVariantInt(value);
	pVM->ReleaseValue(value);
	return result;
}

// native float ScriptHandle.GetFloat(const char[] key);
static cell_t Native_GetValueFloat(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);

	ScriptVariant_t value;
	if (!pVM->GetValue(hScope, key, &value))
		return 0;

	float result = VariantMarshal::ReadVariantFloat(value);
	pVM->ReleaseValue(value);
	return sp_ftoc(result);
}

// native bool ScriptHandle.GetBool(const char[] key);
static cell_t Native_GetValueBool(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);

	ScriptVariant_t value;
	if (!pVM->GetValue(hScope, key, &value))
		return 0;

	cell_t result = VariantMarshal::ReadVariantBool(value);
	pVM->ReleaseValue(value);
	return result;
}

// native int ScriptHandle.GetString(const char[] key, char[] buffer, int maxlen);
static cell_t Native_GetValueString(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);

	char *buffer;
	pContext->LocalToString(params[3], &buffer);
	int maxlen = params[4];

	ScriptVariant_t value;
	if (!pVM->GetValue(hScope, key, &value))
	{
		buffer[0] = '\0';
		return 0;
	}

	int bytes = VariantMarshal::ReadVariantString(value, buffer, maxlen);
	pVM->ReleaseValue(value);
	return bytes;
}

// native ScriptHandle ScriptHandle.GetHScript(const char[] key);
static cell_t Native_GetValueHScript(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);

	ScriptVariant_t value;
	if (!pVM->GetValue(hScope, key, &value))
		return BAD_HANDLE;

	HSCRIPT h = VariantMarshal::ReadVariantHScript(value);
	if (!h)
	{
		pVM->ReleaseValue(value);
		return BAD_HANDLE;
	}

	// Ownership transferred to handle, do not call ReleaseValue
	return (cell_t)CreateHScriptHandle(pContext, h, HScriptType::Table, HScriptOwnership::Owned);
}

// native int ScriptHandle.GetEntity(const char[] key);
static cell_t Native_GetValueEntity(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return -1;

	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return -1;

	char *key;
	pContext->LocalToString(params[2], &key);

	ScriptVariant_t value;
	if (!pVM->GetValue(hScope, key, &value))
		return -1;

	int result = VariantToEntityIndex(pVM, value);
	pVM->ReleaseValue(value);
	return result;
}

// native bool ScriptHandle.SetInt(const char[] key, int value);
static cell_t Native_SetValueInt(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);

	ScriptVariant_t variant;
	VariantMarshal::WriteVariantInt(variant, params[3]);
	return pVM->SetValue(hScope, key, variant);
}

// native bool ScriptHandle.SetFloat(const char[] key, float value);
static cell_t Native_SetValueFloat(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);

	ScriptVariant_t variant;
	VariantMarshal::WriteVariantFloat(variant, sp_ctof(params[3]));
	return pVM->SetValue(hScope, key, variant);
}

// native bool ScriptHandle.SetBool(const char[] key, bool value);
static cell_t Native_SetValueBool(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);

	ScriptVariant_t variant;
	VariantMarshal::WriteVariantBool(variant, params[3] != 0);
	return pVM->SetValue(hScope, key, variant);
}

// native bool ScriptHandle.SetString(const char[] key, const char[] value);
static cell_t Native_SetValueString(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);

	char *value;
	pContext->LocalToString(params[3], &value);
	return pVM->SetValue(hScope, key, value);
}

// native bool ScriptHandle.SetHScript(const char[] key, ScriptHandle value);
static cell_t Native_SetValueHScript(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);

	HSCRIPT hValue;
	if (!ReadOptionalHScriptParam(pContext, params[3], hValue))
		return 0;
	ScriptVariant_t variant;
	VariantMarshal::WriteVariantHScript(variant, hValue);
	return pVM->SetValue(hScope, key, variant);
}

// native bool ScriptHandle.SetNull(const char[] key);
static cell_t Native_SetValueNull(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);

	ScriptVariant_t variant = {};
	return pVM->SetValue(hScope, key, variant);
}

// native bool ScriptHandle.DeleteKey(const char[] key);
static cell_t Native_ClearValue(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;
	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;
	char *key;
	pContext->LocalToString(params[2], &key);

	if (!pVM->ValueExists(hScope, key))
		return 0;

	pVM->ClearValue(hScope, key);
	return 1;
}

// native ScriptIterator ScriptHandle.Iterate();
static cell_t Native_Iterate(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return BAD_HANDLE;

	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return BAD_HANDLE;

	ScriptIteratorState *pIter = new ScriptIteratorState(hScope, g_vmGeneration);

	Handle_t hndl = handlesys->CreateHandle(
		g_VScriptExt.GetScriptIteratorHandleType(),
		pIter,
		pContext->GetIdentity(),
		myself->GetIdentity(),
		nullptr);

	if (hndl == BAD_HANDLE)
	{
		delete pIter;
		return BAD_HANDLE;
	}

	return (cell_t)hndl;
}

// native int ScriptHandle.GetNextKey(int iterator, char[] keyBuffer, int keyMaxLen, ScriptFieldType &keyType, ScriptFieldType &fieldType);
static cell_t Native_GetNextKey(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return -1;

	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return -1;

	int iterator = params[2];
	ScriptVariant_t key, value;

	int next = pVM->GetKeyValue(hScope, iterator, &key, &value);

	if (next != -1)
	{
		char *keyBuffer;
		pContext->LocalToString(params[3], &keyBuffer);
		int keyMaxLen = params[4];

		VariantMarshal::ReadVariantString(key, keyBuffer, keyMaxLen);

		cell_t *pKeyType;
		pContext->LocalToPhysAddr(params[5], &pKeyType);
		*pKeyType = (cell_t)VariantMarshal::EngineToSPField(key.GetType());

		cell_t *pFieldType;
		pContext->LocalToPhysAddr(params[6], &pFieldType);
		*pFieldType = (cell_t)VariantMarshal::EngineToSPField(value.GetType());

		pVM->ReleaseValue(key);
		pVM->ReleaseValue(value);
	}

	return next;
}

// native ScriptHandle ScriptHandle.LookupFunction(const char[] name);
static cell_t Native_LookupFunction(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;

	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[1], hScope))
		return 0;

	char *name;
	pContext->LocalToString(params[2], &name);

	HSCRIPT hFunc = VariantMarshal::LookupFunction(pVM, name, hScope);

	if (!hFunc)
		return BAD_HANDLE;

	return (cell_t)CreateHScriptHandle(pContext, hFunc, HScriptType::Function, HScriptOwnership::Owned);
}

// ScriptIterator methodmap

template <int N, void (*ReadFn)(const ScriptVariant_t &, float *)>
static cell_t Native_IterGetValueFloatArray(IPluginContext *pContext, const cell_t *params)
{
	ScriptIteratorState *pIter = ReadScriptIterator(pContext, (Handle_t)params[1]);
	if (!pIter) return 0;

	if (!pIter->hasValue)
	{
		pContext->ThrowNativeError("No iteration value available (call Next() first)");
		return 0;
	}

	cell_t *out;
	pContext->LocalToPhysAddr(params[2], &out);

	float fbuf[N];
	ReadFn(pIter->value, fbuf);
	FloatsToCells<N>(fbuf, out);
	return 0;
}

// native bool ScriptIterator.Next();
static cell_t Native_IterNext(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;

	ScriptIteratorState *pIter = ReadScriptIterator(pContext, (Handle_t)params[1]);
	if (!pIter) return 0;

	pIter->ReleaseCurrentValues();

	ScriptVariant_t key, value;
	int next = pVM->GetKeyValue(pIter->hScope, pIter->nextIterator, &key, &value);

	if (next == -1)
		return 0;

	pIter->key = key;
	pIter->value = value;
	pIter->hasValue = true;
	pIter->nextIterator = next;
	return 1;
}

// native ScriptFieldType ScriptIterator.KeyType.get();
static cell_t Native_IterKeyType(IPluginContext *pContext, const cell_t *params)
{
	ScriptIteratorState *pIter = ReadScriptIterator(pContext, (Handle_t)params[1]);
	if (!pIter) return 0;

	if (!pIter->hasValue)
	{
		pContext->ThrowNativeError("No iteration value available (call Next() first)");
		return 0;
	}

	return (cell_t)VariantMarshal::EngineToSPField(pIter->key.GetType());
}

// native int ScriptIterator.GetKeyString(char[] buffer, int maxlen);
static cell_t Native_IterGetKeyString(IPluginContext *pContext, const cell_t *params)
{
	ScriptIteratorState *pIter = ReadScriptIterator(pContext, (Handle_t)params[1]);
	if (!pIter) return 0;

	if (!pIter->hasValue)
	{
		pContext->ThrowNativeError("No iteration value available (call Next() first)");
		return 0;
	}

	char *buffer;
	pContext->LocalToString(params[2], &buffer);
	int maxlen = params[3];

	return VariantMarshal::ReadVariantString(pIter->key, buffer, maxlen);
}

// native int ScriptIterator.GetKeyInt();
static cell_t Native_IterGetKeyInt(IPluginContext *pContext, const cell_t *params)
{
	ScriptIteratorState *pIter = ReadScriptIterator(pContext, (Handle_t)params[1]);
	if (!pIter) return 0;

	if (!pIter->hasValue)
	{
		pContext->ThrowNativeError("No iteration value available (call Next() first)");
		return 0;
	}

	return VariantMarshal::ReadVariantInt(pIter->key);
}

// native ScriptFieldType ScriptIterator.ValueType.get();
static cell_t Native_IterValueType(IPluginContext *pContext, const cell_t *params)
{
	ScriptIteratorState *pIter = ReadScriptIterator(pContext, (Handle_t)params[1]);
	if (!pIter) return 0;

	if (!pIter->hasValue)
	{
		pContext->ThrowNativeError("No iteration value available (call Next() first)");
		return 0;
	}

	return (cell_t)VariantMarshal::EngineToSPField(pIter->value.GetType());
}

// native int ScriptIterator.GetValueInt();
static cell_t Native_IterGetValueInt(IPluginContext *pContext, const cell_t *params)
{
	ScriptIteratorState *pIter = ReadScriptIterator(pContext, (Handle_t)params[1]);
	if (!pIter) return 0;

	if (!pIter->hasValue)
	{
		pContext->ThrowNativeError("No iteration value available (call Next() first)");
		return 0;
	}

	return VariantMarshal::ReadVariantInt(pIter->value);
}

// native bool ScriptIterator.GetValueBool();
static cell_t Native_IterGetValueBool(IPluginContext *pContext, const cell_t *params)
{
	ScriptIteratorState *pIter = ReadScriptIterator(pContext, (Handle_t)params[1]);
	if (!pIter) return 0;

	if (!pIter->hasValue)
	{
		pContext->ThrowNativeError("No iteration value available (call Next() first)");
		return 0;
	}

	return VariantMarshal::ReadVariantBool(pIter->value);
}

// native float ScriptIterator.GetValueFloat();
static cell_t Native_IterGetValueFloat(IPluginContext *pContext, const cell_t *params)
{
	ScriptIteratorState *pIter = ReadScriptIterator(pContext, (Handle_t)params[1]);
	if (!pIter) return 0;

	if (!pIter->hasValue)
	{
		pContext->ThrowNativeError("No iteration value available (call Next() first)");
		return 0;
	}

	return sp_ftoc(VariantMarshal::ReadVariantFloat(pIter->value));
}

// native int ScriptIterator.GetValueString(char[] buffer, int maxlen);
static cell_t Native_IterGetValueString(IPluginContext *pContext, const cell_t *params)
{
	ScriptIteratorState *pIter = ReadScriptIterator(pContext, (Handle_t)params[1]);
	if (!pIter) return 0;

	if (!pIter->hasValue)
	{
		pContext->ThrowNativeError("No iteration value available (call Next() first)");
		return 0;
	}

	char *buffer;
	pContext->LocalToString(params[2], &buffer);
	int maxlen = params[3];

	return VariantMarshal::ReadVariantString(pIter->value, buffer, maxlen);
}

// native ScriptHandle ScriptIterator.GetValueHScript();
static cell_t Native_IterGetValueHScript(IPluginContext *pContext, const cell_t *params)
{
	ScriptIteratorState *pIter = ReadScriptIterator(pContext, (Handle_t)params[1]);
	if (!pIter) return BAD_HANDLE;

	if (!pIter->hasValue)
	{
		pContext->ThrowNativeError("No iteration value available (call Next() first)");
		return BAD_HANDLE;
	}

	HSCRIPT h = VariantMarshal::ReadVariantHScript(pIter->value);
	if (!h)
		return BAD_HANDLE;

	pIter->value = ScriptVariant_t();

	return (cell_t)CreateHScriptHandle(pContext, h, HScriptType::Table, HScriptOwnership::Owned);
}

// native int ScriptIterator.GetValueEntity();
static cell_t Native_IterGetValueEntity(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return -1;

	ScriptIteratorState *pIter = ReadScriptIterator(pContext, (Handle_t)params[1]);
	if (!pIter) return -1;

	if (!pIter->hasValue)
	{
		pContext->ThrowNativeError("No iteration value available (call Next() first)");
		return -1;
	}

	return VariantToEntityIndex(pVM, pIter->value);
}

// ScriptContext methodmap (for VScript -> SP callbacks)
static ScriptContext *ReadScriptContext(IPluginContext *pContext, Handle_t hndl)
{
	HandleSecurity sec(pContext->GetIdentity(), myself->GetIdentity());
	ScriptContext *pCtx = nullptr;
	HandleError err = handlesys->ReadHandle(hndl, g_VScriptExt.GetScriptContextHandleType(), &sec, (void **)&pCtx);

	if (err != HandleError_None)
	{
		pContext->ThrowNativeError("Invalid ScriptContext handle (error %d)", err);
		return nullptr;
	}

	return pCtx;
}

template <int N, void (ScriptContext::*GetFn)(int, float *) const>
static cell_t Native_ScriptContext_GetArgFloatArray(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	cell_t *out;
	pContext->LocalToPhysAddr(params[3], &out);

	float fbuf[N];
	(pCtx->*GetFn)(params[2], fbuf);
	FloatsToCells<N>(fbuf, out);
	return 0;
}

template <int N, void (ScriptContext::*SetFn)(const float *)>
static cell_t Native_ScriptContext_SetReturnFloatArray(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	cell_t *cells;
	pContext->LocalToPhysAddr(params[2], &cells);

	float fbuf[N];
	CellsToFloats<N>(cells, fbuf);
	(pCtx->*SetFn)(fbuf);
	return 0;
}

// native int ScriptContext.ArgCount.get();
static cell_t Native_ScriptContext_ArgCount(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	return pCtx->GetArgCount();
}

// native int ScriptContext.Entity.get();
static cell_t Native_ScriptContext_Entity(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return -1;

	return pCtx->GetCallerEntity();
}

// native ScriptFieldType ScriptContext.GetArgType(int arg);
static cell_t Native_ScriptContext_GetArgType(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	return (cell_t)pCtx->GetArgType(params[2]);
}

// native int ScriptContext.GetArgInt(int arg);
static cell_t Native_ScriptContext_GetArgInt(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	return pCtx->GetArgInt(params[2]);
}

// native float ScriptContext.GetArgFloat(int arg);
static cell_t Native_ScriptContext_GetArgFloat(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	return sp_ftoc(pCtx->GetArgFloat(params[2]));
}

// native bool ScriptContext.GetArgBool(int arg);
static cell_t Native_ScriptContext_GetArgBool(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	return pCtx->GetArgBool(params[2]);
}

// native void ScriptContext.GetArgString(int arg, char[] buffer, int maxlen);
static cell_t Native_ScriptContext_GetArgString(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	char *buffer;
	pContext->LocalToString(params[3], &buffer);
	int maxlen = params[4];

	return pCtx->GetArgString(params[2], buffer, maxlen);
}

// native ScriptHandle ScriptContext.GetArgHScript(int arg);
static cell_t Native_ScriptContext_GetArgHScript(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	HSCRIPT h = pCtx->GetArgHScript(params[2]);
	if (!h)
		return BAD_HANDLE;

	return (cell_t)pCtx->CreateTrackedHScriptHandle(h);
}

// native int ScriptContext.GetArgEntity(int arg);
static cell_t Native_ScriptContext_GetArgEntity(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return -1;

	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return -1;

	HSCRIPT h = pCtx->GetArgHScript(params[2]);
	if (!h)
		return -1;

	return HScriptToEntityIndex(pVM, h);
}

// native void ScriptContext.SetReturnInt(int value);
static cell_t Native_ScriptContext_SetReturnInt(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	pCtx->SetReturnInt(params[2]);
	return 0;
}

// native void ScriptContext.SetReturnFloat(float value);
static cell_t Native_ScriptContext_SetReturnFloat(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	pCtx->SetReturnFloat(sp_ctof(params[2]));
	return 0;
}

// native void ScriptContext.SetReturnBool(bool value);
static cell_t Native_ScriptContext_SetReturnBool(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	pCtx->SetReturnBool(params[2] != 0);
	return 0;
}

// native void ScriptContext.SetReturnString(const char[] value);
static cell_t Native_ScriptContext_SetReturnString(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	char *value;
	pContext->LocalToString(params[2], &value);

	pCtx->SetReturnString(value);
	return 0;
}

// native void ScriptContext.SetReturnHScript(ScriptHandle value);
static cell_t Native_ScriptContext_SetReturnHScript(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	if (params[2] == 0)
	{
		pCtx->SetReturnHScript(nullptr);
		return 0;
	}

	HScriptHandle *pHandle = ReadHScriptHandle(pContext, (Handle_t)params[2]);
	if (!pHandle) return 0;

	pCtx->SetReturnHScript(pHandle->hScript);

	// Detach ownership so the plugin can safely delete the handle without freeing the underlying HSQOBJECT.
	// PushVariant reads it after Dispatch returns.
	pHandle->ownership = HScriptOwnership::Borrowed;

	return 0;
}

// native void ScriptContext.SetReturnEntity(int entity);
static cell_t Native_ScriptContext_SetReturnEntity(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;

	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	FindEntityScriptOffsets();
	if (s_offsetHScriptInstance == -1)
		return pContext->ThrowNativeError("Could not find entity hscript instance offset");

	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[2]);
	if (!pEntity)
		return pContext->ThrowNativeError("Invalid entity index %d", params[2]);

	HSCRIPT hScript = EnsureEntityScriptInstance(pVM, pEntity);
	if (!hScript)
		return pContext->ThrowNativeError("Failed to create script instance for entity %d", params[2]);

	pCtx->SetReturnHScript(hScript);
	return 0;
}

// native void ScriptContext.SetReturnNull();
static cell_t Native_ScriptContext_SetReturnNull(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	pCtx->SetReturnNull();
	return 0;
}

// native void ScriptContext.RaiseException(const char[] format, any ...);
static cell_t Native_ScriptContext_RaiseException(IPluginContext *pContext, const cell_t *params)
{
	ScriptContext *pCtx = ReadScriptContext(pContext, params[1]);
	if (!pCtx) return 0;

	if (params[0] > 2)
	{
		char buffer[512];
		smutils->FormatString(buffer, sizeof(buffer), pContext, params, 2);
		pCtx->RaiseException(buffer);
	}
	else
	{
		char *text;
		pContext->LocalToString(params[2], &text);
		pCtx->RaiseException(text);
	}

	return 0;
}

// ScriptCall methodmap
static ScriptCallContext *ReadScriptCall(IPluginContext *pContext, Handle_t hndl)
{
	HandleSecurity sec(pContext->GetIdentity(), myself->GetIdentity());
	ScriptCallContext *pCall = nullptr;
	HandleError err = handlesys->ReadHandle(hndl, g_VScriptExt.GetScriptCallHandleType(), &sec, (void **)&pCall);

	if (err != HandleError_None)
	{
		pContext->ThrowNativeError("Invalid ScriptCall handle (error %d)", err);
		return nullptr;
	}

	return pCall;
}

static ScriptCallContext *ReadExecutedScriptCall(IPluginContext *pContext, Handle_t hndl)
{
	ScriptCallContext *pCall = ReadScriptCall(pContext, hndl);
	if (!pCall) return nullptr;

	if (!pCall->hasExecuted)
	{
		pContext->ThrowNativeError("ScriptCall has not been executed yet");
		return nullptr;
	}

	return pCall;
}

template <int N, void (*ReadFn)(const ScriptVariant_t &, float *)>
static cell_t Native_ScriptCall_GetReturnFloatArray(IPluginContext *pContext, const cell_t *params)
{
	ScriptCallContext *pCall = ReadExecutedScriptCall(pContext, params[1]);
	if (!pCall) return 0;

	cell_t *out;
	pContext->LocalToPhysAddr(params[2], &out);

	float fbuf[N];
	ReadFn(pCall->returnValue, fbuf);
	FloatsToCells<N>(fbuf, out);
	return 0;
}

// native ScriptCall(const char[] name, ScriptFieldType returnType = ScriptField_Void, ScriptFieldType ...);
static cell_t Native_ScriptCall_Ctor(IPluginContext *pContext, const cell_t *params)
{
	char *name;
	pContext->LocalToString(params[1], &name);

	if (!name || !name[0])
		return pContext->ThrowNativeError("Function name cannot be empty");

	if (!IsValidSPFieldType(params[2]))
		return pContext->ThrowNativeError("Invalid return type %d", params[2]);
	SPFieldType returnType = (SPFieldType)params[2];

	int numParams = params[0] - 2;
	std::vector<SPFieldType> paramTypes;
	for (int i = 0; i < numParams; i++)
	{
		cell_t *addr;
		pContext->LocalToPhysAddr(params[3 + i], &addr);
		if (!IsValidSPFieldType(*addr))
			return pContext->ThrowNativeError("Invalid parameter type %d at index %d", *addr, i);
		paramTypes.push_back((SPFieldType)*addr);
	}

	ScriptCallContext *pCall = new ScriptCallContext(name, returnType, std::move(paramTypes));

	Handle_t hndl = handlesys->CreateHandle(
		g_VScriptExt.GetScriptCallHandleType(),
		pCall,
		pContext->GetIdentity(),
		myself->GetIdentity(),
		nullptr);

	if (hndl == BAD_HANDLE)
	{
		delete pCall;
		return BAD_HANDLE;
	}

	return (cell_t)hndl;
}

static bool MarshalOneArg( IPluginContext *pContext, const cell_t *params, int paramIndex, SPFieldType type, ScriptVariant_t &out)
{
	switch (type)
	{
		case SPFieldType::Int:
		{
			cell_t *addr;
			pContext->LocalToPhysAddr(params[paramIndex], &addr);
			VariantMarshal::WriteVariantInt(out, *addr);
			break;
		}
		case SPFieldType::Float:
		{
			cell_t *addr;
			pContext->LocalToPhysAddr(params[paramIndex], &addr);
			VariantMarshal::WriteVariantFloat(out, sp_ctof(*addr));
			break;
		}
		case SPFieldType::Bool:
		{
			cell_t *addr;
			pContext->LocalToPhysAddr(params[paramIndex], &addr);
			VariantMarshal::WriteVariantBool(out, *addr != 0);
			break;
		}
		case SPFieldType::String:
		{
			char *str;
			pContext->LocalToString(params[paramIndex], &str);
			VariantMarshal::WriteVariantString(out, str, false);
			break;
		}
		case SPFieldType::HScript:
		{
			cell_t *addr;
			pContext->LocalToPhysAddr(params[paramIndex], &addr);
			HSCRIPT h;
			if (!ReadOptionalHScriptParam(pContext, *addr, h))
				return false;
			VariantMarshal::WriteVariantHScript(out, h);
			break;
		}
		case SPFieldType::Vector:
		{
			cell_t *cells;
			pContext->LocalToPhysAddr(params[paramIndex], &cells);
			float fbuf[3];
			CellsToFloats<3>(cells, fbuf);
			VariantMarshal::WriteVariantVector(out, fbuf);
			break;
		}
		case SPFieldType::Vector2D:
		{
			cell_t *cells;
			pContext->LocalToPhysAddr(params[paramIndex], &cells);
			float fbuf[2];
			CellsToFloats<2>(cells, fbuf);
			VariantMarshal::WriteVariantVector2D(out, fbuf);
			break;
		}
		case SPFieldType::Quaternion:
		{
			cell_t *cells;
			pContext->LocalToPhysAddr(params[paramIndex], &cells);
			float fbuf[4];
			CellsToFloats<4>(cells, fbuf);
			VariantMarshal::WriteVariantQuaternion(out, fbuf);
			break;
		}
		case SPFieldType::Variant:
			pContext->ThrowNativeError("ScriptField_Variant cannot be used as a parameter type; use a concrete type instead");
			return false;
		case SPFieldType::Void:
		default:
			VariantMarshal::WriteVariantNull(out);
			break;
	}
	return true;
}

// Extra arguments beyond declared types are consumed as alternating (ScriptFieldType, value) pairs
static bool MarshalVariadicArgs(IPluginContext *pContext, const cell_t *params, int firstArgParam, const std::vector<SPFieldType> &paramTypes, std::vector<ScriptVariant_t> &outArgs)
{
	int numNativeArgs = params[0] - (firstArgParam - 1);
	int declaredCount = (int)paramTypes.size();

	if (numNativeArgs < declaredCount)
	{
		pContext->ThrowNativeError("Expected at least %d argument(s), got %d", declaredCount, numNativeArgs);
		return false;
	}

	int extraNativeArgs = numNativeArgs - declaredCount;
	if (extraNativeArgs % 2 != 0)
	{
		pContext->ThrowNativeError("Extra variadic arguments must be alternating ScriptFieldType and value arguments; got %d extra arg(s)", extraNativeArgs);
		return false;
	}

	int extraArgCount = extraNativeArgs / 2;
	outArgs.resize(declaredCount + extraArgCount);

	for (int i = 0; i < declaredCount; i++)
	{
		if (!MarshalOneArg(pContext, params, firstArgParam + i, paramTypes[i], outArgs[i]))
			return false;
	}

	for (int i = 0; i < extraArgCount; i++)
	{
		int pairBase = firstArgParam + declaredCount + (i * 2);

		cell_t *typeAddr;
		pContext->LocalToPhysAddr(params[pairBase], &typeAddr);

		if (!IsValidSPFieldType(*typeAddr))
		{
			pContext->ThrowNativeError("Invalid field type %d for extra argument %d", *typeAddr, i);
			return false;
		}

		SPFieldType type = (SPFieldType)*typeAddr;
		if (!MarshalOneArg(pContext, params, pairBase + 1, type, outArgs[declaredCount + i]))
			return false;
	}

	return true;
}

// native ScriptStatus ScriptCall.Execute(any ...);
static cell_t Native_ScriptCall_Execute(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;

	ScriptCallContext *pCall = ReadScriptCall(pContext, params[1]);
	if (!pCall) return 0;

	std::vector<ScriptVariant_t> args;
	if (!MarshalVariadicArgs(pContext, params, 2, pCall->paramTypes, args))
		return 0;

	ScriptStatus_t result = pCall->Execute(pVM, args.empty() ? nullptr : args.data(), args.size(), nullptr);

	for (ScriptVariant_t &arg : args)
		arg.Free();

	return (cell_t)result;
}

// native ScriptStatus ScriptCall.ExecuteInScope(ScriptHandle scope, any ...);
static cell_t Native_ScriptCall_ExecuteInScope(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return 0;

	ScriptCallContext *pCall = ReadScriptCall(pContext, params[1]);
	if (!pCall) return 0;

	HSCRIPT hScope;
	if (!ReadHScriptParam(pContext, params[2], hScope))
		return 0;

	std::vector<ScriptVariant_t> args;
	if (!MarshalVariadicArgs(pContext, params, 3, pCall->paramTypes, args))
		return 0;

	ScriptStatus_t result = pCall->Execute(pVM, args.empty() ? nullptr : args.data(), args.size(), hScope);

	for (ScriptVariant_t &arg : args)
		arg.Free();

	return (cell_t)result;
}

// native ScriptFieldType ScriptCall.ReturnType.get();
static cell_t Native_ScriptCall_ReturnType(IPluginContext *pContext, const cell_t *params)
{
	ScriptCallContext *pCall = ReadExecutedScriptCall(pContext, params[1]);
	if (!pCall) return 0;
	return (cell_t)VariantMarshal::ReadVariantType(pCall->returnValue);
}

// native int ScriptCall.GetReturnInt();
static cell_t Native_ScriptCall_GetReturnInt(IPluginContext *pContext, const cell_t *params)
{
	ScriptCallContext *pCall = ReadExecutedScriptCall(pContext, params[1]);
	if (!pCall) return 0;
	return VariantMarshal::ReadVariantInt(pCall->returnValue);
}

// native float ScriptCall.GetReturnFloat();
static cell_t Native_ScriptCall_GetReturnFloat(IPluginContext *pContext, const cell_t *params)
{
	ScriptCallContext *pCall = ReadExecutedScriptCall(pContext, params[1]);
	if (!pCall) return 0;
	return sp_ftoc(VariantMarshal::ReadVariantFloat(pCall->returnValue));
}

// native bool ScriptCall.GetReturnBool();
static cell_t Native_ScriptCall_GetReturnBool(IPluginContext *pContext, const cell_t *params)
{
	ScriptCallContext *pCall = ReadExecutedScriptCall(pContext, params[1]);
	if (!pCall) return 0;
	return VariantMarshal::ReadVariantBool(pCall->returnValue);
}

// native int ScriptCall.GetReturnString(char[] buffer, int maxlen);
static cell_t Native_ScriptCall_GetReturnString(IPluginContext *pContext, const cell_t *params)
{
	ScriptCallContext *pCall = ReadExecutedScriptCall(pContext, params[1]);
	if (!pCall) return 0;

	char *buffer;
	pContext->LocalToString(params[2], &buffer);
	return VariantMarshal::ReadVariantString(pCall->returnValue, buffer, params[3]);
}

// native ScriptHandle ScriptCall.GetReturnHScript();
static cell_t Native_ScriptCall_GetReturnHScript(IPluginContext *pContext, const cell_t *params)
{
	ScriptCallContext *pCall = ReadExecutedScriptCall(pContext, params[1]);
	if (!pCall) return 0;

	HSCRIPT h = VariantMarshal::ReadVariantHScript(pCall->returnValue);
	if (!h)
		return BAD_HANDLE;

	// Transfer ownership. memset because Free() and operator= both trigger SV_FREE deallocation
	memset(&pCall->returnValue, 0, sizeof(pCall->returnValue));

	return (cell_t)CreateHScriptHandle(pContext, h, HScriptType::Table, HScriptOwnership::Owned);
}

// native int ScriptCall.GetReturnEntity();
static cell_t Native_ScriptCall_GetReturnEntity(IPluginContext *pContext, const cell_t *params)
{
	IScriptVM *pVM = GetVMOrThrow(pContext);
	if (!pVM) return -1;

	ScriptCallContext *pCall = ReadExecutedScriptCall(pContext, params[1]);
	if (!pCall) return -1;

	HSCRIPT h = VariantMarshal::ReadVariantHScript(pCall->returnValue);
	if (!h)
		return -1;

	return HScriptToEntityIndex(pVM, h);
}

// native bool ScriptCall.IsReturnNull();
static cell_t Native_ScriptCall_IsReturnNull(IPluginContext *pContext, const cell_t *params)
{
	ScriptCallContext *pCall = ReadExecutedScriptCall(pContext, params[1]);
	if (!pCall) return 0;
	return pCall->returnValue.IsNull();
}

const sp_nativeinfo_t g_VScriptNatives[] =
{
	// VM lifecycle
	{ "VScript_IsVMInitialized",           Native_IsVMInitialized },
	{ "VScript_Run",                       Native_Run },

	// Compilation
	{ "VScript_CompileScript",             Native_CompileScript },
	{ "VScript_RunScript",                 Native_RunScript },

	// Scopes/Tables
	{ "VScript_CreateScope",               Native_CreateScope },
	{ "VScript_CreateTable",               Native_CreateTable },

	// Function registration
	{ "VScript_RegisterFunction",          Native_RegisterFunction },
	{ "VScript_RegisterClassFunction",     Native_RegisterClassFunction },
	{ "VScript_UnregisterFunction",        Native_UnregisterFunction },
	{ "VScript_UnregisterClassFunction",   Native_UnregisterClassFunction },

	// Entity integration
	{ "VScript_GetEntityScriptScope",      Native_GetEntityScriptScope },
	{ "VScript_EntityToHScript",           Native_EntityToHScript },
	{ "VScript_HScriptToEntity",           Native_HScriptToEntity },

	// Utility
	{ "VScript_GenerateUniqueKey",         Native_GenerateUniqueKey },
	{ "VScript_AddSearchPath",             Native_AddSearchPath },

	// ScriptHandle methodmap
	{ "ScriptHandle.GetType",              Native_GetValueType },
	{ "ScriptHandle.HasKey",               Native_ValueExists },
	{ "ScriptHandle.Length.get",           Native_GetNumTableEntries },
	{ "ScriptHandle.GetInt",               Native_GetValueInt },
	{ "ScriptHandle.GetFloat",             Native_GetValueFloat },
	{ "ScriptHandle.GetBool",              Native_GetValueBool },
	{ "ScriptHandle.GetString",            Native_GetValueString },
	{ "ScriptHandle.GetVector",            Native_GetValueFloatArray<3, VariantMarshal::ReadVariantVector> },
	{ "ScriptHandle.GetVector2D",          Native_GetValueFloatArray<2, VariantMarshal::ReadVariantVector2D> },
	{ "ScriptHandle.GetQuaternion",        Native_GetValueFloatArray<4, VariantMarshal::ReadVariantQuaternion> },
	{ "ScriptHandle.GetHScript",           Native_GetValueHScript },
	{ "ScriptHandle.GetEntity",            Native_GetValueEntity },
	{ "ScriptHandle.SetInt",               Native_SetValueInt },
	{ "ScriptHandle.SetFloat",             Native_SetValueFloat },
	{ "ScriptHandle.SetBool",              Native_SetValueBool },
	{ "ScriptHandle.SetString",            Native_SetValueString },
	{ "ScriptHandle.SetVector",            Native_SetValueFloatArray<3, VariantMarshal::WriteVariantVector> },
	{ "ScriptHandle.SetVector2D",          Native_SetValueFloatArray<2, VariantMarshal::WriteVariantVector2D> },
	{ "ScriptHandle.SetQuaternion",        Native_SetValueFloatArray<4, VariantMarshal::WriteVariantQuaternion> },
	{ "ScriptHandle.SetHScript",           Native_SetValueHScript },
	{ "ScriptHandle.SetNull",              Native_SetValueNull },
	{ "ScriptHandle.DeleteKey",            Native_ClearValue },
	{ "ScriptHandle.Iterate",              Native_Iterate },
	{ "ScriptHandle.GetNextKey",           Native_GetNextKey },
	{ "ScriptHandle.LookupFunction",       Native_LookupFunction },

	// ScriptIterator methodmap
	{ "ScriptIterator.Next",               Native_IterNext },
	{ "ScriptIterator.KeyType.get",        Native_IterKeyType },
	{ "ScriptIterator.GetKeyString",       Native_IterGetKeyString },
	{ "ScriptIterator.GetKeyInt",          Native_IterGetKeyInt },
	{ "ScriptIterator.ValueType.get",      Native_IterValueType },
	{ "ScriptIterator.GetValueInt",        Native_IterGetValueInt },
	{ "ScriptIterator.GetValueBool",       Native_IterGetValueBool },
	{ "ScriptIterator.GetValueFloat",      Native_IterGetValueFloat },
	{ "ScriptIterator.GetValueString",     Native_IterGetValueString },
	{ "ScriptIterator.GetValueVector",     Native_IterGetValueFloatArray<3, VariantMarshal::ReadVariantVector> },
	{ "ScriptIterator.GetValueVector2D",   Native_IterGetValueFloatArray<2, VariantMarshal::ReadVariantVector2D> },
	{ "ScriptIterator.GetValueQuaternion", Native_IterGetValueFloatArray<4, VariantMarshal::ReadVariantQuaternion> },
	{ "ScriptIterator.GetValueHScript",    Native_IterGetValueHScript },
	{ "ScriptIterator.GetValueEntity",     Native_IterGetValueEntity },

	// ScriptContext methodmap
	{ "ScriptContext.ArgCount.get",        Native_ScriptContext_ArgCount },
	{ "ScriptContext.Entity.get",          Native_ScriptContext_Entity },
	{ "ScriptContext.GetArgType",          Native_ScriptContext_GetArgType },
	{ "ScriptContext.GetArgInt",           Native_ScriptContext_GetArgInt },
	{ "ScriptContext.GetArgFloat",         Native_ScriptContext_GetArgFloat },
	{ "ScriptContext.GetArgBool",          Native_ScriptContext_GetArgBool },
	{ "ScriptContext.GetArgString",        Native_ScriptContext_GetArgString },
	{ "ScriptContext.GetArgVector",        Native_ScriptContext_GetArgFloatArray<3, &ScriptContext::GetArgVector> },
	{ "ScriptContext.GetArgVector2D",      Native_ScriptContext_GetArgFloatArray<2, &ScriptContext::GetArgVector2D> },
	{ "ScriptContext.GetArgQuaternion",    Native_ScriptContext_GetArgFloatArray<4, &ScriptContext::GetArgQuaternion> },
	{ "ScriptContext.GetArgHScript",       Native_ScriptContext_GetArgHScript },
	{ "ScriptContext.GetArgEntity",        Native_ScriptContext_GetArgEntity },
	{ "ScriptContext.SetReturnInt",        Native_ScriptContext_SetReturnInt },
	{ "ScriptContext.SetReturnFloat",      Native_ScriptContext_SetReturnFloat },
	{ "ScriptContext.SetReturnBool",       Native_ScriptContext_SetReturnBool },
	{ "ScriptContext.SetReturnString",     Native_ScriptContext_SetReturnString },
	{ "ScriptContext.SetReturnVector",     Native_ScriptContext_SetReturnFloatArray<3, &ScriptContext::SetReturnVector> },
	{ "ScriptContext.SetReturnVector2D",   Native_ScriptContext_SetReturnFloatArray<2, &ScriptContext::SetReturnVector2D> },
	{ "ScriptContext.SetReturnQuaternion", Native_ScriptContext_SetReturnFloatArray<4, &ScriptContext::SetReturnQuaternion> },
	{ "ScriptContext.SetReturnHScript",    Native_ScriptContext_SetReturnHScript },
	{ "ScriptContext.SetReturnEntity",     Native_ScriptContext_SetReturnEntity },
	{ "ScriptContext.SetReturnNull",       Native_ScriptContext_SetReturnNull },
	{ "ScriptContext.RaiseException",      Native_ScriptContext_RaiseException },

	// ScriptCall methodmap
	{ "ScriptCall.ScriptCall",            Native_ScriptCall_Ctor },
	{ "ScriptCall.Execute",               Native_ScriptCall_Execute },
	{ "ScriptCall.ExecuteInScope",        Native_ScriptCall_ExecuteInScope },
	{ "ScriptCall.ReturnType.get",        Native_ScriptCall_ReturnType },
	{ "ScriptCall.GetReturnInt",          Native_ScriptCall_GetReturnInt },
	{ "ScriptCall.GetReturnFloat",        Native_ScriptCall_GetReturnFloat },
	{ "ScriptCall.GetReturnBool",         Native_ScriptCall_GetReturnBool },
	{ "ScriptCall.GetReturnString",       Native_ScriptCall_GetReturnString },
	{ "ScriptCall.GetReturnVector",       Native_ScriptCall_GetReturnFloatArray<3, VariantMarshal::ReadVariantVector> },
	{ "ScriptCall.GetReturnVector2D",     Native_ScriptCall_GetReturnFloatArray<2, VariantMarshal::ReadVariantVector2D> },
	{ "ScriptCall.GetReturnQuaternion",   Native_ScriptCall_GetReturnFloatArray<4, VariantMarshal::ReadVariantQuaternion> },
	{ "ScriptCall.GetReturnHScript",      Native_ScriptCall_GetReturnHScript },
	{ "ScriptCall.GetReturnEntity",       Native_ScriptCall_GetReturnEntity },
	{ "ScriptCall.IsReturnNull",          Native_ScriptCall_IsReturnNull },

	{ nullptr, nullptr }
};
