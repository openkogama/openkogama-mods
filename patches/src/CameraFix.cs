using Mono.Cecil;
using Mono.Cecil.Cil;
using Mono.Cecil.Rocks;

namespace OpenKogama.Mods.Patches;

public static class CameraFix
{
    const string Camera = "PlaymodeCamera";

    public static bool Apply(ModuleDefinition module)
    {
        bool changed = false;
        foreach (TypeDefinition type in module.GetTypes().Where(IsCamera))
            foreach (MethodDefinition method in type.Methods.Where(method => method.HasBody))
                changed |= Clamp(module, method.Body);
        return changed;
    }

    static bool Clamp(ModuleDefinition module, MethodBody body)
    {
        List<Instruction> roots = [.. body.Instructions.Where(IsSqrt).Where(call => !IsClamped(call))];
        if (roots.Count == 0) return false;

        body.SimplifyMacros();
        ILProcessor il = body.GetILProcessor();
        foreach (Instruction call in roots)
        {
            var max = new MethodReference("Max", module.TypeSystem.Single, ((MethodReference)call.Operand).DeclaringType);
            max.Parameters.Add(new ParameterDefinition(module.TypeSystem.Single));
            max.Parameters.Add(new ParameterDefinition(module.TypeSystem.Single));
            Instruction zero = il.Create(OpCodes.Ldc_R4, 0f);
            il.InsertBefore(call, zero);
            il.InsertBefore(call, il.Create(OpCodes.Call, max));
            Retarget(body, call, zero);
        }
        body.OptimizeMacros();
        return true;
    }

    static void Retarget(MethodBody body, Instruction from, Instruction to)
    {
        foreach (Instruction instruction in body.Instructions)
        {
            if (instruction.Operand == from && instruction.OpCode.FlowControl is FlowControl.Branch or FlowControl.Cond_Branch) instruction.Operand = to;
            if (instruction.Operand is Instruction[] targets)
                for (int i = 0; i < targets.Length; i++)
                    if (targets[i] == from) targets[i] = to;
        }
        foreach (ExceptionHandler handler in body.ExceptionHandlers)
        {
            if (handler.TryStart == from) handler.TryStart = to;
            if (handler.HandlerStart == from) handler.HandlerStart = to;
            if (handler.FilterStart == from) handler.FilterStart = to;
        }
    }

    static bool IsSqrt(Instruction instruction) =>
        instruction.OpCode == OpCodes.Call && instruction.Operand is MethodReference { Name: "Sqrt", DeclaringType.Name: "Mathf" } method && method.Parameters.Count == 1;

    static bool IsClamped(Instruction call) => call.Previous is { OpCode.Code: Code.Call, Operand: MethodReference { Name: "Max", DeclaringType.Name: "Mathf" } };

    static bool IsCamera(TypeDefinition type)
    {
        for (TypeDefinition? at = type; at is not null; at = at.BaseType is { } parent ? type.Module.GetType(parent.FullName) : null)
            if (at.Name == Camera) return true;
        return false;
    }
}
