#include "extension.h"
#include "natives.h"
#include "script_context.h"
#include "callback_manager.h"

#include <khook.hpp>

CVScriptExtension g_VScriptExt;
CGlobalVars *gpGlobals = nullptr;
uint32_t g_vmGeneration = 0; // Used by ScriptCallContext to invalidate cached function lookups across map changes
SMEXT_LINK(&g_VScriptExt);

static KHook::Virtual<IScriptManager, IScriptVM *, ScriptLanguage_t> g_HookCreateVM;
static KHook::Virtual<IScriptManager, void, IScriptVM *> g_HookDestroyVM;
static KHook::Virtual<IScriptVM, bool, ScriptClassDesc_t *> g_HookRegisterClass;
static KHook::Virtual<IScriptVM, void, ScriptErrorFunc_t> g_HookSetErrorCallback;

static void OnScriptOutput(const char *pszText)
{
	IForward *pFwd = g_VScriptExt.GetOnScriptPrintForward();
	if (pFwd && pFwd->GetFunctionCount() > 0)
	{
		pFwd->PushString(pszText);
		pFwd->Execute(nullptr);
	}
}

static ScriptErrorFunc_t g_pOriginalErrorCallback = nullptr;

static bool OnScriptError(ScriptErrorLevel_t eLevel, const char *pszText)
{
	IForward *pFwd = g_VScriptExt.GetOnScriptErrorForward();
	if (pFwd && pFwd->GetFunctionCount() > 0)
	{
		pFwd->PushCell((cell_t)eLevel);
		pFwd->PushString(pszText);
		pFwd->Execute(nullptr);
	}

	if (g_pOriginalErrorCallback)
		return g_pOriginalErrorCallback(eLevel, pszText);

	return false;
}

KHook::Return<IScriptVM *> CVScriptExtension::Hook_CreateVM(IScriptManager *pManager, ScriptLanguage_t language)
{
	IScriptVM *pVM = g_HookCreateVM.CallOriginal(pManager, language);
	OnVMCreated(pVM);
	return {KHook::Action::Supersede, pVM};
}

KHook::Return<void> CVScriptExtension::Hook_DestroyVM(IScriptManager *pManager, IScriptVM *pVM)
{
	OnVMDestroyed(pVM);
	g_HookDestroyVM.CallOriginal(pManager, pVM);
	OnVMDestroyedPost();
	return {KHook::Action::Supersede};
}

KHook::Return<bool> CVScriptExtension::Hook_RegisterClass(IScriptVM *pVM, ScriptClassDesc_t *pClassDesc)
{
	g_CallbackManager.OnRegisterClass(pClassDesc);
	return {KHook::Action::Ignore};
}

KHook::Return<void> CVScriptExtension::Hook_SetErrorCallback(IScriptVM *pVM, ScriptErrorFunc_t pFunc)
{
	g_pOriginalErrorCallback = pFunc;
	g_HookSetErrorCallback.CallOriginal(pVM, &OnScriptError);
	return {KHook::Action::Supersede};
}

void CVScriptExtension::InstallManagerHooks()
{
	g_HookCreateVM.AddContext(this, &CVScriptExtension::Hook_CreateVM, nullptr);
	g_HookCreateVM.Configure(&IScriptManager::CreateVM);

	g_HookDestroyVM.AddContext(this, &CVScriptExtension::Hook_DestroyVM, nullptr);
	g_HookDestroyVM.Configure(&IScriptManager::DestroyVM);

	g_HookRegisterClass.AddContext(this, &CVScriptExtension::Hook_RegisterClass, nullptr);
	g_HookRegisterClass.Configure(&IScriptVM::RegisterClass);

	g_HookSetErrorCallback.AddContext(this, &CVScriptExtension::Hook_SetErrorCallback, nullptr);
	g_HookSetErrorCallback.Configure(&IScriptVM::SetErrorCallback);

	g_HookCreateVM.Add(m_pScriptManager);
	g_HookDestroyVM.Add(m_pScriptManager);
}

void CVScriptExtension::RemoveManagerHooks()
{
	if (m_pScriptManager)
	{
		g_HookCreateVM.Remove(m_pScriptManager);
		g_HookDestroyVM.Remove(m_pScriptManager);
	}
}

void CVScriptExtension::InstallVMHooks(IScriptVM *pVM)
{
	g_HookRegisterClass.Add(pVM);
	g_HookSetErrorCallback.Add(pVM);
}

void CVScriptExtension::RemoveVMHooks()
{
	if (m_pScriptVM)
	{
		g_HookRegisterClass.Remove(m_pScriptVM);
		g_HookSetErrorCallback.Remove(m_pScriptVM);
	}
}

