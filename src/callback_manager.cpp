#include "callback_manager.h"
#include "script_context.h"

CallbackManager g_CallbackManager;

void CallbackManager::Shutdown()
{
	for (auto &[name, pReg] : m_globalRegistrations)
	{
		delete pReg->pBinding;
		delete pReg;
	}
	m_globalRegistrations.clear();

	for (auto &[className, funcs] : m_classRegistrations)
	{
		for (RegisteredFunction *pReg : funcs)
		{
			delete pReg->pBinding;
			delete pReg;
		}
	}
	m_classRegistrations.clear();
	m_knownClasses.clear();
}

void CallbackManager::OnVMInitialized(IScriptVM *pVM)
{
	for (auto &[name, pReg] : m_globalRegistrations)
	{
		RegisterGlobalWithVM(pReg, pVM);
	}

	// Class functions don't need re-registration here, OnRegisterClass handles it
	for (auto &[className, funcs] : m_classRegistrations)
	{
		for (RegisteredFunction *pReg : funcs)
		{
			pReg->isRegistered = false;
			delete pReg->pBinding;
			pReg->pBinding = nullptr;
		}
	}
}

void CallbackManager::OnVMShutdown()
{
	for (auto &[name, pReg] : m_globalRegistrations)
		pReg->isRegistered = false;

	m_knownClasses.clear();
}

