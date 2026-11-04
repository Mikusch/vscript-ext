#include "script_hook.h"
#include "callback_manager.h"

ScriptHookManager g_HookManager;

static ScriptFunctionBinding_t *FindMemberBinding(ScriptClassDesc_t *pClassDesc, const std::string &name)
{
	if (!pClassDesc)
		return nullptr;

	// Search backwards, if a name is bound twice the VM keeps the last binding
	for (int i = pClassDesc->m_FunctionBindings.Count() - 1; i >= 0; i--)
	{
		ScriptFunctionBinding_t &binding = pClassDesc->m_FunctionBindings[i];
		if (binding.m_desc.m_pszScriptName && name == binding.m_desc.m_pszScriptName)
			return &binding;
	}

	return nullptr;
}

static bool IsAttachedTo(const ScriptFunctionBinding_t *pBinding, const HookTarget *pTarget)
{
	return pBinding->m_pfnBinding == &ScriptHookManager::Dispatch
		&& ScriptConvertFreeFuncPtrFromVoid<HookTarget *>(pBinding->m_pFunction) == pTarget;
}

ScriptHookContext::ScriptHookContext(HookTarget *pTarget, void *pInstance, ScriptVariant_t *pArgs, int nArgs, ScriptVariant_t *pReturn)
	: ScriptContext(pArgs, nArgs, pReturn, &pTarget->paramTypes, pTarget->isMemberFunction, pInstance)
	, m_pTarget(pTarget)
	, m_pOriginalArgs(pArgs)
{
	m_returnEngineType = pTarget->engineReturnType;
}

ScriptHookContext::~ScriptHookContext()
{
	ReleaseArgStorage();
}

void ScriptHookContext::ReleaseArgStorage()
{
	HandleSecurity sec(myself->GetIdentity(), myself->GetIdentity());
	for (ArgStorage &storage : m_argStorage)
	{
		if (storage.keepAlive != BAD_HANDLE)
		{
			handlesys->FreeHandle(storage.keepAlive, &sec);
			storage.keepAlive = BAD_HANDLE;
		}
	}
}

int ScriptHookContext::GetArgEngineType(int index) const
{
	if (index < 0 || index >= (int)m_pTarget->engineParamTypes.size())
		return FIELD_TYPEUNKNOWN;

	return m_pTarget->engineParamTypes[index];
}

bool ScriptHookContext::ValidateArg(int index, SPFieldType valueType, char *error, size_t maxlength) const
{
	if (m_bPost)
	{
		smutils->Format(error, maxlength, "Arguments can only be changed in pre hooks");
		return false;
	}

	if (index < 0 || index >= m_nArgs)
	{
		smutils->Format(error, maxlength, "Invalid argument index %d (function has %d argument(s))", index, m_nArgs);
		return false;
	}

	int engineType = GetArgEngineType(index);
	if (!VariantMarshal::CanAssign(engineType, valueType))
	{
		smutils->Format(error, maxlength, "Argument %d has type '%s', which cannot be set from this value type",
			index, ScriptFieldTypeName((int16)engineType));
		return false;
	}

	return true;
}

// Copy on first write so the VM's argument array is never modified
ScriptVariant_t &ScriptHookContext::BeginArgWrite(int index)
{
	if (!m_bArgsModified)
	{
		m_modifiedArgs.resize(m_nArgs);
		memcpy((void *)m_modifiedArgs.data(), (const void *)m_pOriginalArgs, m_nArgs * sizeof(ScriptVariant_t));
		m_argStorage.resize(m_nArgs);
		m_pArgs = m_modifiedArgs.data();
		m_bArgsModified = true;
	}

	ArgStorage &storage = m_argStorage[index];
	if (storage.keepAlive != BAD_HANDLE)
	{
		HandleSecurity sec(myself->GetIdentity(), myself->GetIdentity());
		handlesys->FreeHandle(storage.keepAlive, &sec);
		storage.keepAlive = BAD_HANDLE;
	}

	// The slot may be a shallow copy of a VM-owned value, reset it without freeing
	ScriptVariant_t &slot = m_modifiedArgs[index];
	memset((void *)&slot, 0, sizeof(slot));
	return slot;
}

void ScriptHookContext::SetArgInt(int index, int value)
{
	ScriptVariant_t &slot = BeginArgWrite(index);
	if (GetArgEngineType(index) == FIELD_UINT)
		slot = (uint)value;
	else
		slot = value;
}

