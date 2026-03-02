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

#endif // _INCLUDE_VSCRIPT_NATIVES_H_
