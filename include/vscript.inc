#if defined _vscript_included
	#endinput
#endif
#define _vscript_included

// ============================================================================
// Enums
// ============================================================================

/**
 * Status codes returned by VScript execution functions.
 */
enum ScriptStatus
{
	ScriptStatus_Error = -1,       /**< Script execution encountered an error */
	ScriptStatus_Done = 0,         /**< Script execution completed successfully */
	ScriptStatus_Running = 1,      /**< Script is still running (coroutine yielded) */
};

/**
 * Supported data types for VScript values.
 */
enum ScriptFieldType
{
	ScriptField_Void = 0,         /**< No type / void */
	ScriptField_Float,            /**< Floating point value */
	ScriptField_Vector,           /**< 3D vector (float[3]) */
	ScriptField_Int,              /**< Integer value */
	ScriptField_Bool,             /**< Boolean value */
	ScriptField_String,           /**< String value */
	ScriptField_HScript,          /**< Script handle (HSCRIPT) */
	ScriptField_Vector2D,         /**< 2D vector (float[2]) */
	ScriptField_Quaternion,       /**< Quaternion (float[4]) */
	ScriptField_Variant,          /**< Any type (use GetArgType to determine) */
};

/**
 * Error levels for VScript error callbacks.
 */
enum ScriptErrorLevel
{
	ScriptLevel_Warning = 0,      /**< Non-fatal warning */
	ScriptLevel_Error = 1,        /**< Fatal error */
};

// ============================================================================
// Handle types
// ============================================================================

/**
 * Handle wrapping an HSCRIPT representing a scope, table, function, or class instance.
 * All ScriptHandles must be closed when no longer needed, except entity handles returned by
 * VScript_EntityToHScript and VScript_GetEntityScriptScope which are managed by the extension.
 * All ScriptHandles become invalid after VM shutdown (map change).
 */
methodmap ScriptHandle < Handle {};

// ============================================================================
// Callback types
// ============================================================================

/**
 * Callback signature for SourcePawn functions registered into VScript.
 *
 * @param context   Provides access to function arguments, return value, caller entity, and exception handling.
 *
 * @note            The ScriptContext handle is temporary and only valid during the callback.
 */
typedef ScriptCallback = function void (ScriptContext context);

// ============================================================================
// Handles
// ============================================================================

/**
 * Context for callbacks from VScript to SourcePawn.
 */