// Class bindings can't be cleaned, they live in engine-owned static descriptors that hold pointers to our strings
void CallbackManager::CleanupStaleRegistrations()
{
	for (auto it = m_globalRegistrations.begin(); it != m_globalRegistrations.end(); )
	{
		RegisteredFunction *pReg = it->second;
		if (!pReg->pCallback)
		{
			delete pReg->pBinding;
			delete pReg;
			it = m_globalRegistrations.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void CallbackManager::PopulateBinding(ScriptFunctionBinding_t *pBinding, const RegisteredFunction *pReg)
{
	pBinding->m_desc.m_pszScriptName = pReg->scriptName.c_str();
	pBinding->m_desc.m_pszFunction = pReg->scriptName.c_str();
	pBinding->m_desc.m_pszDescription = pReg->description.c_str();
	pBinding->m_desc.m_ReturnType = (ScriptDataType_t)VariantMarshal::SPFieldToEngine(pReg->returnType);

	pBinding->m_desc.m_Parameters.RemoveAll();
	for (SPFieldType paramType : pReg->paramTypes)
	{
		pBinding->m_desc.m_Parameters.AddToTail((ScriptDataType_t)VariantMarshal::SPFieldToEngine(paramType));
	}

	pBinding->m_pfnBinding = &CallbackManager::Dispatch;
	pBinding->m_flags = pReg->isMemberFunction ? SF_MEMBER_FUNC : 0;

	pBinding->m_pFunction = ScriptConvertFreeFuncPtrToVoid(pReg);
}

RegisteredFunction *CallbackManager::RegisterFunction(
	const char *name,
	const char *description,
	IPluginFunction *pCallback,
	SPFieldType returnType,
	const std::vector<SPFieldType> &paramTypes)
{
	auto it = m_globalRegistrations.find(name);
	if (it != m_globalRegistrations.end())
	{
		RegisteredFunction *pExisting = it->second;
		pExisting->description = description;
		pExisting->pCallback = pCallback;
		pExisting->returnType = returnType;
		pExisting->paramTypes = paramTypes;

		// Re-register with the VM in case the value was cleared from the root table
		pExisting->isRegistered = false;
		IScriptVM *pVM = g_VScriptExt.GetVM();
		if (pVM)
			RegisterGlobalWithVM(pExisting, pVM);

		return pExisting;
	}

	RegisteredFunction *pReg = new RegisteredFunction();
	pReg->scriptName = name;
	pReg->description = description;
	pReg->pCallback = pCallback;
	pReg->returnType = returnType;
	pReg->paramTypes = paramTypes;
	pReg->pBinding = nullptr;
	pReg->isMemberFunction = false;
	pReg->isRegistered = false;

	m_globalRegistrations[name] = pReg;

	IScriptVM *pVM = g_VScriptExt.GetVM();
	if (pVM)
	{
		RegisterGlobalWithVM(pReg, pVM);
	}

	return pReg;
}

RegisteredFunction *CallbackManager::RegisterClassFunction(
	const char *className,
	const char *name,
	const char *description,
	IPluginFunction *pCallback,
	SPFieldType returnType,
	const std::vector<SPFieldType> &paramTypes)
{
	auto it = m_classRegistrations.find(className);
	if (it != m_classRegistrations.end())
	{
		for (RegisteredFunction *pExisting : it->second)
		{
			if (pExisting->scriptName == name)
			{
				pExisting->description = description;
				pExisting->pCallback = pCallback;
				pExisting->returnType = returnType;
				pExisting->paramTypes = paramTypes;

				if (!pExisting->isRegistered)
				{
					IScriptVM *pVM = g_VScriptExt.GetVM();
					if (pVM)
					{
						auto descIt = m_knownClasses.find(className);
						if (descIt != m_knownClasses.end())
						{
							InjectIntoDescriptor(pExisting, descIt->second);
							RegisterClassFunctionWithVM(pExisting, pVM);
						}
					}
				}

				return pExisting;
			}
		}
	}

	RegisteredFunction *pReg = new RegisteredFunction();
	pReg->scriptName = name;
	pReg->description = description;
	pReg->pCallback = pCallback;
	pReg->returnType = returnType;
	pReg->paramTypes = paramTypes;
	pReg->pBinding = nullptr;
	pReg->className = className;
	pReg->isMemberFunction = true;
	pReg->isRegistered = false;

	m_classRegistrations[className].push_back(pReg);

	IScriptVM *pVM = g_VScriptExt.GetVM();
	if (pVM)
	{
		auto descIt = m_knownClasses.find(className);
		if (descIt != m_knownClasses.end())
		{
			InjectIntoDescriptor(pReg, descIt->second);
			RegisterClassFunctionWithVM(pReg, pVM);
		}
	}

	return pReg;
}

bool CallbackManager::UnregisterFunction(const char *name)
{
	auto it = m_globalRegistrations.find(name);
	if (it == m_globalRegistrations.end() || !it->second->pCallback)
		return false;

	it->second->pCallback = nullptr;

	IScriptVM *pVM = g_VScriptExt.GetVM();
	if (pVM)
		pVM->ClearValue(nullptr, name);

	return true;
}

bool CallbackManager::UnregisterClassFunction(const char *className, const char *name)
{
	auto it = m_classRegistrations.find(className);
	if (it == m_classRegistrations.end())
		return false;

	for (RegisteredFunction *pReg : it->second)
	{
		if (pReg->scriptName == name && pReg->pCallback)
		{
			pReg->pCallback = nullptr;
			return true;
		}
	}

	return false;
}

void CallbackManager::UnregisterAllForContext(IPluginContext *pContext)
{
	IScriptVM *pVM = g_VScriptExt.GetVM();

	// Can't delete bindings (VM holds raw pointers); null the callback to no-op Dispatch
	for (auto &[name, pReg] : m_globalRegistrations)
	{
		if (pReg->pCallback && pReg->pCallback->GetParentContext() == pContext)
		{
			pReg->pCallback = nullptr;

			if (pVM)
				pVM->ClearValue(nullptr, name.c_str());
		}
	}

	for (auto &[className, funcs] : m_classRegistrations)
	{
		for (RegisteredFunction *pReg : funcs)
		{
			if (pReg->pCallback && pReg->pCallback->GetParentContext() == pContext)
				pReg->pCallback = nullptr;
		}
	}
}

void CallbackManager::RegisterGlobalWithVM(RegisteredFunction *pReg, IScriptVM *pVM)
{
	if (pReg->isRegistered)
		return;

	// Reuse the existing binding allocation so that any old Squirrel closures
	// whose upvalues point to this binding still dereference valid memory.
	if (!pReg->pBinding)
		pReg->pBinding = new ScriptFunctionBinding_t();

	PopulateBinding(pReg->pBinding, pReg);

	pVM->RegisterFunction(pReg->pBinding);
	pReg->isRegistered = true;
}

void CallbackManager::InjectIntoDescriptor(RegisteredFunction *pReg, ScriptClassDesc_t *pClassDesc)
{
	if (!pReg->pCallback)
		return;

	// Reuse existing binding if already injected from a previous map
	bool found = false;
	FOR_EACH_VEC(pClassDesc->m_FunctionBindings, j)
	{
		if (pClassDesc->m_FunctionBindings[j].m_desc.m_pszScriptName &&
			V_strcmp(pClassDesc->m_FunctionBindings[j].m_desc.m_pszScriptName, pReg->scriptName.c_str()) == 0)
		{
			PopulateBinding(&pClassDesc->m_FunctionBindings[j], pReg);
			found = true;
			break;
		}
	}

	if (!found)
	{
		int idx = pClassDesc->m_FunctionBindings.AddToTail();
		PopulateBinding(&pClassDesc->m_FunctionBindings[idx], pReg);
	}
}

// Late-registers a class function by creating it as a global, moving the closure
// to the class table via script, propagating to derived classes, then restoring the global.
void CallbackManager::RegisterClassFunctionWithVM(RegisteredFunction *pReg, IScriptVM *pVM)
{
	if (pReg->isRegistered || !pReg->pCallback)
		return;

	// Real name so the closure gets correct stack traces
	ScriptFunctionBinding_t *pBinding = new ScriptFunctionBinding_t();
	PopulateBinding(pBinding, pReg);

	const char *name = pReg->scriptName.c_str();
	ScriptVariant_t savedValue;
	bool hadExisting = pVM->GetValue(nullptr, name, &savedValue);

	// SCRIPT_HIDE suppresses auto-generated docs; we add our own via RegisterLateDocumentation
	const char *pRealDescription = pBinding->m_desc.m_pszDescription;
	pBinding->m_desc.m_pszDescription = SCRIPT_HIDE;

	pVM->RegisterFunction(pBinding);
	pBinding->m_desc.m_pszDescription = pRealDescription;

	char script[1024];
	V_snprintf(script, sizeof(script), "%s.%s <- ::%s", pReg->className.c_str(), name, name);

	ScriptStatus_t result = pVM->Run(script);

	// Propagate to already-existing derived classes
	if (result == SCRIPT_DONE)
	{
		for (auto &[className, pClassDesc] : m_knownClasses)
		{
			if (className == pReg->className)
				continue;

			for (ScriptClassDesc_t *pBase = pClassDesc->m_pBaseDesc; pBase; pBase = pBase->m_pBaseDesc)
			{
				if (pBase->m_pszScriptName && pReg->className == pBase->m_pszScriptName)
				{
					V_snprintf(script, sizeof(script), "%s.%s <- ::%s", className.c_str(), name, name);
					pVM->Run(script);
					break;
				}
			}
		}
	}

	// Grab closure for documentation before we remove it from the root table
	HSCRIPT hRegisteredFunc = VariantMarshal::LookupFunction(pVM, name, nullptr);
	if (hadExisting)
	{
		pVM->SetValue(nullptr, name, savedValue);
		pVM->ReleaseValue(savedValue);
	}
	else
	{
		pVM->ClearValue(nullptr, name);
	}

	if (result != SCRIPT_DONE)
	{
		g_pSM->LogError(myself, "Failed to late-register class function '%s' on '%s'", name, pReg->className.c_str());
		if (hRegisteredFunc)
			pVM->ReleaseFunction(hRegisteredFunc);
		delete pBinding;
		return;
	}

	// Keep the binding alive, the class closure holds a raw pointer to it
	delete pReg->pBinding;
	pReg->pBinding = pBinding;
	pReg->isRegistered = true;

	RegisterLateDocumentation(pReg, hRegisteredFunc, pVM);

	if (hRegisteredFunc)
		pVM->ReleaseFunction(hRegisteredFunc);
}

void CallbackManager::RegisterLateDocumentation(const RegisteredFunction *pReg, HSCRIPT hRegisteredFunc, IScriptVM *pVM)
{
	HSCRIPT hDocFunc = VariantMarshal::LookupFunction(pVM, "RegisterFunctionDocumentation", nullptr);
	if (!hDocFunc)
		return;

	char szName[256];
	V_snprintf(szName, sizeof(szName), "%s::%s", pReg->className.c_str(), pReg->scriptName.c_str());

	char szSignature[512];
	int pos = V_snprintf(szSignature, sizeof(szSignature), "%s %s(",
		ScriptFieldTypeName((int16)VariantMarshal::SPFieldToEngine(pReg->returnType)),
		szName);

	for (size_t i = 0; i < pReg->paramTypes.size() && pos < (int)sizeof(szSignature) - 1; i++)
	{
		if (i > 0)
			pos += V_snprintf(szSignature + pos, sizeof(szSignature) - pos, ", ");
		pos += V_snprintf(szSignature + pos, sizeof(szSignature) - pos, "%s",
			ScriptFieldTypeName((int16)VariantMarshal::SPFieldToEngine(pReg->paramTypes[i])));
	}
	V_snprintf(szSignature + pos, sizeof(szSignature) - pos, ")");

	ScriptVariant_t args[4];
	args[0] = ScriptVariant_t(hRegisteredFunc);
	args[1] = ScriptVariant_t(szName, false);
	args[2] = ScriptVariant_t(szSignature, false);
	args[3] = ScriptVariant_t(pReg->description.c_str(), false);

	pVM->ExecuteFunction(hDocFunc, args, 4, nullptr, nullptr, true);
	pVM->ReleaseFunction(hDocFunc);
}

void CallbackManager::OnRegisterClass(ScriptClassDesc_t *pClassDesc)
{
	if (!pClassDesc)
		return;

	if (pClassDesc->m_pszScriptName)
		m_knownClasses[pClassDesc->m_pszScriptName] = pClassDesc;

	if (m_classRegistrations.empty())
		return;

	// Walk up the class hierarchy to inject functions registered for any ancestor.
	// e.g. a function registered for CBaseEntity is injected when CTFPlayer is registered.
	ScriptClassDesc_t *pCurrent = pClassDesc;
	while (pCurrent)
	{
		if (pCurrent->m_pszScriptName)
		{
			auto it = m_classRegistrations.find(pCurrent->m_pszScriptName);
			if (it != m_classRegistrations.end())
			{
				for (RegisteredFunction *pReg : it->second)
				{
					if (pReg->isRegistered || !pReg->pCallback)
						continue;

					InjectIntoDescriptor(pReg, pCurrent);
					pReg->isRegistered = true;
				}
			}
		}
		pCurrent = pCurrent->m_pBaseDesc;
	}
}

bool CallbackManager::Dispatch(
	ScriptFunctionBindingStorageType_t pFunction,
	void *pContext,
	ScriptVariant_t *pArguments,
	int nArguments,
	ScriptVariant_t *pReturn)
{
	RegisteredFunction *pReg = ScriptConvertFreeFuncPtrFromVoid<RegisteredFunction *>(pFunction);

	if (!pReg || !pReg->pCallback)
	{
		if (pReturn)
			pReturn->Free();
		return true;
	}

	ScriptContext ctx(pArguments, nArguments, pReturn, pReg, pContext);

	Handle_t hContext = handlesys->CreateHandle(
		g_VScriptExt.GetScriptContextHandleType(),
		&ctx,
		pReg->pCallback->GetParentContext()->GetIdentity(),
		myself->GetIdentity(),
		nullptr);

	if (hContext == BAD_HANDLE)
		return true;

	pReg->pCallback->PushCell(hContext);
	pReg->pCallback->Execute(nullptr);

	ctx.FlushReturn();
	ctx.FreeTrackedHandles();

	HandleSecurity sec(pReg->pCallback->GetParentContext()->GetIdentity(), myself->GetIdentity());
	handlesys->FreeHandle(hContext, &sec);

	return true;
}
