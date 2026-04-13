#ifndef _INCLUDE_VSCRIPT_NATIVES_H_
#define _INCLUDE_VSCRIPT_NATIVES_H_

#include "extension.h"

extern const sp_nativeinfo_t g_VScriptNatives[];

inline IScriptVM *GetVMOrThrow(IPluginContext *pContext)
{
	IScriptVM *pVM = g_VScriptExt.GetVM();
	if (!pVM)
	{
		pContext->ThrowNativeError("VScript VM is not initialized");
		return nullptr;
	}
	return pVM;
}

HScriptHandle *ReadHScriptHandle(IPluginContext *pContext, Handle_t hndl);
Handle_t CreateHScriptHandle(IPluginContext *pContext, HSCRIPT hScript, HScriptType type, HScriptOwnership ownership);

struct ScriptIteratorState
{
	HSCRIPT hScope;
	int nextIterator;
	ScriptVariant_t key;
	ScriptVariant_t value;
	bool hasValue;
	uint32_t vmGeneration;

	ScriptIteratorState(HSCRIPT scope, uint32_t gen)
		: hScope(scope), nextIterator(0), hasValue(false), vmGeneration(gen) {}

	~ScriptIteratorState() { ReleaseCurrentValues(); }

	void ReleaseCurrentValues()
	{
		if (hasValue && vmGeneration == g_vmGeneration)
		{
			IScriptVM *pVM = g_VScriptExt.GetVM();
			if (pVM)
			{
				pVM->ReleaseValue(key);
				pVM->ReleaseValue(value);
			}
		}
		hasValue = false;
	}
};

#endif // _INCLUDE_VSCRIPT_NATIVES_H_
