#ifndef _INCLUDE_VSCRIPT_EXTENSION_H_
#define _INCLUDE_VSCRIPT_EXTENSION_H_

#include "smsdk_ext.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <vscript/ivscript.h>
#include <extensions/ISDKHooks.h>

enum class HScriptType
{
	Scope,    // Released via ReleaseScope
	Function, // Released via ReleaseFunction
	Script,   // Released via ReleaseScript
	Value,    // Released via RemoveInstance (C++ class instances only)
	Table,    // Released via ReleaseFunction (tables and other non-instance HSCRIPTs)
};

enum class HScriptOwnership
{
	Owned,    // Extension releases HSCRIPT when the handle is freed
	Borrowed, // Engine manages the HSCRIPT lifetime
};

extern uint32_t g_vmGeneration;

inline bool IsCurrentGeneration(uint32_t generation) { return generation == g_vmGeneration; }
IScriptVM *GetVMForGeneration(uint32_t generation);
void ReleaseVMValue(ScriptVariant_t &value, uint32_t generation);

struct HScriptHandle
{
	HSCRIPT hScript;
	HScriptType type;
	HScriptOwnership ownership;

	uint32_t vmGeneration;

	HScriptHandle(HSCRIPT h, HScriptType t, HScriptOwnership o)
		: hScript(h), type(t), ownership(o), vmGeneration(g_vmGeneration) {}
};

class CVScriptExtension
	: public SDKExtension
	, public IHandleTypeDispatch
	, public IPluginsListener
	, public ISMEntityListener
{
public:
	virtual bool SDK_OnLoad(char *error, size_t maxlength, bool late) override;
	virtual void SDK_OnUnload() override;
	virtual void SDK_OnAllLoaded() override;
	virtual bool SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late) override;
	virtual bool SDK_OnMetamodUnload(char *error, size_t maxlen) override;
	virtual void OnHandleDestroy(HandleType_t type, void *object) override;
	virtual void OnPluginLoaded(IPlugin *plugin) override;
	virtual void OnPluginUnloaded(IPlugin *plugin) override;
	virtual void OnEntityCreated(CBaseEntity *pEntity, const char *classname) override;
	virtual void OnEntityDestroyed(CBaseEntity *pEntity) override;

	IScriptVM *GetVM() const { return m_pScriptVM; }
	bool IsKnownEntity(void *ptr) const { return m_knownEntities.count(static_cast<CBaseEntity *>(ptr)) > 0; }

	Handle_t GetOrCreateCachedEntityHandle(CBaseEntity *pEntity, HSCRIPT hScript, HScriptType type);

	HandleType_t GetHScriptHandleType() const { return m_htHScript; }
	HandleType_t GetScriptContextHandleType() const { return m_htScriptContext; }
	HandleType_t GetScriptCallHandleType() const { return m_htScriptCall; }
	HandleType_t GetScriptIteratorHandleType() const { return m_htScriptIterator; }

	IForward *GetOnScriptPrintForward() const { return m_pOnScriptPrint; }
	IForward *GetOnScriptErrorForward() const { return m_pOnScriptError; }

	void OnVMCreated(IScriptVM *pVM);
	void OnVMDestroyed(IScriptVM *pVM);
	void OnVMDestroyedPost();

private:
	IScriptManager *m_pScriptManager = nullptr;
	IScriptVM *m_pScriptVM = nullptr;

	HandleType_t m_htHScript = 0;
	HandleType_t m_htScriptContext = 0;
	HandleType_t m_htScriptCall = 0;
	HandleType_t m_htScriptIterator = 0;
	Handle_t m_hRootTable = BAD_HANDLE;

	IForward *m_pOnVMInit = nullptr;
	IForward *m_pOnVMShutdown = nullptr;
	IForward *m_pOnScriptPrint = nullptr;
	IForward *m_pOnScriptError = nullptr;

	ISDKHooks *m_pSDKHooks = nullptr;

	std::unordered_set<CBaseEntity *> m_knownEntities;
	std::unordered_map<CBaseEntity *, Handle_t> m_cachedEntityInstances;
	std::unordered_map<CBaseEntity *, Handle_t> m_cachedEntityScopes;

	void ClearEntityHandleCache();

	void SetCurrentVM(IScriptVM *pVM);

	void InstallManagerHooks();
	void RemoveManagerHooks();
	void InstallVMHooks(IScriptVM *pVM);
	void RemoveVMHooks();

	KHook::Return<IScriptVM *> Hook_CreateVM(IScriptManager *pManager, ScriptLanguage_t language);
	KHook::Return<void> Hook_DestroyVM(IScriptManager *pManager, IScriptVM *pVM);
	KHook::Return<bool> Hook_RegisterClass(IScriptVM *pVM, ScriptClassDesc_t *pClassDesc);
	KHook::Return<void> Hook_SetErrorCallback(IScriptVM *pVM, ScriptErrorFunc_t pFunc);
};

void ReleaseOwnedHScript(IScriptVM *pVM, HSCRIPT hScript, HScriptType type);

extern CGlobalVars *gpGlobals;
extern CVScriptExtension g_VScriptExt;
extern const sp_nativeinfo_t g_VScriptNatives[];

#endif // _INCLUDE_VSCRIPT_EXTENSION_H_
