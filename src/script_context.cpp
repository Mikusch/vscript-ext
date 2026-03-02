#include "script_context.h"
#include "callback_manager.h"

ScriptCallContext::~ScriptCallContext()
{
	IScriptVM *pCurrentVM = g_VScriptExt.GetVM();

	if (hCachedFunction && pCurrentVM && cachedGeneration == g_vmGeneration)
		pCurrentVM->ReleaseFunction(hCachedFunction);

	if (pExecutedVM)
	{
		if (pExecutedVM == pCurrentVM)
			pExecutedVM->ReleaseValue(returnValue);
		else
			returnValue.Free();
	}
}

HSCRIPT ScriptCallContext::ResolveFunction(IScriptVM *pVM, HSCRIPT hScope)
{
	if (cachedGeneration != g_vmGeneration || hCachedScope != hScope)
	{
		// Only release if the VM is still the same one that created the handle
		if (hCachedFunction && cachedGeneration == g_vmGeneration)
			pVM->ReleaseFunction(hCachedFunction);

		hCachedFunction = nullptr;
	}

	if (!hCachedFunction)
	{
		hCachedFunction = VariantMarshal::LookupFunction(pVM, functionName.c_str(), hScope);

		hCachedScope = hScope;
		cachedGeneration = g_vmGeneration;
	}

	return hCachedFunction;
}

ScriptStatus_t ScriptCallContext::Execute(IScriptVM *pVM, ScriptVariant_t *pArgs, int nArgs, HSCRIPT hScope)
{
	if (pExecutedVM)
	{
		if (pExecutedVM == pVM)
			pExecutedVM->ReleaseValue(returnValue);
		else
			returnValue.Free();
	}

	HSCRIPT hFunc = ResolveFunction(pVM, hScope);
	if (!hFunc)
	{
		g_pSM->LogError(myself, "Function '%s' not found", functionName.c_str());
		return SCRIPT_ERROR;
	}

	ScriptStatus_t result = pVM->ExecuteFunction(hFunc, pArgs, nArgs, &returnValue, hScope, true);
	pExecutedVM = pVM;
	hasExecuted = true;
	return result;
}

ScriptContext::ScriptContext(ScriptVariant_t *pArgs, int nArgs, ScriptVariant_t *pReturn, RegisteredFunction *pReg, void *pCallerContext)
	: m_pArgs(pArgs)
	, m_nArgs(nArgs)
	, m_pReturn(pReturn)
	, m_pReg(pReg)
	, m_pCallerContext(pCallerContext)
{}

int ScriptContext::GetCallerEntity() const
{
	if (!m_pCallerContext || !m_pReg || !m_pReg->isMemberFunction)
		return -1;

	// pCallerContext is the ScriptInstance_t::m_pInstance of the caller (EntityToBCompatRef crashes on non-entities)
	if (!g_VScriptExt.IsKnownEntity(m_pCallerContext))
		return -1;

	return gamehelpers->EntityToBCompatRef(static_cast<CBaseEntity *>(m_pCallerContext));
}

SPFieldType ScriptContext::GetArgType(int index) const
{
	if (index < 0 || index >= m_nArgs)
		return SPFieldType::Void;

	if (m_pReg && index < (int)m_pReg->paramTypes.size() && m_pReg->paramTypes[index] != SPFieldType::Variant)
		return m_pReg->paramTypes[index];

	return VariantMarshal::ReadVariantType(m_pArgs[index]);
}

int ScriptContext::GetArgInt(int index) const
{
	if (index < 0 || index >= m_nArgs)
		return 0;

	return VariantMarshal::ReadVariantInt(m_pArgs[index]);
}

float ScriptContext::GetArgFloat(int index) const
{
	if (index < 0 || index >= m_nArgs)
		return 0.0f;
	return VariantMarshal::ReadVariantFloat(m_pArgs[index]);
}

bool ScriptContext::GetArgBool(int index) const
{
	if (index < 0 || index >= m_nArgs)
		return false;
	return VariantMarshal::ReadVariantBool(m_pArgs[index]);
}

int ScriptContext::GetArgString(int index, char *buffer, int maxlen) const
{
	if (index < 0 || index >= m_nArgs)
	{
		buffer[0] = '\0';
		return 0;
	}
	return VariantMarshal::ReadVariantString(m_pArgs[index], buffer, maxlen);
}

void ScriptContext::GetArgVector(int index, float vec[3]) const
{
	if (index < 0 || index >= m_nArgs)
	{
		vec[0] = vec[1] = vec[2] = 0.0f;
		return;
	}
	VariantMarshal::ReadVariantVector(m_pArgs[index], vec);
}

