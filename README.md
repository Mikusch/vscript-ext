# VScript

A SourceMod extension that bridges **SourcePawn** and **VScript**, giving plugins full access to the scripting VM.

This extension is compiled against official [Source SDK 2013](https://github.com/ValveSoftware/source-sdk-2013) headers and requires no gamedata.

Supported Games:

* Team Fortress 2
* Counter-Strike: Source
* Half-Life 2 Deathmatch
* Day of Defeat: Source

## Examples

All examples assume you have included [`vscript.inc`](/include/vscript.inc).

### Running VScript Code

> [!IMPORTANT]
> `VScript_Run` requires the VM to be initialized.
> Use `VScript_OnVMInitialized` for code that should run at map start, and `VScript_IsVMInitialized` to guard runtime calls.

```sourcepawn
public void VScript_OnVMInitialized()
{
	VScript_Run("printl(\"Hello from SourcePawn!\")");
}
```

### Calling a VScript Function

Create a reusable call handle once, then call it as many times as needed.
The function is looked up lazily and re-resolved automatically after each map change.

Given a Squirrel function:

```squirrel
function AddNumbers(a, b) { return a + b }
```

```sourcepawn
ScriptCall g_AddNumbers;

public void OnPluginStart()
{
	// int AddNumbers(int a, int b)
	g_AddNumbers = new ScriptCall("AddNumbers", ScriptField_Int, ScriptField_Int, ScriptField_Int);

	RegConsoleCmd("sm_add", Command_Add);
}

Action Command_Add(int client, int args)
{
	g_AddNumbers.Execute(10, 25);
	ReplyToCommand(client, "Result: %d", g_AddNumbers.GetReturnInt());
	return Plugin_Handled;
}
```

### Exposing SourcePawn Functions to VScript

Register functions that VScript code can call.
Registrations persist across map changes:

```sourcepawn
public void OnPluginStart()
{
	// void PrintCenterTextAll(string message)
	VScript_RegisterFunction("PrintCenterTextAll", OnPrintCenterTextAll,
		"Prints a message to all clients in the center of the screen.",
		ScriptField_Void, ScriptField_String);

	// string GetCommandLine()
	VScript_RegisterFunction("GetCommandLine", OnGetCommandLine,
		"Gets the full command line the server was launched with.",
		ScriptField_String);
}

void OnPrintCenterTextAll(ScriptContext context)
{
	char message[256];
	context.GetArgString(0, message, sizeof(message));
	PrintCenterTextAll(message);
}

void OnGetCommandLine(ScriptContext context)
{
	char commandLine[512];
	if (GetCommandLine(commandLine, sizeof(commandLine)))
		context.SetReturnString(commandLine);
	else
		context.RaiseException("GetCommandLine failed!");
}
```

VScript can then call these directly:

```squirrel
PrintCenterTextAll("Hello world!")
printl("This server was launched with:\n" + GetCommandLine())
```

### Accepting Any Type with Variant

Use `ScriptField_Variant` for parameters that accept any type, and `GetArgType` to branch at runtime:

```sourcepawn
public void OnPluginStart()
{
	// void ConPrint(any value)
	VScript_RegisterFunction("ConPrint", OnConPrint,
		"Prints any value to the server console.",
		ScriptField_Void, ScriptField_Variant);
}

void OnConPrint(ScriptContext context)
{
	switch (context.GetArgType(0))
	{
		case ScriptField_Int:
			PrintToServer("%d", context.GetArgInt(0));
		case ScriptField_Float:
			PrintToServer("%f", context.GetArgFloat(0));
		case ScriptField_Bool:
			PrintToServer(context.GetArgBool(0) ? "true" : "false");
		case ScriptField_Void:
			PrintToServer("null");
		case ScriptField_String:
		{
			char buffer[256];
			context.GetArgString(0, buffer, sizeof(buffer));
			PrintToServer(buffer);
		}
	}
}
```

```squirrel
ConPrint("Hello")    // prints: Hello
ConPrint(42)         // prints: 42
ConPrint(3.14)       // prints: 3.14
ConPrint(true)       // prints: true
ConPrint(null)       // prints: null
```

### Adding Methods to Entity Classes

Register methods on VScript classes that all instances of that class inherit:

```sourcepawn
public void OnPluginStart()
{
	// void CBaseEntity::SetRenderColor(int r, int g, int b, int a)
	VScript_RegisterClassFunction("CBaseEntity", "SetRenderColor", OnSetRenderColor,
		"Sets the entity's render color.",
		ScriptField_Void, ScriptField_Int, ScriptField_Int, ScriptField_Int, ScriptField_Int);

	// bool CBasePlayer::IsSourceTV()
	VScript_RegisterClassFunction("CBasePlayer", "IsSourceTV", OnIsSourceTV,
		"Returns whether the player is the SourceTV bot.",
		ScriptField_Bool);
}

void OnSetRenderColor(ScriptContext context)
{
	int entity = context.Entity;
	if (entity == -1)
		return;

	int r = context.GetArgInt(0);
	int g = context.GetArgInt(1);
	int b = context.GetArgInt(2);
	int a = context.GetArgInt(3);
	SetEntityRenderColor(entity, r, g, b, a);
}

void OnIsSourceTV(ScriptContext context)
{
	int player = context.Entity;
	if (player == -1)
		return;

	context.SetReturnBool(IsClientSourceTV(player));
}
```

Now any entity in a script can use the new methods:

```squirrel
local player = PlayerInstanceFromIndex(1)
player.SetRenderColor(255, 0, 0, 255)
printl("Is this player SourceTV? " + (player.IsSourceTV() ? "Yep!" : "Nope."))
```

### Working with Entity Handles

> [!IMPORTANT]
> Entity handles from `VScript_EntityToHScript` and `VScript_GetEntityScriptScope` are managed by the extension and invalidated automatically when the entity is destroyed or the VM shuts down. **Do not close them.**

Pass an entity handle to a VScript function:

```squirrel
function GetEntityName(entity) { return entity.GetName() }
```

```sourcepawn
ScriptCall g_GetEntityName;

public void OnPluginStart()
{
	// string GetEntityName(handle entity)
	g_GetEntityName = new ScriptCall("GetEntityName", ScriptField_String, ScriptField_HScript);
}

void PrintEntityName(int entity)
{
	ScriptHandle hEntity = VScript_EntityToHScript(entity);
	if (!hEntity)
		return;

	if (g_GetEntityName.Execute(hEntity) != ScriptStatus_Done)
		return;

	char name[64];
	g_GetEntityName.GetReturnString(name, sizeof(name));
	PrintToServer("Entity name: %s", name);
}
```

Convert an HSCRIPT back to an entity index inside a callback:

```sourcepawn
public void OnPluginStart()
{
	// void Ignite(handle entity)
	VScript_RegisterFunction("Ignite", OnIgnite,
		"Ignites an entity on fire.",
		ScriptField_Void, ScriptField_HScript);
}

void OnIgnite(ScriptContext context)
{
	int entity = context.GetArgEntity(0);
	if (entity != -1)
		IgniteEntity(entity, 10.0);
}
```

```squirrel
local player = PlayerInstanceFromIndex(1)
Ignite(player)
```

### Working with Entity Scopes

Read and write variables on an entity's script scope:

```sourcepawn
void ReadScriptScope(int entity)
{
	ScriptHandle scope = VScript_GetEntityScriptScope(entity);
	if (!scope)
		return;

	// Read a value set by a script: self.is_boss <- true
	if (scope.HasKey("is_boss") && scope.GetBool("is_boss"))
	{
		// Write a value that scripts can read later
		scope.SetInt("health", 1000);
	}
}
```

## Building

Requires [AMBuild](https://github.com/alliedmodders/ambuild).

```bash
git clone --recursive https://github.com/Mikusch/vscript-ext
mkdir build && cd build
python3 ../configure.py \
  --sdk=tf2 \
  --mms-path=/path/to/metamod-source \
  --sm-path=/path/to/sourcemod \
  --sdk-root=/path/to/hl2sdks
ambuild
```

---

Special thanks to [Batfoxkid](https://github.com/Batfoxkid) for thoroughly testing the extension and to [Kenzzer](https://github.com/Kenzzer) for helping with the design.