void CVScriptExtension::OnVMCreated(IScriptVM *pVM)
{
	if (!pVM)
		return;

	m_pScriptVM = pVM;

	InstallVMHooks(pVM);

	pVM->SetOutputCallback(&OnScriptOutput);
	g_HookSetErrorCallback.CallOriginal(pVM, &OnScriptError);

	if (m_pOnVMInit)
		m_pOnVMInit->Execute(nullptr);

	g_CallbackManager.OnVMInitialized(pVM);
}

void CVScriptExtension::OnVMDestroyed(IScriptVM *pVM)
{
	if (pVM && pVM == m_pScriptVM)
	{
		RemoveVMHooks();
		g_pOriginalErrorCallback = nullptr;

		g_vmGeneration++;

		g_CallbackManager.OnVMShutdown();

		if (m_pOnVMShutdown)
			m_pOnVMShutdown->Execute(nullptr);

		ClearEntityHandleCache();

		m_pScriptVM = nullptr;
	}
}

void CVScriptExtension::OnVMDestroyedPost()
{
	g_CallbackManager.CleanupStaleRegistrations();
}

bool CVScriptExtension::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	gpGlobals = ismm->GetCGlobals();

	GET_V_IFACE_CURRENT(GetEngineFactory, m_pScriptManager, IScriptManager, VSCRIPT_INTERFACE_VERSION);

	InstallManagerHooks();

	return true;
}

bool CVScriptExtension::SDK_OnMetamodUnload(char *error, size_t maxlen)
{
	RemoveVMHooks();
	g_pOriginalErrorCallback = nullptr;
	RemoveManagerHooks();

	return true;
}

bool CVScriptExtension::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	sharesys->AddNatives(myself, g_VScriptNatives);
	sharesys->RegisterLibrary(myself, "vscript");

	HandleAccess sec;
	handlesys->InitAccessDefaults(nullptr, &sec);

	m_htHScript = handlesys->CreateType("ScriptHandle", this, 0, nullptr, &sec, myself->GetIdentity(), nullptr);
	m_htScriptContext = handlesys->CreateType("ScriptContext", this, 0, nullptr, &sec, myself->GetIdentity(), nullptr);
	m_htScriptCall = handlesys->CreateType("ScriptCall", this, 0, nullptr, &sec, myself->GetIdentity(), nullptr);
	m_htScriptIterator = handlesys->CreateType("ScriptIterator", this, 0, nullptr, &sec, myself->GetIdentity(), nullptr);

	// HSCRIPT nullptr = root table in the VM
	HScriptHandle *pRoot = new HScriptHandle(nullptr, HScriptType::Table, HScriptOwnership::Borrowed);
	m_hRootTable = handlesys->CreateHandle(m_htHScript, pRoot, myself->GetIdentity(), myself->GetIdentity(), nullptr);

	m_pOnVMInit = forwards->CreateForward("VScript_OnVMInitialized", ET_Ignore, 0, nullptr);
	m_pOnVMShutdown = forwards->CreateForward("VScript_OnVMShutdown", ET_Ignore, 0, nullptr);
	m_pOnScriptPrint = forwards->CreateForward("VScript_OnScriptPrint", ET_Ignore, 1, nullptr, Param_String);
	m_pOnScriptError = forwards->CreateForward("VScript_OnScriptError", ET_Ignore, 2, nullptr, Param_Cell, Param_String);

	plsys->AddPluginsListener(this);

	if (late && !m_pScriptVM)
		g_pSM->LogMessage(myself, "VScript extension loaded late -- VM will be available after next map change.");

	return true;
}

void CVScriptExtension::SDK_OnAllLoaded()
{
	SM_GET_LATE_IFACE(SDKHOOKS, m_pSDKHooks);

	if (m_pSDKHooks)
		m_pSDKHooks->AddEntityListener(this);
}

void CVScriptExtension::SDK_OnUnload()
{
	if (m_pSDKHooks)
	{
		m_pSDKHooks->RemoveEntityListener(this);
		m_pSDKHooks = nullptr;
	}

	plsys->RemovePluginsListener(this);

	g_CallbackManager.Shutdown();

	ClearEntityHandleCache();

	if (m_hRootTable != BAD_HANDLE)
	{
		HandleSecurity sec(myself->GetIdentity(), myself->GetIdentity());
		handlesys->FreeHandle(m_hRootTable, &sec);
		m_hRootTable = BAD_HANDLE;
	}

	auto removeType = [](HandleType_t &ht) {
		if (ht) { handlesys->RemoveType(ht, myself->GetIdentity()); ht = 0; }
	};
	removeType(m_htHScript);
	removeType(m_htScriptContext);
	removeType(m_htScriptCall);
	removeType(m_htScriptIterator);

	auto releaseForward = [](IForward *&fwd) {
		if (fwd) { forwards->ReleaseForward(fwd); fwd = nullptr; }
	};
	releaseForward(m_pOnVMInit);
	releaseForward(m_pOnVMShutdown);
	releaseForward(m_pOnScriptPrint);
	releaseForward(m_pOnScriptError);

	m_pScriptVM = nullptr;
}