void ScriptHookContext::SetArgFloat(int index, float value)
{
	BeginArgWrite(index) = value;
}

void ScriptHookContext::SetArgBool(int index, bool value)
{
	BeginArgWrite(index) = value;
}

void ScriptHookContext::SetArgString(int index, const char *value)
{
	ScriptVariant_t &slot = BeginArgWrite(index);
	ArgStorage &storage = m_argStorage[index];
	storage.str = value;
	slot = storage.str.c_str();
}

void ScriptHookContext::SetArgVector(int index, const float vec[3])
{
	ScriptVariant_t &slot = BeginArgWrite(index);
	ArgStorage &storage = m_argStorage[index];

	if (GetArgEngineType(index) == FIELD_QANGLE)
	{
		storage.ang = QAngle(vec[0], vec[1], vec[2]);
		slot = &storage.ang;
	}
	else
	{
		storage.vec = Vector(vec[0], vec[1], vec[2]);
		slot = &storage.vec;
	}
}

void ScriptHookContext::SetArgVector2D(int index, const float vec[2])
{
	ScriptVariant_t &slot = BeginArgWrite(index);
	ArgStorage &storage = m_argStorage[index];
	storage.vec2 = Vector2D(vec[0], vec[1]);
	slot = &storage.vec2;
}

void ScriptHookContext::SetArgQuaternion(int index, const float quat[4])
{
	ScriptVariant_t &slot = BeginArgWrite(index);
	ArgStorage &storage = m_argStorage[index];
	storage.quat = Quaternion(quat[0], quat[1], quat[2], quat[3]);
	slot = &storage.quat;
}

void ScriptHookContext::SetArgHScript(int index, HSCRIPT value, Handle_t keepAlive)
{
	ScriptVariant_t &slot = BeginArgWrite(index);
	m_argStorage[index].keepAlive = keepAlive;
	slot = value;
}

void ScriptHookContext::SetArgEHandle(int index, CBaseHandle value)
{
	BeginArgWrite(index) = value;
}

void ScriptHookContext::SetArgNull(int index)
{
	ScriptVariant_t &slot = BeginArgWrite(index);

	switch (GetArgEngineType(index))
	{
		case FIELD_HSCRIPT: slot = (HSCRIPT)nullptr; break;
		case FIELD_EHANDLE: slot = CBaseHandle(); break;
		default: break;
	}
}

void ScriptHookContext::BeginPost(bool argsApplied)
{
	m_bPost = true;
	DiscardReturn();

	if (!argsApplied)
		m_pArgs = m_pOriginalArgs;
}

void ScriptHookManager::Init()
{
	smutils->AddGameFrameHook(&ScriptHookManager::OnGameFrame);
}

void ScriptHookManager::Shutdown()
{
	smutils->RemoveGameFrameHook(&ScriptHookManager::OnGameFrame);

	for (auto &[key, pTarget] : m_targets)
	{
		Detach(pTarget);

		for (std::vector<HookCallback *> &callbacks : pTarget->callbacks)
		{
			for (HookCallback *pCallback : callbacks)
				delete pCallback;
		}

		delete pTarget;
	}
	m_targets.clear();

	// Setups are deleted when their handles are destroyed
	for (ScriptHookSetup *pSetup : m_setups)
	{
		pSetup->pTarget = nullptr;
		pSetup->callbacks.clear();
	}

	m_globalBindings.clear();
	m_classes.clear();
	m_bReadyCheckPending = false;
	m_bVMReady = false;
}

void ScriptHookManager::OnVMCreated()
{
	// Natives are registered right after CreateVM, report hooks that failed to attach on the next frame
	m_bVMReady = false;
	m_bReadyCheckPending = true;
	m_pendingReadyGeneration = g_vmGeneration;
}

void ScriptHookManager::OnVMShutdown()
{
	for (auto &[key, pTarget] : m_targets)
	{
		Detach(pTarget);
		pTarget->reported = false;
	}

	m_globalBindings.clear();
	m_classes.clear();
	m_bReadyCheckPending = false;
	m_bVMReady = false;
}

void ScriptHookManager::OnGameFrame(bool simulating)
{
	ScriptHookManager &self = g_HookManager;
	if (!self.m_bReadyCheckPending)
		return;

	self.m_bReadyCheckPending = false;
	if (!IsCurrentGeneration(self.m_pendingReadyGeneration) || !g_VScriptExt.GetVM())
		return;

	self.m_bVMReady = true;

	for (auto &[key, pTarget] : self.m_targets)
	{
		if (pTarget->activeCount > 0 && !pTarget->attached)
			self.ReportUnattached(pTarget);
	}
}