methodmap ScriptContext < Handle
{
	/**
	 * Number of arguments passed to this function.
	 */
	property int ArgCount {
		public native get();
	}

	/**
	 * For class member function callbacks: the entity index of the caller.
	 *
	 * @return          The entity index, or -1 if this is not a member function call or not an entity class.
	 */
	property int Entity {
		public native get();
	}

	/**
	 * Gets the field type of the argument at the given index.
	 *
	 * @param arg       Argument index (0-based).
	 * @return          The field type.
	 */
	public native ScriptFieldType GetArgType(int arg);

	/**
	 * Gets an integer argument.
	 *
	 * @param arg       Argument index (0-based).
	 * @return          The integer value.
	 */
	public native int GetArgInt(int arg);

	/**
	 * Gets a float argument.
	 *
	 * @param arg       Argument index (0-based).
	 * @return          The float value.
	 */
	public native float GetArgFloat(int arg);

	/**
	 * Gets a boolean argument.
	 *
	 * @param arg       Argument index (0-based).
	 * @return          The boolean value.
	 */
	public native bool GetArgBool(int arg);

	/**
	 * Gets a string argument.
	 *
	 * @param arg       Argument index (0-based).
	 * @param buffer    Buffer to store the string.
	 * @param maxlen    Maximum length of the buffer.
	 * @return          Number of bytes written.
	 */
	public native int GetArgString(int arg, char[] buffer, int maxlen);

	/**
	 * Gets a vector argument.
	 *
	 * @param arg       Argument index (0-based).
	 * @param vec       Array to store the vector.
	 */
	public native void GetArgVector(int arg, float vec[3]);

	/**
	 * Gets a Vector2D argument.
	 *
	 * @param arg       Argument index (0-based).
	 * @param vec       Array to store the Vector2D.
	 */
	public native void GetArgVector2D(int arg, float vec[2]);

	/**
	 * Gets a Quaternion argument.
	 *
	 * @param arg       Argument index (0-based).
	 * @param quat      Array to store the Quaternion.
	 */
	public native void GetArgQuaternion(int arg, float quat[4]);

	/**
	 * Gets an HSCRIPT argument as a handle.
	 *
	 * @param arg       Argument index (0-based).
	 * @return          A ScriptHandle.
	 *
	 * @note            The returned handle is managed by the extension and freed automatically when the callback returns.
	 *                  Do not close it.
	 */
	public native ScriptHandle GetArgHScript(int arg);

	/**
	 * Sets the return value to an integer.
	 *
	 * @param value     The integer to return.
	 */
	public native void SetReturnInt(int value);

	/**
	 * Sets the return value to a float.
	 *
	 * @param value     The float to return.
	 */
	public native void SetReturnFloat(float value);

	/**
	 * Sets the return value to a boolean.
	 *
	 * @param value     The boolean to return.
	 */
	public native void SetReturnBool(bool value);

	/**
	 * Sets the return value to a string.
	 *
	 * @param value     The string to return.
	 */
	public native void SetReturnString(const char[] value);

	/**
	 * Sets the return value to a vector.
	 *
	 * @param vec       The vector to return.
	 */
	public native void SetReturnVector(const float vec[3]);

	/**
	 * Sets the return value to a Vector2D.
	 *
	 * @param vec       The Vector2D to return.
	 */
	public native void SetReturnVector2D(const float vec[2]);

	/**
	 * Sets the return value to a Quaternion.
	 *
	 * @param quat      The Quaternion to return.
	 */
	public native void SetReturnQuaternion(const float quat[4]);

	/**
	 * Sets the return value to an HSCRIPT.
	 *
	 * @param handle    The ScriptHandle to return.
	 */
	public native void SetReturnHScript(ScriptHandle handle);

	/**
	 * Sets the return value to null/void.
	 */
	public native void SetReturnNull();

	/**
	 * Raises an exception in the VScript VM.
	 * The return value is suppressed and the exception is propagated to VScript (catchable via try/catch).
	 *
	 * @param text      The exception message.
	 */
	public native void RaiseException(const char[] text);
};

/**
 * Reusable setup handle for calling VScript functions from SourcePawn.
 * Similar to SDKCalls: declare the function name and parameter types once,
 * then call Execute() with arguments as many times as needed.
 *
 * The function is looked up on first execution and cached internally.
 * The cache is invalidated on VM restart and re-resolved on the next call.
 */