void CVScriptExtension::OnPluginLoaded(IPlugin *plugin)
{
	IPluginContext *pContext = plugin->GetBaseContext();

	uint32_t idx;
	if (pContext->FindPubvarByName("ScriptRootTable", &idx) == SP_ERROR_NONE)
	{
		sp_pubvar_t *var = nullptr;
		if (pContext->GetPubvarByIndex(idx, &var) == SP_ERROR_NONE && var)
			*var->offs = (cell_t)m_hRootTable;
	}

	if (!m_pScriptVM)
		return;

	IPluginFunction *pFunc = pContext->GetFunctionByName("VScript_OnVMInitialized");
	if (pFunc)
		pFunc->Execute(nullptr);
}

void CVScriptExtension::OnPluginUnloaded(IPlugin *plugin)
{
	g_CallbackManager.UnregisterAllForContext(plugin->GetBaseContext());
}

void CVScriptExtension::OnEntityCreated(CBaseEntity *pEntity, const char *classname)
{
	m_knownEntities.insert(pEntity);
}

void CVScriptExtension::OnEntityDestroyed(CBaseEntity *pEntity)
{
	m_knownEntities.erase(pEntity);

	HandleSecurity sec(myself->GetIdentity(), myself->GetIdentity());

	auto it = m_cachedEntityInstances.find(pEntity);
	if (it != m_cachedEntityInstances.end())
	{
		handlesys->FreeHandle(it->second, &sec);
		m_cachedEntityInstances.erase(it);
	}

	it = m_cachedEntityScopes.find(pEntity);
	if (it != m_cachedEntityScopes.end())
	{
		handlesys->FreeHandle(it->second, &sec);
		m_cachedEntityScopes.erase(it);
	}
}

void ReleaseOwnedHScript(IScriptVM *pVM, HSCRIPT hScript, HScriptType type)
{
	switch (type)
	{
		case HScriptType::Scope:    pVM->ReleaseScope(hScript); break;
		case HScriptType::Function: pVM->ReleaseFunction(hScript); break;
		case HScriptType::Script:   pVM->ReleaseScript(hScript); break;
		case HScriptType::Value:    pVM->RemoveInstance(hScript); break;
		case HScriptType::Table:    pVM->ReleaseFunction(hScript); break; // sq_release + delete on the HSQOBJECT wrapper
		default: break;
	}
}

void CVScriptExtension::ClearEntityHandleCache()
{
	HandleSecurity sec(myself->GetIdentity(), myself->GetIdentity());

	for (auto &pair : m_cachedEntityInstances)
		handlesys->FreeHandle(pair.second, &sec);
	for (auto &pair : m_cachedEntityScopes)
		handlesys->FreeHandle(pair.second, &sec);

	m_cachedEntityInstances.clear();
	m_cachedEntityScopes.clear();
}

Handle_t CVScriptExtension::GetOrCreateCachedEntityHandle(CBaseEntity *pEntity, HSCRIPT hScript, HScriptType type)
{
	if (!hScript)
		return BAD_HANDLE;

	auto &cache = (type == HScriptType::Scope) ? m_cachedEntityScopes : m_cachedEntityInstances;

	auto it = cache.find(pEntity);
	if (it != cache.end())
	{
		HandleSecurity sec(myself->GetIdentity(), myself->GetIdentity());
		HScriptHandle *pHandle = nullptr;
		if (handlesys->ReadHandle(it->second, m_htHScript, &sec, (void **)&pHandle) == HandleError_None
			&& pHandle->hScript == hScript)
		{
			return it->second;
		}

		handlesys->FreeHandle(it->second, &sec);
		cache.erase(it);
	}

	HScriptHandle *pHandle = new HScriptHandle(hScript, type, HScriptOwnership::Borrowed);

	Handle_t hndl = handlesys->CreateHandle(
		m_htHScript,
		pHandle,
		myself->GetIdentity(),
		myself->GetIdentity(),
		nullptr);

	if (hndl == BAD_HANDLE)
	{
		delete pHandle;
		return BAD_HANDLE;
	}

	cache[pEntity] = hndl;
	return hndl;
}

void CVScriptExtension::OnHandleDestroy(HandleType_t type, void *object)
{
	if (type == m_htHScript)
	{
		HScriptHandle *h = static_cast<HScriptHandle *>(object);

		if (h->ownership == HScriptOwnership::Owned && m_pScriptVM && h->hScript)
			ReleaseOwnedHScript(m_pScriptVM, h->hScript, h->type);

		delete h;
	}
	else if (type == m_htScriptContext)
	{
		// ScriptContext lifetime is managed by the dispatcher
	}
	else if (type == m_htScriptIterator)
	{
		delete static_cast<ScriptIteratorState *>(object);
	}
	else if (type == m_htScriptCall)
	{
		delete static_cast<ScriptCallContext *>(object);
	}
}
