using Mono.Cecil;

namespace OpenKogama.Mods.Patcher;

public sealed class MonoImage
{
    public ModuleDefinition Module { get; }

    public MonoImage(string path) => Module = ModuleDefinition.ReadModule(path);
    public MonoImage(ModuleDefinition module) => Module = module;

    public TypeDefinition? Type(string name)
    {
        string dotted = name.Replace('.', '/');
        return Module.GetType(name) ?? Module.GetType(dotted)
            ?? Module.Types.SelectMany(All).FirstOrDefault(t => t.FullName == name || t.FullName == dotted || t.Name == name);
    }

    static IEnumerable<TypeDefinition> All(TypeDefinition type)
    {
        yield return type;
        foreach (TypeDefinition nested in type.NestedTypes)
            foreach (TypeDefinition inner in All(nested))
                yield return inner;
    }

    public MethodDefinition? Find(string type, string method, int? parameters)
    {
        if (Type(type) is not { } t) return null;
        List<MethodDefinition> matches = [.. t.Methods.Where(m => m.Name == method && (parameters is null || m.Parameters.Count == parameters))];
        return matches.Count == 1 ? matches[0] : null;
    }
}