methodmap ScriptCall < Handle
{
	/**
	 * Creates a reusable call handle for a VScript function.
	 *
	 * @param name          Function name to call.
	 * @param returnType    The function's return type (default void).
	 * @param ...           Variadic list of ScriptFieldType for each parameter.
	 *
	 * @note                The function is not looked up until Execute/ExecuteInScope is called.
	 */
	public native ScriptCall(const char[] name, ScriptFieldType returnType = ScriptField_Void, ScriptFieldType ...);

	/**
	 * Executes the function in the root scope.
	 *
	 * @param ...       Variadic arguments matching the declared parameter types.
	 *                  Additional arguments beyond the declared types can be passed
	 *                  as alternating ScriptFieldType and value arguments.
	 * @return          ScriptStatus_Done on success, ScriptStatus_Error on failure, or ScriptStatus_Running if the function yielded.
	 * @error           Too few arguments, odd number of extra arguments, or function not found.
	 */
	public native ScriptStatus Execute(any ...);

	/**
	 * Executes the function in a specific scope.
	 *
	 * @param scope     Scope to look up and execute the function in (e.g. entity script scope).
	 * @param ...       Variadic arguments matching the declared parameter types.
	 *                  Additional arguments beyond the declared types can be passed
	 *                  as alternating ScriptFieldType and value arguments.
	 * @return          ScriptStatus_Done on success, ScriptStatus_Error on failure, or ScriptStatus_Running if the function yielded.
	 * @error           Too few arguments, odd number of extra arguments, or function not found.
	 */
	public native ScriptStatus ExecuteInScope(ScriptHandle scope, any ...);

	/**
	 * The field type of the return value.
	 *
	 * @error           ScriptCall has not been executed yet.
	 */
	property ScriptFieldType ReturnType {
		public native get();
	}

	/**
	 * Gets the return value as an integer.
	 *
	 * @return          The integer return value.
	 * @error           ScriptCall has not been executed yet.
	 */
	public native int GetReturnInt();

	/**
	 * Gets the return value as a float.
	 *
	 * @return          The float return value.
	 * @error           ScriptCall has not been executed yet.
	 */
	public native float GetReturnFloat();

	/**
	 * Gets the return value as a boolean.
	 *
	 * @return          The boolean return value.
	 * @error           ScriptCall has not been executed yet.
	 */
	public native bool GetReturnBool();

	/**
	 * Gets the return value as a string.
	 *
	 * @param buffer    Buffer to store the string.
	 * @param maxlen    Maximum length of the buffer.
	 * @return          Number of bytes written.
	 * @error           ScriptCall has not been executed yet.
	 */
	public native int GetReturnString(char[] buffer, int maxlen);

	/**
	 * Gets the return value as a vector.
	 *
	 * @param vec       Array to store the vector.
	 * @error           ScriptCall has not been executed yet.
	 */
	public native void GetReturnVector(float vec[3]);

	/**
	 * Gets the return value as a Vector2D.
	 *
	 * @param vec       Array to store the Vector2D.
	 * @error           ScriptCall has not been executed yet.
	 */
	public native void GetReturnVector2D(float vec[2]);

	/**
	 * Gets the return value as a Quaternion.
	 *
	 * @param quat      Array to store the Quaternion.
	 * @error           ScriptCall has not been executed yet.
	 */
	public native void GetReturnQuaternion(float quat[4]);

	/**
	 * Gets the return value as an HSCRIPT handle.
	 *
	 * @return          A ScriptHandle, or null if the return value is null.
	 * @error           ScriptCall has not been executed yet.
	 *
	 * @note            The returned handle must be closed when no longer needed.
	 */
	public native ScriptHandle GetReturnHScript();

	/**
	 * Checks if the return value is null.
	 * Use this to distinguish null returns from zero/empty values.
	 *
	 * @return          True if the return value is null.
	 * @error           ScriptCall has not been executed yet.
	 */
	public native bool IsReturnNull();
};

// ============================================================================
// Forwards
// ============================================================================

/**
 * Called when the VScript VM has been initialized.
 * Register custom functions here via VScript_RegisterFunction.
 *
 * @note            On late load, this is called automatically if the VM is already running.
 */
forward void VScript_OnVMInitialized();

/**
 * Called just before the VScript VM is destroyed.
 * Use this to clean up any VScript-related state.
 *
 * @note            Any ScriptHandle objects obtained from the VM become invalid after this.
 */
forward void VScript_OnVMShutdown();

/**
 * Called when VScript produces output (e.g. print() calls).
 *
 * @param text      The output text.
 */
forward void VScript_OnScriptPrint(const char[] text);

/**
 * Called when VScript produces an error or warning.
 *
 * @param level     The error severity level.
 * @param text      The error message text.
 */
forward void VScript_OnScriptError(ScriptErrorLevel level, const char[] text);

// ============================================================================
// VM Lifecycle
// ============================================================================

/**
 * Returns whether the VScript VM is currently initialized.
 *
 * @return          True if the VM is available.
 */
native bool VScript_IsVMInitialized();

/**
 * Executes a string of VScript code directly.
 *
 * @param code      The VScript code to run.
 * @return          ScriptStatus_Done on success, ScriptStatus_Error on failure, or ScriptStatus_Running if the script yielded.
 */
native ScriptStatus VScript_Run(const char[] code);

// ============================================================================
// Compilation
// ============================================================================