void ScriptHookManager::OnRegisterFunction(ScriptFunctionBinding_t *pBinding)
{
	if (!pBinding || !pBinding->m_desc.m_pszScriptName)
		return;

	// Late-registered class functions pass through the root table temporarily
	if (pBinding->m_pfnBinding == &ScriptHookManager::Dispatch || (pBinding->m_flags & SF_MEMBER_FUNC))
		return;

	// Plugin functions are recorded too, they replace any native of the same name
	m_globalBindings[pBinding->m_desc.m_pszScriptName] = pBinding;

	auto it = m_targets.find(pBinding->m_desc.m_pszScriptName);
	if (it == m_targets.end() || it->second->activeCount == 0)
		return;

	// The binding was re-initialized or replaced
	HookTarget *pTarget = it->second;
	Detach(pTarget);
	Attach(pTarget);

	if (!pTarget->attached && m_bVMReady)
		ReportUnattached(pTarget);
}

void ScriptHookManager::OnRegisterClass(ScriptClassDesc_t *pClassDesc)
{
	if (!pClassDesc || !pClassDesc->m_pszScriptName)
		return;

	m_classes[pClassDesc->m_pszScriptName] = pClassDesc;

	for (auto &[key, pTarget] : m_targets)
	{
		if (pTarget->activeCount == 0 || pTarget->className != pClassDesc->m_pszScriptName)
			continue;

		if (pTarget->attached && pTarget->pClassDesc == pClassDesc)
			continue;

		Detach(pTarget);
		Attach(pTarget);
	}
}

ScriptFunctionBinding_t *ScriptHookManager::FindBinding(HookTarget *pTarget) const
{
	if (pTarget->IsGlobal())
	{
		auto it = m_globalBindings.find(pTarget->functionName);
		return it != m_globalBindings.end() ? it->second : nullptr;
	}

	auto it = m_classes.find(pTarget->className);
	return it != m_classes.end() ? FindMemberBinding(it->second, pTarget->functionName) : nullptr;
}

void ScriptHookManager::Attach(HookTarget *pTarget)
{
	if (pTarget->attached || pTarget->activeCount == 0)
		return;

	ScriptFunctionBinding_t *pBinding = FindBinding(pTarget);
	if (!pBinding || !pBinding->m_pfnBinding)
		return;

	if (pBinding->m_pfnBinding == &CallbackManager::Dispatch || pBinding->m_pfnBinding == &ScriptHookManager::Dispatch)
		return;

	pTarget->engineParamTypes.clear();
	pTarget->paramTypes.clear();
	FOR_EACH_VEC(pBinding->m_desc.m_Parameters, i)
	{
		int type = pBinding->m_desc.m_Parameters[i];
		pTarget->engineParamTypes.push_back(type);
		pTarget->paramTypes.push_back(VariantMarshal::IsKnownEngineType(type) ? VariantMarshal::EngineToSPField(type) : SPFieldType::Variant);
	}
	pTarget->engineReturnType = pBinding->m_desc.m_ReturnType;
	pTarget->isMemberFunction = (pBinding->m_flags & SF_MEMBER_FUNC) != 0;

	pTarget->pfnOriginal = pBinding->m_pfnBinding;
	pTarget->originalFunction = pBinding->m_pFunction;
	pTarget->pGlobalBinding = pTarget->IsGlobal() ? pBinding : nullptr;
	pTarget->pClassDesc = pTarget->IsGlobal() ? nullptr : m_classes[pTarget->className];

	pBinding->m_pfnBinding = &ScriptHookManager::Dispatch;
	pBinding->m_pFunction = ScriptConvertFreeFuncPtrToVoid(pTarget);
	pTarget->attached = true;
}

void ScriptHookManager::Detach(HookTarget *pTarget)
{
	if (!pTarget->attached)
		return;

	ScriptFunctionBinding_t *pBinding = pTarget->IsGlobal()
		? pTarget->pGlobalBinding
		: FindMemberBinding(pTarget->pClassDesc, pTarget->functionName);

	// The owner may have re-initialized the binding
	if (pBinding && IsAttachedTo(pBinding, pTarget))
	{
		pBinding->m_pfnBinding = pTarget->pfnOriginal;
		pBinding->m_pFunction = pTarget->originalFunction;
	}

	pTarget->attached = false;
	pTarget->pGlobalBinding = nullptr;
	pTarget->pClassDesc = nullptr;
}

