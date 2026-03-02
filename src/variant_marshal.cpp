#include "variant_marshal.h"

// Not exported by all SDK/mathlib builds; needed by CVariantBase::operator const Quaternion &()
const Quaternion quat_identity(0.0f, 0.0f, 0.0f, 1.0f);

namespace VariantMarshal
{

int SPFieldToEngine(SPFieldType spType)
{
	switch (spType)
	{
		case SPFieldType::Void:       return FIELD_VOID;
		case SPFieldType::Float:      return FIELD_FLOAT;
		case SPFieldType::Vector:     return FIELD_VECTOR;
		case SPFieldType::Int:        return FIELD_INTEGER;
		case SPFieldType::Bool:       return FIELD_BOOLEAN;
		case SPFieldType::String:     return FIELD_CSTRING;
		case SPFieldType::HScript:    return FIELD_HSCRIPT;
		case SPFieldType::Vector2D:   return FIELD_VECTOR2D;
		case SPFieldType::Quaternion: return FIELD_QUATERNION;
		case SPFieldType::Variant:    return FIELD_VARIANT;
		default:                      return FIELD_VOID;
	}
}

SPFieldType EngineToSPField(int engineType)
{
	switch (engineType)
	{
		case FIELD_VOID:       return SPFieldType::Void;
		case FIELD_FLOAT:      return SPFieldType::Float;
		case FIELD_VECTOR:     return SPFieldType::Vector;
		case FIELD_INTEGER:    return SPFieldType::Int;
		case FIELD_BOOLEAN:    return SPFieldType::Bool;
		case FIELD_CSTRING:    return SPFieldType::String;
		case FIELD_HSCRIPT:    return SPFieldType::HScript;
		case FIELD_QANGLE:     return SPFieldType::Vector;
		case FIELD_VECTOR2D:   return SPFieldType::Vector2D;
		case FIELD_QUATERNION: return SPFieldType::Quaternion;
		case FIELD_VARIANT:    return SPFieldType::Variant;
		default:               return SPFieldType::Void;
	}
}

SPFieldType ReadVariantType(const ScriptVariant_t &variant)
{
	return EngineToSPField(variant.GetType());
}

int ReadVariantInt(const ScriptVariant_t &variant)
{
	return variant.Get<int>();
}

float ReadVariantFloat(const ScriptVariant_t &variant)
{
	return variant.Get<float>();
}

bool ReadVariantBool(const ScriptVariant_t &variant)
{
	return variant.Get<bool>();
}

HSCRIPT ReadVariantHScript(const ScriptVariant_t &variant)
{
	return variant.Get<HSCRIPT>();
}

int ReadVariantString(const ScriptVariant_t &variant, char *buffer, int maxlen)
{
	if (!variant.AssignTo(buffer, maxlen))
		buffer[0] = '\0';

	return strlen(buffer);
}

void ReadVariantVector(const ScriptVariant_t &variant, float vec[3])
{
	Vector v(0, 0, 0);
	variant.AssignTo(&v);
	vec[0] = v.x;
	vec[1] = v.y;
	vec[2] = v.z;
}

void ReadVariantVector2D(const ScriptVariant_t &variant, float vec[2])
{
	Vector2D v(0, 0);
	variant.AssignTo(&v);
	vec[0] = v.x;
	vec[1] = v.y;
}

void ReadVariantQuaternion(const ScriptVariant_t &variant, float quat[4])
{
	Quaternion q(0, 0, 0, 0);
	variant.AssignTo(&q);
	quat[0] = q.x;
	quat[1] = q.y;
	quat[2] = q.z;
	quat[3] = q.w;
}

void WriteVariantInt(ScriptVariant_t &variant, int value)
{
	variant = value;
}

void WriteVariantFloat(ScriptVariant_t &variant, float value)
{
	variant = value;
}

void WriteVariantBool(ScriptVariant_t &variant, bool value)
{
	variant = value;
}

void WriteVariantString(ScriptVariant_t &variant, const char *value, bool bCopy)
{
	variant = ScriptVariant_t(value, bCopy);
}

void WriteVariantVector(ScriptVariant_t &variant, const float vec[3])
{
	Vector v(vec[0], vec[1], vec[2]);
	variant = v;
	variant.ConvertToCopiedData();
}

void WriteVariantVector2D(ScriptVariant_t &variant, const float vec[2])
{
	Vector2D v(vec[0], vec[1]);
	variant = v;
	variant.ConvertToCopiedData();
}

void WriteVariantQuaternion(ScriptVariant_t &variant, const float quat[4])
{
	Quaternion q(quat[0], quat[1], quat[2], quat[3]);
	variant = q;
	variant.ConvertToCopiedData();
}

void WriteVariantHScript(ScriptVariant_t &variant, HSCRIPT value)
{
	variant = value;
}

void WriteVariantNull(ScriptVariant_t &variant)
{
	variant.Free();
}

HSCRIPT LookupFunction(IScriptVM *pVM, const char *name, HSCRIPT hScope)
{
	// GetValue instead of LookupFunction to also find native closures (engine-registered C++ functions)
	ScriptVariant_t val;
	if (pVM->GetValue(hScope, name, &val))
	{
		if (val.GetType() == FIELD_HSCRIPT)
			return ReadVariantHScript(val);
		else
			pVM->ReleaseValue(val);
	}
	return nullptr;
}

} // namespace VariantMarshal