/**
 * Compiles a VScript string into a reusable script handle.
 *
 * @param code      The VScript code to compile.
 * @param id        Optional identifier for the script (shown in errors).
 * @return          A ScriptHandle to the compiled script, or null on failure.
 *
 * @note            The returned handle must be closed when no longer needed.
 */
native ScriptHandle VScript_CompileScript(const char[] code, const char[] id = "");

/**
 * Runs a previously compiled script.
 *
 * @param script    Compiled script handle from VScript_CompileScript.
 * @param scope     Scope to run in (null for root table).
 * @return          ScriptStatus_Done on success, ScriptStatus_Error on failure, or ScriptStatus_Running if the script yielded.
 */
native ScriptStatus VScript_RunScript(ScriptHandle script, ScriptHandle scope = null);

// ============================================================================
// Scopes and Tables
// ============================================================================

/**
 * Creates a new script scope.
 *
 * @param name      Name of the scope.
 * @param parent    Parent scope (null for root-level scope).
 * @return          A ScriptHandle to the new scope, or null on failure.
 *
 * @note            The returned handle must be closed when no longer needed.
 */
native ScriptHandle VScript_CreateScope(const char[] name, ScriptHandle parent = null);

/**
 * Creates a new empty script table.
 *
 * @return          A ScriptHandle to the new table, or null on failure.
 *
 * @note            The returned handle must be closed when no longer needed.
 */
native ScriptHandle VScript_CreateTable();

/**
 * Returns the number of entries in a table or scope.
 *
 * @param scope     The table or scope handle.
 * @return          Number of entries.
 */
native int VScript_GetNumTableEntries(ScriptHandle scope);

/**
 * Iterates over keys in a table or scope.
 * Pass 0 as iterator to begin. Returns the next iterator, or -1 when done.
 * To read the actual value, use the key name with the appropriate VScript_GetValue* native.
 *
 * @param scope     The table or scope handle.
 * @param iterator  Current iterator (0 to start).
 * @param keyBuffer Buffer to store the key name.
 * @param keyMaxLen Maximum length of the key buffer.
 * @param fieldType Output for the value's field type.
 * @return          Next iterator value, or -1 when iteration is complete.
 */
native int VScript_GetNextKey(ScriptHandle scope, int iterator, char[] keyBuffer, int keyMaxLen, ScriptFieldType &fieldType);

// ============================================================================
// Value Operations
// ============================================================================

/**
 * Checks if a key exists in the given scope.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @return          True if the key exists.
 */
native bool VScript_ValueExists(ScriptHandle scope, const char[] key);

/**
 * Clears (removes) a key from the given scope.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @return          True on success, false if the key does not exist.
 */
native bool VScript_ClearValue(ScriptHandle scope, const char[] key);

/**
 * Gets the field type of a value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @return          The field type, or ScriptField_Void if the key does not exist.
 */
native ScriptFieldType VScript_GetValueType(ScriptHandle scope, const char[] key);

/**
 * Gets an integer value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @return          The integer value.
 */
native int VScript_GetValueInt(ScriptHandle scope, const char[] key);

/**
 * Gets a boolean value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @return          The boolean value.
 */
native bool VScript_GetValueBool(ScriptHandle scope, const char[] key);

/**
 * Gets a float value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @return          The float value.
 */
native float VScript_GetValueFloat(ScriptHandle scope, const char[] key);

/**
 * Gets a string value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @param buffer    Buffer to store the string.
 * @param maxlen    Maximum length of the buffer.
 * @return          Number of bytes written.
 */
native int VScript_GetValueString(ScriptHandle scope, const char[] key, char[] buffer, int maxlen);

/**
 * Gets a vector value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @param vec       Array to store the vector.
 */
native void VScript_GetValueVector(ScriptHandle scope, const char[] key, float vec[3]);

/**
 * Gets a Vector2D value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @param vec       Array to store the Vector2D.
 */
native void VScript_GetValueVector2D(ScriptHandle scope, const char[] key, float vec[2]);

/**
 * Gets a Quaternion value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @param quat      Array to store the Quaternion.
 */
native void VScript_GetValueQuaternion(ScriptHandle scope, const char[] key, float quat[4]);