void ScriptHookManager::ReportUnattached(HookTarget *pTarget)
{
	if (pTarget->reported)
		return;

	pTarget->reported = true;

	char reason[512];
	ScriptFunctionBinding_t *pBinding = FindBinding(pTarget);

	if (pBinding && pBinding->m_pfnBinding == &CallbackManager::Dispatch)
	{
		V_snprintf(reason, sizeof(reason), "the function was registered by a plugin");
	}
	else if (pTarget->IsGlobal())
	{
		V_snprintf(reason, sizeof(reason),
			"no native function named '%s' was registered (functions defined in scripts cannot be hooked)",
			pTarget->functionName.c_str());
	}
	else
	{
		auto it = m_classes.find(pTarget->className);
		if (it == m_classes.end())
		{
			V_snprintf(reason, sizeof(reason), "no script class named '%s' was registered", pTarget->className.c_str());
		}
		else
		{
			V_snprintf(reason, sizeof(reason), "class '%s' has no native function named '%s'",
				pTarget->className.c_str(), pTarget->functionName.c_str());

			for (ScriptClassDesc_t *pBase = it->second->m_pBaseDesc; pBase; pBase = pBase->m_pBaseDesc)
			{
				if (pBase->m_pszScriptName && FindMemberBinding(pBase, pTarget->functionName))
				{
					V_snprintf(reason, sizeof(reason), "the function is inherited from '%s', hook \"%s::%s\" instead",
						pBase->m_pszScriptName, pBase->m_pszScriptName, pTarget->functionName.c_str());
					break;
				}
			}
		}
	}

	g_pSM->LogError(myself, "Cannot hook \"%s\": %s", pTarget->displayName.c_str(), reason);
}

ScriptHookSetup *ScriptHookManager::CreateSetup(const std::string &className, const std::string &functionName)
{
	std::string key = className.empty() ? functionName : className + "::" + functionName;

	HookTarget *&pTarget = m_targets[key];
	if (!pTarget)
	{
		pTarget = new HookTarget();
		pTarget->className = className;
		pTarget->functionName = functionName;
		pTarget->displayName = key;
	}

	ScriptHookSetup *pSetup = new ScriptHookSetup();
	pSetup->pTarget = pTarget;
	m_setups.insert(pSetup);
	return pSetup;
}

void ScriptHookManager::DestroySetup(ScriptHookSetup *pSetup)
{
	m_setups.erase(pSetup);

	if (HookTarget *pTarget = pSetup->pTarget)
	{
		while (!pSetup->callbacks.empty())
			Deactivate(pTarget, pSetup->callbacks.back());

		Purge(pTarget);
	}

	delete pSetup;
}

bool ScriptHookManager::Enable(ScriptHookSetup *pSetup, ScriptHookMode mode, IPluginFunction *pFunc)
{
	HookTarget *pTarget = pSetup->pTarget;
	if (!pTarget)
		return false;

	for (HookCallback *pCallback : pSetup->callbacks)
	{
		if (pCallback->mode == mode && pCallback->pFunc == pFunc)
			return false;
	}

	HookCallback *pCallback = new HookCallback();
	pCallback->pFunc = pFunc;
	pCallback->pSetup = pSetup;
	pCallback->mode = mode;

	pTarget->callbacks[(int)mode].push_back(pCallback);
	pSetup->callbacks.push_back(pCallback);
	pTarget->activeCount++;

	if (!pTarget->attached)
	{
		Attach(pTarget);

		if (!pTarget->attached && m_bVMReady)
			ReportUnattached(pTarget);
	}

	return true;
}

bool ScriptHookManager::Disable(ScriptHookSetup *pSetup, ScriptHookMode mode, IPluginFunction *pFunc)
{
	HookTarget *pTarget = pSetup->pTarget;
	if (!pTarget)
		return false;

	for (HookCallback *pCallback : pSetup->callbacks)
	{
		if (pCallback->mode == mode && pCallback->pFunc == pFunc)
		{
			Deactivate(pTarget, pCallback);
			Purge(pTarget);
			return true;
		}
	}

	return false;
}