void ScriptContext::GetArgVector2D(int index, float vec[2]) const
{
	if (index < 0 || index >= m_nArgs)
	{
		vec[0] = vec[1] = 0.0f;
		return;
	}
	VariantMarshal::ReadVariantVector2D(m_pArgs[index], vec);
}

void ScriptContext::GetArgQuaternion(int index, float quat[4]) const
{
	if (index < 0 || index >= m_nArgs)
	{
		quat[0] = quat[1] = quat[2] = quat[3] = 0.0f;
		return;
	}
	VariantMarshal::ReadVariantQuaternion(m_pArgs[index], quat);
}

HSCRIPT ScriptContext::GetArgHScript(int index) const
{
	if (index < 0 || index >= m_nArgs)
		return nullptr;
	return VariantMarshal::ReadVariantHScript(m_pArgs[index]);
}

void ScriptContext::SetReturnInt(int value)            { m_return = value; }
void ScriptContext::SetReturnFloat(float value)        { m_return = value; }
void ScriptContext::SetReturnBool(bool value)          { m_return = value; }
void ScriptContext::SetReturnString(const char *value) { m_return = std::string(value); }
void ScriptContext::SetReturnHScript(HSCRIPT value)    { m_return = value; }
void ScriptContext::SetReturnNull()                    { m_return = std::monostate{}; }

void ScriptContext::SetReturnVector(const float vec[3])
{
	m_return = Vec3{{vec[0], vec[1], vec[2]}};
}

void ScriptContext::SetReturnVector2D(const float vec[2])
{
	m_return = Vec2{{vec[0], vec[1]}};
}

void ScriptContext::SetReturnQuaternion(const float quat[4])
{
	m_return = Quat{{quat[0], quat[1], quat[2], quat[3]}};
}

Handle_t ScriptContext::CreateTrackedHScriptHandle(HSCRIPT h)
{
	if (!h)
		return BAD_HANDLE;

	HScriptHandle *pHandle = new HScriptHandle(h, HScriptType::Value, HScriptOwnership::Borrowed);

	HandleSecurity sec(myself->GetIdentity(), myself->GetIdentity());
	HandleError err;
	Handle_t hndl = handlesys->CreateHandleEx(g_VScriptExt.GetHScriptHandleType(), pHandle, &sec, nullptr, &err);

	if (hndl == BAD_HANDLE)
	{
		delete pHandle;
		return BAD_HANDLE;
	}

	m_trackedHandles.push_back(hndl);
	return hndl;
}

void ScriptContext::FreeTrackedHandles()
{
	HandleSecurity sec(myself->GetIdentity(), myself->GetIdentity());
	for (Handle_t h : m_trackedHandles)
		handlesys->FreeHandle(h, &sec);
	m_trackedHandles.clear();
}

void ScriptContext::RaiseException(const char *text)
{
	IScriptVM *pVM = g_VScriptExt.GetVM();
	if (pVM)
	{
		pVM->RaiseException(text);
		m_bHasException = true;
	}
}

namespace {
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;
}

void ScriptContext::FlushReturn()
{
	// The VM ignores return values during exception handling
	if (m_bHasException || !m_return || !m_pReturn)
		return;

	// Pointer types are deep-copied into the variant and leak because TranslateCall never frees returnValue.
	// This is the same leak pattern used by the engine's own template bindings in vscript_templates.h.
	std::visit(overloaded{
		[&](std::monostate)        { VariantMarshal::WriteVariantNull(*m_pReturn); },
		[&](int v)                 { VariantMarshal::WriteVariantInt(*m_pReturn, v); },
		[&](float v)               { VariantMarshal::WriteVariantFloat(*m_pReturn, v); },
		[&](bool v)                { VariantMarshal::WriteVariantBool(*m_pReturn, v); },
		[&](const std::string &v)  { VariantMarshal::WriteVariantString(*m_pReturn, v.c_str(), false); },
		[&](const Vec3 &v)         { VariantMarshal::WriteVariantVector(*m_pReturn, v.v); },
		[&](const Vec2 &v)         { VariantMarshal::WriteVariantVector2D(*m_pReturn, v.v); },
		[&](const Quat &v)         { VariantMarshal::WriteVariantQuaternion(*m_pReturn, v.v); },
		[&](HSCRIPT v)             { VariantMarshal::WriteVariantHScript(*m_pReturn, v); },
	}, *m_return);
}
