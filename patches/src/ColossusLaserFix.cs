using Mono.Cecil;
using Mono.Cecil.Cil;
using Mono.Cecil.Rocks;

namespace OpenKogama.Mods.Patches;

public static class ColossusLaserFix
{

    public static bool Apply(ModuleDefinition module) => Guard(module, "GodzillaLaser", "OnUnequip");

    public static bool Guard(ModuleDefinition module, string typeName, string methodName)
    {
        if (module.GetType(typeName)?.Methods.FirstOrDefault(method => method.Name == methodName && !method.HasParameters && method.HasBody
            && method.ReturnType.MetadataType == MetadataType.Void) is not { } method)
            return false;
        MethodBody body = method.Body;
        if (body.ExceptionHandlers.Any(handler => handler.HandlerType == ExceptionHandlerType.Catch && handler.TryStart == body.Instructions[0]))
            return false;

        body.SimplifyMacros();
        Instruction start = body.Instructions[0];
        Instruction caught = Instruction.Create(OpCodes.Pop);
        Instruction end = Instruction.Create(OpCodes.Ret);
        foreach (Instruction instruction in body.Instructions.Where(instruction => instruction.OpCode == OpCodes.Ret))
        {
            instruction.OpCode = OpCodes.Leave;
            instruction.Operand = end;
        }
        ILProcessor il = body.GetILProcessor();
        il.Append(caught);
        il.Append(Instruction.Create(OpCodes.Leave, end));
        il.Append(end);
        body.ExceptionHandlers.Add(new ExceptionHandler(ExceptionHandlerType.Catch)
        {
            TryStart = start,
            TryEnd = caught,
            HandlerStart = caught,
            HandlerEnd = end,
            CatchType = new TypeReference("System", "Exception", module, module.TypeSystem.CoreLibrary),
        });
        body.OptimizeMacros();
        return true;
    }
}