/**
 * Gets an HSCRIPT value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @return          A ScriptHandle, or null if not found.
 *
 * @note            The returned handle must be closed when no longer needed.
 */
native ScriptHandle VScript_GetValueHScript(ScriptHandle scope, const char[] key);

/**
 * Sets an integer value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @param value     The integer value to set.
 * @return          True on success, false otherwise.
 */
native bool VScript_SetValueInt(ScriptHandle scope, const char[] key, int value);

/**
 * Sets a float value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @param value     The float value to set.
 * @return          True on success, false otherwise.
 */
native bool VScript_SetValueFloat(ScriptHandle scope, const char[] key, float value);

/**
 * Sets a bool value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @param value     The bool value to set.
 * @return          True on success, false otherwise.
 */
native bool VScript_SetValueBool(ScriptHandle scope, const char[] key, bool value);

/**
 * Sets a string value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @param value     The string value to set.
 * @return          True on success, false otherwise.
 */
native bool VScript_SetValueString(ScriptHandle scope, const char[] key, const char[] value);

/**
 * Sets a vector value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @param vec       The vector value to set.
 * @return          True on success, false otherwise.
 */
native bool VScript_SetValueVector(ScriptHandle scope, const char[] key, const float vec[3]);

/**
 * Sets a Vector2D value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @param vec       The Vector2D value to set.
 * @return          True on success, false otherwise.
 */
native bool VScript_SetValueVector2D(ScriptHandle scope, const char[] key, const float vec[2]);

/**
 * Sets a Quaternion value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @param quat      The Quaternion value to set.
 * @return          True on success, false otherwise.
 */
native bool VScript_SetValueQuaternion(ScriptHandle scope, const char[] key, const float quat[4]);

/**
 * Sets an HSCRIPT value at the given key.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @param value     The ScriptHandle value to set.
 * @return          True on success, false otherwise.
 */
native bool VScript_SetValueHScript(ScriptHandle scope, const char[] key, ScriptHandle value);

/**
 * Sets the value at the given key to null.
 *
 * @param scope     The scope/table handle (null for root table).
 * @param key       The key name.
 * @return          True on success, false otherwise.
 */
native bool VScript_SetValueNull(ScriptHandle scope, const char[] key);

// ============================================================================
// Functions
// ============================================================================

/**
 * Looks up a named function in the given scope (or root table if null).
 *
 * @param name      Function name to look up.
 * @param scope     Scope to search in (null for root table).
 * @return          A ScriptHandle to the function, or null if not found.
 *
 * @note            The returned handle must be closed when no longer needed.
 */
native ScriptHandle VScript_LookupFunction(const char[] name, ScriptHandle scope = null);

/**
 * Registers a new VScript function that calls back to SourcePawn when invoked.
 * The function is automatically re-registered on map change.
 *
 * @param name          Function name visible in VScript.
 * @param callback      The SourcePawn function to call.
 * @param description   Optional description for the function.
 * @param returnType    The return type of the function.
 * @param ...           Variadic list of ScriptFieldType for parameters.
 * @return              True on success, false otherwise.
 * @error               Invalid callback function, invalid return type, or invalid parameter type.
 *
 * @note                The function is automatically removed from the root table when the owning plugin is unloaded.
 */
native bool VScript_RegisterFunction(const char[] name, ScriptCallback callback, const char[] description = "", ScriptFieldType returnType = ScriptField_Void, ScriptFieldType ...);

/**
 * Registers a function on a VScript class.
 * All instances of that class and its subclasses will gain this method.
 * The function is automatically re-registered on map change.
 * Inside the callback, use context.Entity to get the entity the method was called on.
 *
 * @param className     The VScript class name (e.g. "CTFPlayer").
 * @param name          Function name visible in VScript.
 * @param callback      The SourcePawn function to call.
 * @param description   Optional description for the function.
 * @param returnType    The return type of the function.
 * @param ...           Variadic list of ScriptFieldType for parameters.
 * @return              True on success, false otherwise.
 * @error               Invalid callback function, invalid return type, or invalid parameter type.
 */
