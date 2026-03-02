#ifndef _INCLUDE_VSCRIPT_SCRIPT_CONTEXT_H_
#define _INCLUDE_VSCRIPT_SCRIPT_CONTEXT_H_

#include "extension.h"
#include "variant_marshal.h"
#include <vector>
#include <string>
#include <variant>
#include <optional>

struct ScriptCallContext
{
	// Setup state (immutable after construction)
	std::string functionName;
	SPFieldType returnType;
	std::vector<SPFieldType> paramTypes;

	// Cached function lookup
	HSCRIPT hCachedFunction = nullptr;
	HSCRIPT hCachedScope = nullptr;
	uint32_t cachedGeneration = 0;

	// Execution state
	IScriptVM *pExecutedVM = nullptr;
	ScriptVariant_t returnValue;
	bool hasExecuted = false;

	ScriptCallContext(const char *name, SPFieldType retType, std::vector<SPFieldType> params)
		: functionName(name)
		, returnType(retType)
		, paramTypes(std::move(params))
	{}

	~ScriptCallContext();

	HSCRIPT ResolveFunction(IScriptVM *pVM, HSCRIPT hScope);
	ScriptStatus_t Execute(IScriptVM *pVM, ScriptVariant_t *pArgs, int nArgs, HSCRIPT hScope);
};

struct RegisteredFunction;

// The Squirrel VM does NOT set the type field in binding argument variants,
// so we use the registered parameter types to interpret the raw union data.
class ScriptContext
{
public:
	ScriptContext(ScriptVariant_t *pArgs, int nArgs, ScriptVariant_t *pReturn, RegisteredFunction *pReg, void *pCallerContext = nullptr);
	int GetArgCount() const { return m_nArgs; }

	int GetCallerEntity() const;

	SPFieldType GetArgType(int index) const;
	int GetArgInt(int index) const;
	float GetArgFloat(int index) const;
	bool GetArgBool(int index) const;
	int GetArgString(int index, char *buffer, int maxlen) const;
	void GetArgVector(int index, float vec[3]) const;
	void GetArgVector2D(int index, float vec[2]) const;
	void GetArgQuaternion(int index, float quat[4]) const;
	HSCRIPT GetArgHScript(int index) const;

	void SetReturnInt(int value);
	void SetReturnFloat(float value);
	void SetReturnBool(bool value);
	void SetReturnString(const char *value);
	void SetReturnVector(const float vec[3]);
	void SetReturnVector2D(const float vec[2]);
	void SetReturnQuaternion(const float quat[4]);
	void SetReturnHScript(HSCRIPT value);
	void SetReturnNull();

	void RaiseException(const char *text);
	bool HasException() const { return m_bHasException; }

	void FlushReturn();

	Handle_t CreateTrackedHScriptHandle(HSCRIPT h);
	void FreeTrackedHandles();

private:
	ScriptVariant_t *m_pArgs;
	int m_nArgs;
	ScriptVariant_t *m_pReturn;
	RegisteredFunction *m_pReg;
	void *m_pCallerContext;

	struct Vec3  { float v[3]; };
	struct Vec2  { float v[2]; };
	struct Quat  { float v[4]; };

	using ReturnValue = std::variant<std::monostate, int, float, bool, std::string, Vec3, Vec2, Quat, HSCRIPT>;
	std::optional<ReturnValue> m_return;
	bool m_bHasException = false;

	std::vector<Handle_t> m_trackedHandles;
};

#endif // _INCLUDE_VSCRIPT_SCRIPT_CONTEXT_H_
