#ifndef _INCLUDE_VSCRIPT_CALLBACK_MANAGER_H_
#define _INCLUDE_VSCRIPT_CALLBACK_MANAGER_H_

#include "extension.h"
#include "variant_marshal.h"
#include <vector>
#include <string>
#include <unordered_map>

struct RegisteredFunction
{
	std::string scriptName;
	std::string description;

	IPluginFunction *pCallback;

	std::vector<SPFieldType> paramTypes;
	SPFieldType returnType;

	ScriptFunctionBinding_t *pBinding;

	std::string className;
	bool isMemberFunction;
	bool isRegistered;
};

class CallbackManager
{
public:
	void Shutdown();

	void OnVMInitialized(IScriptVM *pVM);
	void OnVMShutdown();
	void CleanupStaleRegistrations();

	RegisteredFunction *RegisterFunction(
		const char *name,
		const char *description,
		IPluginFunction *pCallback,
		SPFieldType returnType,
		const std::vector<SPFieldType> &paramTypes);

	RegisteredFunction *RegisterClassFunction(
		const char *className,
		const char *name,
		const char *description,
		IPluginFunction *pCallback,
		SPFieldType returnType,
		const std::vector<SPFieldType> &paramTypes);

	bool UnregisterFunction(const char *name);
	bool UnregisterClassFunction(const char *className, const char *name);
	void UnregisterAllForContext(IPluginContext *pContext);

	void OnRegisterClass(ScriptClassDesc_t *pClassDesc);

	static bool Dispatch(
		ScriptFunctionBindingStorageType_t pFunction,
		void *pContext,
		ScriptVariant_t *pArguments,
		int nArguments,
		ScriptVariant_t *pReturn);

private:
	void RegisterGlobalWithVM(RegisteredFunction *pReg, IScriptVM *pVM);
	void RegisterClassFunctionWithVM(RegisteredFunction *pReg, IScriptVM *pVM);
	static void RegisterLateDocumentation(const RegisteredFunction *pReg, HSCRIPT hRegisteredFunc, IScriptVM *pVM);
	void InjectIntoDescriptor(RegisteredFunction *pReg, ScriptClassDesc_t *pClassDesc);
	static void PopulateBinding(ScriptFunctionBinding_t *pBinding, const RegisteredFunction *pReg);

	std::unordered_map<std::string, RegisteredFunction *> m_globalRegistrations;
	std::unordered_map<std::string, std::vector<RegisteredFunction *>> m_classRegistrations;
	std::unordered_map<std::string, ScriptClassDesc_t *> m_knownClasses;
};

extern CallbackManager g_CallbackManager;

#endif // _INCLUDE_VSCRIPT_CALLBACK_MANAGER_H_