native bool VScript_RegisterClassFunction(const char[] className, const char[] name, ScriptCallback callback, const char[] description = "", ScriptFieldType returnType = ScriptField_Void, ScriptFieldType ...);

/**
 * Unregisters a previously registered global VScript function.
 * The function is removed from the root table and will no longer be callable from VScript.
 *
 * @param name      The function name that was used during registration.
 * @return          True if the function was found and unregistered, false otherwise.
 */
native bool VScript_UnregisterFunction(const char[] name);

/**
 * Unregisters a previously registered class function.
 *
 * @param className The VScript class name the function was registered on.
 * @param name      The function name that was used during registration.
 * @return          True if the function was found and unregistered, false otherwise.
 *
 * @note            The method cannot be removed from the class. It will remain but become a silent no-op.
 */
native bool VScript_UnregisterClassFunction(const char[] className, const char[] name);

// ============================================================================
// Entities
// ============================================================================

/**
 * Gets the VScript scope handle for an entity.
 *
 * @param entity    Entity index.
 * @param create    If true, creates the script instance and scope if they don't already exist.
 *                  If false, returns null when uninitialized.
 * @return          A ScriptHandle, or null if the entity has no scope.
 * @error           Invalid entity index.
 *
 * @note            The returned handle is managed by the extension and invalidated automatically
 *                  when the entity is destroyed or the VM shuts down. Do not close it.
 */
native ScriptHandle VScript_GetEntityScriptScope(int entity, bool create = false);

/**
 * Gets the HSCRIPT instance handle for an entity.
 *
 * @param entity    Entity index.
 * @param create    If true, creates the script instance if it doesn't already exist.
 *                  If false, returns null when uninitialized.
 * @return          A ScriptHandle, or null if the entity has no VScript instance.
 * @error           Invalid entity index.
 *
 * @note            The returned handle is managed by the extension and invalidated automatically
 *                  when the entity is destroyed or the VM shuts down. Do not close it.
 */
native ScriptHandle VScript_EntityToHScript(int entity, bool create = false);

/**
 * Converts an HSCRIPT instance back to an entity index.
 *
 * @param handle    The HSCRIPT handle to convert.
 * @return          Entity index, or -1 if invalid.
 */
native int VScript_HScriptToEntity(ScriptHandle handle);

// ============================================================================
// Utility
// ============================================================================

/**
 * Generates a unique key string based on a root name.
 *
 * @param root      Root string for the key.
 * @param buffer    Buffer to store the generated key.
 * @param maxlen    Maximum length of the buffer.
 * @return          True on success, false otherwise.
 */
native bool VScript_GenerateUniqueKey(const char[] root, char[] buffer, int maxlen);

/**
 * Adds a search path for VScript file loading.
 *
 * @param path      The search path to add.
 */
native void VScript_AddSearchPath(const char[] path);

// ============================================================================
// Extension
// ============================================================================

public Extension __ext_vscript =
{
	name = "vscript",
	file = "vscript.ext",
	autoload = 1,
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};

