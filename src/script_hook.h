#ifndef _INCLUDE_VSCRIPT_SCRIPT_HOOK_H_
#define _INCLUDE_VSCRIPT_SCRIPT_HOOK_H_

#include "extension.h"
#include "variant_marshal.h"
#include "script_context.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

// Mirrors the SourcePawn-side ScriptHookMode enum
enum class ScriptHookMode : int
{
	Pre = 0,
	Post,
};

struct HookTarget;
struct ScriptHookSetup;

struct HookCallback
{
	IPluginFunction *pFunc;
	ScriptHookSetup *pSetup;
	ScriptHookMode mode;
	bool active = true;
};

// The VM reads m_pfnBinding/m_pFunction on every call, so redirecting them to Dispatch also hooks existing closures
struct HookTarget
{
	std::string className;
	std::string functionName;
	std::string displayName;

	std::vector<HookCallback *> callbacks[2]; // Indexed by ScriptHookMode
	int activeCount = 0;
	int dispatchDepth = 0;

	// Attachment state, only valid for the current VM
	bool attached = false;
	ScriptFunctionBinding_t *pGlobalBinding = nullptr;
	ScriptClassDesc_t *pClassDesc = nullptr;
	ScriptBindingFunc_t pfnOriginal = nullptr;
	ScriptFunctionBindingStorageType_t originalFunction = {};
	bool reported = false;

	std::vector<SPFieldType> paramTypes;
	std::vector<int> engineParamTypes;
	int engineReturnType = FIELD_VOID;
	bool isMemberFunction = false;

	bool IsGlobal() const { return className.empty(); }
};

struct ScriptHookSetup
{
	HookTarget *pTarget;
	std::vector<HookCallback *> callbacks;
};

class ScriptHookContext : public ScriptContext
{
public:
	ScriptHookContext(HookTarget *pTarget, void *pInstance, ScriptVariant_t *pArgs, int nArgs, ScriptVariant_t *pReturn);
	~ScriptHookContext() override;

	bool IsPost() const { return m_bPost; }
	int GetArgEngineType(int index) const;

	bool ValidateArg(int index, SPFieldType valueType, char *error, size_t maxlength) const;

	void SetArgInt(int index, int value);
	void SetArgFloat(int index, float value);
	void SetArgBool(int index, bool value);
	void SetArgString(int index, const char *value);
	void SetArgVector(int index, const float vec[3]);
	void SetArgVector2D(int index, const float vec[2]);
	void SetArgQuaternion(int index, const float quat[4]);
	void SetArgHScript(int index, HSCRIPT value, Handle_t keepAlive);
	void SetArgEHandle(int index, CBaseHandle value);
	void SetArgNull(int index);

	bool ArgsModified() const { return m_bArgsModified; }
	ScriptVariant_t *GetModifiedArgs() { return m_modifiedArgs.data(); }

	void BeginPost(bool argsApplied);

private:
	struct ArgStorage
	{
		std::string str;
		Vector vec;
		QAngle ang;
		Vector2D vec2;
		Quaternion quat;
		Handle_t keepAlive = BAD_HANDLE;
	};

	ScriptVariant_t &BeginArgWrite(int index);
	void ReleaseArgStorage();

	HookTarget *m_pTarget;
	ScriptVariant_t *m_pOriginalArgs;
	std::vector<ScriptVariant_t> m_modifiedArgs;
	std::vector<ArgStorage> m_argStorage;
	bool m_bArgsModified = false;
	bool m_bPost = false;
};

class ScriptHookManager
{
public:
	void Init();
	void Shutdown();

	void OnVMCreated();
	void OnVMShutdown();
	void OnRegisterFunction(ScriptFunctionBinding_t *pBinding);
	void OnRegisterClass(ScriptClassDesc_t *pClassDesc);

	ScriptHookSetup *CreateSetup(const std::string &className, const std::string &functionName);
	void DestroySetup(ScriptHookSetup *pSetup);

	bool Enable(ScriptHookSetup *pSetup, ScriptHookMode mode, IPluginFunction *pFunc);
	bool Disable(ScriptHookSetup *pSetup, ScriptHookMode mode, IPluginFunction *pFunc);
	void RemoveCallbacksForContext(IPluginContext *pContext);

	static bool Dispatch(
		ScriptFunctionBindingStorageType_t pFunction,
		void *pContext,
		ScriptVariant_t *pArguments,
		int nArguments,
		ScriptVariant_t *pReturn);

private:
	static void OnGameFrame(bool simulating);
	static cell_t RunCallbacks(HookTarget *pTarget, ScriptHookMode mode, ScriptHookContext &ctx);

	ScriptFunctionBinding_t *FindBinding(HookTarget *pTarget) const;
	void Attach(HookTarget *pTarget);
	void Detach(HookTarget *pTarget);
	void Deactivate(HookTarget *pTarget, HookCallback *pCallback);
	void Purge(HookTarget *pTarget);
	void ReportUnattached(HookTarget *pTarget);

	std::unordered_map<std::string, HookTarget *> m_targets;
	std::unordered_set<ScriptHookSetup *> m_setups;

	// Registered with the current VM
	std::unordered_map<std::string, ScriptFunctionBinding_t *> m_globalBindings;
	std::unordered_map<std::string, ScriptClassDesc_t *> m_classes;

	uint32_t m_pendingReadyGeneration = 0;
	bool m_bReadyCheckPending = false;
	bool m_bVMReady = false;
};

extern ScriptHookManager g_HookManager;

#endif // _INCLUDE_VSCRIPT_SCRIPT_HOOK_H_