void ScriptHookManager::RemoveCallbacksForContext(IPluginContext *pContext)
{
	for (auto &[key, pTarget] : m_targets)
	{
		for (std::vector<HookCallback *> &callbacks : pTarget->callbacks)
		{
			for (HookCallback *pCallback : callbacks)
			{
				if (pCallback->active && pCallback->pFunc->GetParentContext() == pContext)
					Deactivate(pTarget, pCallback);
			}
		}

		Purge(pTarget);
	}
}

// Freed by Purge once no dispatch is using it
void ScriptHookManager::Deactivate(HookTarget *pTarget, HookCallback *pCallback)
{
	if (!pCallback->active)
		return;

	pCallback->active = false;

	if (ScriptHookSetup *pSetup = pCallback->pSetup)
	{
		auto &callbacks = pSetup->callbacks;
		for (auto it = callbacks.begin(); it != callbacks.end(); ++it)
		{
			if (*it == pCallback)
			{
				callbacks.erase(it);
				break;
			}
		}
		pCallback->pSetup = nullptr;
	}

	if (--pTarget->activeCount == 0)
		Detach(pTarget);
}

void ScriptHookManager::Purge(HookTarget *pTarget)
{
	if (pTarget->dispatchDepth > 0)
		return;

	for (std::vector<HookCallback *> &callbacks : pTarget->callbacks)
	{
		size_t kept = 0;
		for (HookCallback *pCallback : callbacks)
		{
			if (pCallback->active)
				callbacks[kept++] = pCallback;
			else
				delete pCallback;
		}
		callbacks.resize(kept);
	}
}

cell_t ScriptHookManager::RunCallbacks(HookTarget *pTarget, ScriptHookMode mode, ScriptHookContext &ctx)
{
	std::vector<HookCallback *> &callbacks = pTarget->callbacks[(int)mode];

	// Purge doesn't run during dispatch, so indices stay valid. Callbacks added meanwhile fire on the next call.
	size_t count = callbacks.size();
	cell_t action = Pl_Continue;

	for (size_t i = 0; i < count; i++)
	{
		HookCallback *pCallback = callbacks[i];
		if (!pCallback->active)
			continue;

		IPluginFunction *pFunc = pCallback->pFunc;
		IdentityToken_t *pIdentity = pFunc->GetParentContext()->GetIdentity();

		Handle_t hContext = handlesys->CreateHandle(
			g_VScriptExt.GetScriptHookContextHandleType(),
			static_cast<ScriptContext *>(&ctx),
			pIdentity,
			myself->GetIdentity(),
			nullptr);

		if (hContext == BAD_HANDLE)
			continue;

		cell_t result = Pl_Continue;
		pFunc->PushCell(hContext);
		if (pFunc->Execute(&result) != SP_ERROR_NONE)
			result = Pl_Continue;

		ctx.FreeTrackedHandles();

		HandleSecurity sec(pIdentity, myself->GetIdentity());
		handlesys->FreeHandle(hContext, &sec);

		if (result > action)
			action = result;

		if (result >= Pl_Stop || ctx.HasException())
			break;
	}

	return action;
}

bool ScriptHookManager::Dispatch(
	ScriptFunctionBindingStorageType_t pFunction,
	void *pContext,
	ScriptVariant_t *pArguments,
	int nArguments,
	ScriptVariant_t *pReturn)
{
	HookTarget *pTarget = ScriptConvertFreeFuncPtrFromVoid<HookTarget *>(pFunction);

	bool result = true;
	pTarget->dispatchDepth++;

	{
		ScriptHookContext ctx(pTarget, pContext, pArguments, nArguments, pReturn);

		cell_t preAction = RunCallbacks(pTarget, ScriptHookMode::Pre, ctx);

		if (!ctx.HasException())
		{
			bool callOriginal = preAction < Pl_Handled;
			bool argsApplied = callOriginal && preAction == Pl_Changed && ctx.ArgsModified();

			if (callOriginal)
			{
				ctx.DiscardReturn();
				result = pTarget->pfnOriginal(pTarget->originalFunction, pContext, argsApplied ? ctx.GetModifiedArgs() : pArguments, nArguments, pReturn);
			}
			else
			{
				ctx.FlushReturn();
			}

			ctx.BeginPost(argsApplied);

			cell_t postAction = RunCallbacks(pTarget, ScriptHookMode::Post, ctx);
			if (postAction >= Pl_Changed)
				ctx.FlushReturn();
		}
	}

	if (--pTarget->dispatchDepth == 0)
		g_HookManager.Purge(pTarget);

	return result;
}