#if !defined REQUIRE_EXTENSIONS
public void __ext_vscript_SetNTVOptional()
{
	MarkNativeAsOptional("VScript_IsVMInitialized");
	MarkNativeAsOptional("VScript_Run");
	MarkNativeAsOptional("VScript_CompileScript");
	MarkNativeAsOptional("VScript_RunScript");
	MarkNativeAsOptional("VScript_CreateScope");
	MarkNativeAsOptional("VScript_CreateTable");
	MarkNativeAsOptional("VScript_GetNumTableEntries");
	MarkNativeAsOptional("VScript_GetNextKey");
	MarkNativeAsOptional("VScript_ValueExists");
	MarkNativeAsOptional("VScript_ClearValue");
	MarkNativeAsOptional("VScript_GetValueType");
	MarkNativeAsOptional("VScript_GetValueInt");
	MarkNativeAsOptional("VScript_GetValueBool");
	MarkNativeAsOptional("VScript_GetValueFloat");
	MarkNativeAsOptional("VScript_GetValueString");
	MarkNativeAsOptional("VScript_GetValueVector");
	MarkNativeAsOptional("VScript_GetValueVector2D");
	MarkNativeAsOptional("VScript_GetValueQuaternion");
	MarkNativeAsOptional("VScript_GetValueHScript");
	MarkNativeAsOptional("VScript_SetValueInt");
	MarkNativeAsOptional("VScript_SetValueFloat");
	MarkNativeAsOptional("VScript_SetValueBool");
	MarkNativeAsOptional("VScript_SetValueString");
	MarkNativeAsOptional("VScript_SetValueVector");
	MarkNativeAsOptional("VScript_SetValueVector2D");
	MarkNativeAsOptional("VScript_SetValueQuaternion");
	MarkNativeAsOptional("VScript_SetValueHScript");
	MarkNativeAsOptional("VScript_SetValueNull");
	MarkNativeAsOptional("VScript_LookupFunction");
	MarkNativeAsOptional("VScript_RegisterFunction");
	MarkNativeAsOptional("VScript_RegisterClassFunction");
	MarkNativeAsOptional("VScript_UnregisterFunction");
	MarkNativeAsOptional("VScript_UnregisterClassFunction");
	MarkNativeAsOptional("VScript_GetEntityScriptScope");
	MarkNativeAsOptional("VScript_EntityToHScript");
	MarkNativeAsOptional("VScript_HScriptToEntity");
	MarkNativeAsOptional("VScript_GenerateUniqueKey");
	MarkNativeAsOptional("VScript_AddSearchPath");

	// ScriptCall methodmap
	MarkNativeAsOptional("ScriptCall.ScriptCall");
	MarkNativeAsOptional("ScriptCall.Execute");
	MarkNativeAsOptional("ScriptCall.ExecuteInScope");
	MarkNativeAsOptional("ScriptCall.ReturnType.get");
	MarkNativeAsOptional("ScriptCall.GetReturnInt");
	MarkNativeAsOptional("ScriptCall.GetReturnFloat");
	MarkNativeAsOptional("ScriptCall.GetReturnBool");
	MarkNativeAsOptional("ScriptCall.GetReturnString");
	MarkNativeAsOptional("ScriptCall.GetReturnVector");
	MarkNativeAsOptional("ScriptCall.GetReturnVector2D");
	MarkNativeAsOptional("ScriptCall.GetReturnQuaternion");
	MarkNativeAsOptional("ScriptCall.GetReturnHScript");
	MarkNativeAsOptional("ScriptCall.IsReturnNull");

	// ScriptContext methodmap
	MarkNativeAsOptional("ScriptContext.ArgCount.get");
	MarkNativeAsOptional("ScriptContext.Entity.get");
	MarkNativeAsOptional("ScriptContext.GetArgType");
	MarkNativeAsOptional("ScriptContext.GetArgInt");
	MarkNativeAsOptional("ScriptContext.GetArgFloat");
	MarkNativeAsOptional("ScriptContext.GetArgBool");
	MarkNativeAsOptional("ScriptContext.GetArgString");
	MarkNativeAsOptional("ScriptContext.GetArgVector");
	MarkNativeAsOptional("ScriptContext.GetArgVector2D");
	MarkNativeAsOptional("ScriptContext.GetArgQuaternion");
	MarkNativeAsOptional("ScriptContext.GetArgHScript");
	MarkNativeAsOptional("ScriptContext.SetReturnInt");
	MarkNativeAsOptional("ScriptContext.SetReturnFloat");
	MarkNativeAsOptional("ScriptContext.SetReturnBool");
	MarkNativeAsOptional("ScriptContext.SetReturnString");
	MarkNativeAsOptional("ScriptContext.SetReturnVector");
	MarkNativeAsOptional("ScriptContext.SetReturnVector2D");
	MarkNativeAsOptional("ScriptContext.SetReturnQuaternion");
	MarkNativeAsOptional("ScriptContext.SetReturnHScript");
	MarkNativeAsOptional("ScriptContext.SetReturnNull");
	MarkNativeAsOptional("ScriptContext.RaiseException");
}
#endif
