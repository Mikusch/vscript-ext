#ifndef _INCLUDE_VSCRIPT_VARIANT_MARSHAL_H_
#define _INCLUDE_VSCRIPT_VARIANT_MARSHAL_H_

#include "extension.h"

// Mirrors the SourcePawn-side ScriptFieldType enum
enum class SPFieldType : int
{
	Void = 0,
	Float,
	Vector,
	Int,
	Bool,
	String,
	HScript,
	Vector2D,
	Quaternion,
	Variant,
};

namespace VariantMarshal
{
	int SPFieldToEngine(SPFieldType spType);
	SPFieldType EngineToSPField(int engineType);

	SPFieldType ReadVariantType(const ScriptVariant_t &variant);
	int ReadVariantInt(const ScriptVariant_t &variant);
	float ReadVariantFloat(const ScriptVariant_t &variant);
	bool ReadVariantBool(const ScriptVariant_t &variant);
	HSCRIPT ReadVariantHScript(const ScriptVariant_t &variant);
	int ReadVariantString(const ScriptVariant_t &variant, char *buffer, int maxlen);
	void ReadVariantVector(const ScriptVariant_t &variant, float vec[3]);
	void ReadVariantVector2D(const ScriptVariant_t &variant, float vec[2]);
	void ReadVariantQuaternion(const ScriptVariant_t &variant, float quat[4]);

	void WriteVariantInt(ScriptVariant_t &variant, int value);
	void WriteVariantFloat(ScriptVariant_t &variant, float value);
	void WriteVariantBool(ScriptVariant_t &variant, bool value);
	void WriteVariantString(ScriptVariant_t &variant, const char *value, bool bCopy = false);
	void WriteVariantVector(ScriptVariant_t &variant, const float vec[3]);
	void WriteVariantVector2D(ScriptVariant_t &variant, const float vec[2]);
	void WriteVariantQuaternion(ScriptVariant_t &variant, const float quat[4]);
	void WriteVariantHScript(ScriptVariant_t &variant, HSCRIPT value);
	void WriteVariantNull(ScriptVariant_t &variant);

	HSCRIPT LookupFunction(IScriptVM *pVM, const char *name, HSCRIPT hScope);
}

#endif // _INCLUDE_VSCRIPT_VARIANT_MARSHAL_H_
