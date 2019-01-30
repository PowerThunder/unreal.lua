#include "LuaFunctionInjectHelper.h"
#include "UnrealLuaInterface.h"
#include "UnrealLua.h"

DEFINE_FUNCTION(LuaFunctionInjectHelper::execCallLua)
{
	lua_State*inL = UUnrealLuaInterface::GetLuaStateByIns(Stack.Object);
	UTableUtil::call(inL, Stack, RESULT_PARAM);
}

extern uint8 GRegisterNative(int32 NativeBytecodeIndex, const FNativeFuncPtr& Func);
static FNativeFunctionRegistrar CallLuaRegistrar(UObject::StaticClass(), "execCallLua", (FNativeFuncPtr)&LuaFunctionInjectHelper::execCallLua);
static uint8 CallLuaBytecode = GRegisterNative(EX_CallLua, (FNativeFuncPtr)&LuaFunctionInjectHelper::execCallLua);

LuaFunctionInjectHelper* LuaFunctionInjectHelper::SingletonIns = nullptr;
LuaFunctionInjectHelper* LuaFunctionInjectHelper::Get()
{
	if (SingletonIns == nullptr)
		SingletonIns = new LuaFunctionInjectHelper;
	return SingletonIns;
}

void LuaFunctionInjectHelper::Destroy()
{
	if (SingletonIns)
	{
		delete SingletonIns;
		SingletonIns = nullptr;
	}
}

LuaFunctionInjectHelper::LuaFunctionInjectHelper()
{
}

LuaFunctionInjectHelper::~LuaFunctionInjectHelper()
{
}

void LuaFunctionInjectHelper::ReplaceUClassFunction(lua_State*inL, UClass* Class, const char* LuaClassPath)
{
	FName ClassName = Class->GetFName();
	TMap<FName, UClass*>& LuaClassNameToClass = HasReplaceClass.FindOrAdd(ClassName);
	FName LuaClassName = LuaClassPath;
	UClass** ClassHasAddedPtr = LuaClassNameToClass.Find(LuaClassName);
	if (ClassHasAddedPtr && *ClassHasAddedPtr == Class)
		return;
	LuaClassNameToClass.Add(LuaClassName, Class);

	TSet<FName> LuaFunctionNames = LuaStaticCallr_State(inL, TSet<FName>, "GetClassFunctionNames", LuaClassPath);

	TMap<FName, UFunction*> Functions;
	for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated, EFieldIteratorFlags::IncludeInterfaces); It; ++It)
	{
		UFunction *Function = *It;
		if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent))
		{
			FName FuncName = Function->GetFName();
			UFunction **FuncPtr = Functions.Find(FuncName);
			if (!FuncPtr)
			{
				Functions.Add(FuncName, Function);
			}
		}
	}

	for (int32 i = 0; i < Class->ClassReps.Num(); ++i)
	{
		UProperty *Property = Class->ClassReps[i].Property;
		if (Property->HasAnyPropertyFlags(CPF_RepNotify))
		{
			UFunction *Function = Class->FindFunctionByName(Property->RepNotifyFunc);
			if (Function)
			{
				UFunction **FuncPtr = Functions.Find(Property->RepNotifyFunc);
				if (!FuncPtr)
				{
					Functions.Add(Property->RepNotifyFunc, Function);
				}
			}
		}
	}

	for (const FName &LuaFuncName : LuaFunctionNames)
	{
		UFunction **Func = Functions.Find(LuaFuncName);
		if (Func)
		{
			UFunction *Function = *Func;
			if (Function->GetOuter() != Class)
			{
				AddFunction(Function, Class, LuaFuncName);
			}
			else
			{
				ReplaceFunction(Function, Class, LuaFuncName);
			}
		}
	}
}

void LuaFunctionInjectHelper::AddFunction(UFunction *TemplateFunction, UClass *OuterClass, FName NewFuncName)
{
// 	UFunction *Func = OuterClass->FindFunctionByName(NewFuncName, EIncludeSuperFlag::ExcludeSuper);
// 	if (!Func)
	{
		UFunction *NewFunc = DuplicateFunction(TemplateFunction, OuterClass, NewFuncName);
	}
}

void ReplaceNativeFunc(UFunction *Function, FNativeFuncPtr NativeFunc)
{
	Function->SetNativeFunc(NativeFunc);
	if (Function->Script.Num() < 1)
	{
		Function->Script.Add(EX_CallLua);
		Function->Script.Add(EX_Return);
		Function->Script.Add(EX_Nothing);
	}
}


void LuaFunctionInjectHelper::ReplaceFunction(UFunction *TemplateFunction, UClass *OuterClass, FName NewFuncName)
{
	FNativeFuncPtr *NativePtr = CachedNatives.Find(TemplateFunction);
	if (!NativePtr)
	{
// 		CachedNatives.Add(TemplateFunction, TemplateFunction->GetNativeFunc());
		if (!TemplateFunction->HasAnyFunctionFlags(FUNC_Native) && TemplateFunction->Script.Num() > 0)
		{
// 			CachedScripts.Add(TemplateFunction, TemplateFunction->Script);
			TemplateFunction->Script.Empty(3);
		}
		ReplaceNativeFunc(TemplateFunction, (FNativeFuncPtr)&LuaFunctionInjectHelper::execCallLua);
// 		GReflectionRegistry.RegisterFunction(TemplateFunction);
	}
}

struct FFakeProperty : public UField
{
	int32		ArrayDim;
	int32		ElementSize;
	uint64		PropertyFlags;
	uint16		RepIndex;
	TEnumAsByte<ELifetimeCondition> BlueprintReplicationCondition;
	int32		Offset_Internal;
};

UFunction* DuplicateUFunction(UFunction *TemplateFunction, UClass *OuterClass, FName NewFuncName)
{
	static int32 Offset = offsetof(FFakeProperty, Offset_Internal);		// (int32)offsetof(UProperty, RepNotifyFunc) - sizeof(int32);	// offset for Offset_Internal... todo: use UProperty::Link()
	UFunction *NewFunc = DuplicateObject(TemplateFunction, OuterClass, NewFuncName);
// todo
	NewFunc->AddToRoot();
// 
	NewFunc->PropertiesSize = TemplateFunction->PropertiesSize;
	NewFunc->MinAlignment = TemplateFunction->MinAlignment;
	int32 NumParams = NewFunc->NumParms;
	if (NumParams > 0)
	{
		NewFunc->PropertyLink = Cast<UProperty>(NewFunc->Children);
		UProperty *SrcProperty = Cast<UProperty>(TemplateFunction->Children);
		UProperty *DestProperty = NewFunc->PropertyLink;
		while (true)
		{
			check(SrcProperty && DestProperty);
			DestProperty->ArrayDim = SrcProperty->ArrayDim;
			DestProperty->ElementSize = SrcProperty->ElementSize;
			DestProperty->PropertyFlags = SrcProperty->PropertyFlags;
			DestProperty->RepIndex = SrcProperty->RepIndex;
			*((int32*)((uint8*)DestProperty + Offset)) = *((int32*)((uint8*)SrcProperty + Offset));		// set Offset_Internal ...
			if (--NumParams < 1)
			{
				break;
			}
			DestProperty->PropertyLinkNext = Cast<UProperty>(DestProperty->Next);
			DestProperty = DestProperty->PropertyLinkNext;
			SrcProperty = SrcProperty->PropertyLinkNext;
		}
	}
	OuterClass->AddFunctionToFunctionMap(NewFunc, NewFuncName);
// 	GObjectReferencer.AddObjectRef(NewFunc);
// 	GReflectionRegistry.RegisterFunction(NewFunc);
	return NewFunc;
}

UFunction* LuaFunctionInjectHelper::DuplicateFunction(UFunction *TemplateFunction, UClass *OuterClass, FName NewFuncName)
{
	TMap<FName, UFunction*>& CachedFuncOfClass = CachedClassNewUFunctions.FindOrAdd(OuterClass->GetFName());
	UFunction** ExistNewFuncPtr = CachedFuncOfClass.Find(NewFuncName);
	UFunction *NewFunc = nullptr;
	if (ExistNewFuncPtr != nullptr)
	{
		NewFunc = *ExistNewFuncPtr;
	}
	else
	{
		NewFunc = DuplicateUFunction(TemplateFunction, OuterClass, NewFuncName);
		if (!NewFunc->HasAnyFunctionFlags(FUNC_Native) && NewFunc->Script.Num() > 0)
		{
			NewFunc->Script.Empty(3);
		}
		ReplaceNativeFunc(NewFunc, (FNativeFuncPtr)&LuaFunctionInjectHelper::execCallLua);
		CachedFuncOfClass.Add(NewFuncName, NewFunc);
	}
	return NewFunc;
}
