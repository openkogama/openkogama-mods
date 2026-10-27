using System.Buffers.Binary;
using System.Text;
using System.Text.Json;

namespace OpenKogama.Mods.Patches;

public sealed record SoundEntry(string Url, string Name, string Group);

public sealed record ServerContent(IReadOnlyList<SoundEntry> Sounds, IReadOnlyList<ThemeSpec> Themes)
{
    public static ServerContent FromJson(string json)
    {
        using JsonDocument document = JsonDocument.Parse(json);
        List<SoundEntry> sounds = document.RootElement.TryGetProperty("sounds", out JsonElement soundList)
            ? [.. soundList.EnumerateArray().Select(sound => new SoundEntry(Text(sound, "url"), Text(sound, "name"), Text(sound, "group")))]
            : [];
        List<ThemeSpec> themes = document.RootElement.TryGetProperty("themes", out JsonElement themeList)
            ? [.. themeList.EnumerateArray().Select(ThemeOf)]
            : [];
        return new ServerContent(sounds, themes);
    }

    static string Text(JsonElement element, string name) => element.GetProperty(name).GetString() ?? "";

    static ThemeSpec ThemeOf(JsonElement theme)
    {
        string name = Text(theme, "name");
        return new ThemeSpec(
            name,
            theme.TryGetProperty("base", out JsonElement basis) ? basis.GetString() ?? name : name,
            theme.TryGetProperty("title", out JsonElement title) ? title.GetString() ?? name : name,
            theme.TryGetProperty("description", out JsonElement description) ? description.GetString() ?? "" : "",
            theme.TryGetProperty("settings", out JsonElement settings) ? ThemeKeys.FromJson(settings) : new Dictionary<string, object>(),
            theme.TryGetProperty("textures", out JsonElement textures)
                ? textures.EnumerateObject().ToDictionary(texture => texture.Name, texture => texture.Value.GetString() ?? "")
                : new Dictionary<string, string>(),
            theme.TryGetProperty("cycle", out JsonElement cycle) && cycle.ValueKind == JsonValueKind.Object ? ThemeCycleData.FromJson(cycle) : null,
            theme.TryGetProperty("folder", out JsonElement folder) ? folder.GetString() ?? "" : "",
            theme.TryGetProperty("files", out JsonElement files) && files.ValueKind == JsonValueKind.Object
                ? files.EnumerateObject().ToDictionary(file => file.Name, file => file.Value.GetString() ?? "")
                : null);
    }
}

public static class ContentPatch
{
    const string SoundRoot = "AmbientAudio/";
    const int MaxSounds = 10_000;

    sealed record SoundList(int Node, SerializedFile.Entry Entry, int CountAt, int Count, List<string> Categories);

    public static byte[] Apply(byte[] original, ServerContent content, out List<string> applied)
    {
        applied = [];
        return original.AsSpan().StartsWith("UnityFS"u8)
            ? Patch(original, content.Sounds, content.Themes, out applied)
            : content.Themes.Count > 0 && ThemePatch.Holds(original) ? ThemePatch.Apply(original, content.Themes, out applied) : original;
    }

    static byte[] Patch(byte[] archive, IReadOnlyList<SoundEntry> sounds, IReadOnlyList<ThemeSpec> themes, out List<string> applied)
    {
        (string engine, List<ArchiveNode> nodes) = UnityArchive.Read(archive);
        applied = [];
        int holder = themes.Count > 0 ? nodes.FindIndex(node => (node.Flags & UnityArchive.SerializedFileNode) != 0 && ThemePatch.Holds(node.Data)) : -1;
        if (holder >= 0) nodes[holder] = nodes[holder] with { Data = ThemePatch.Apply(nodes[holder].Data, themes, out applied) };
        if (sounds.Count == 0) return UnityArchive.Write(engine, nodes);

        List<SoundList> lists = FindLists(nodes);
        if (lists.Count == 0) throw new InvalidDataException("the client has no sound list this patch knows");

        HashSet<string> known = [.. lists.SelectMany(list => list.Categories)];
        foreach (IGrouping<int, SoundList> node in lists.GroupBy(list => list.Node))
        {
            var file = new SerializedFile(nodes[node.Key].Data);
            foreach (SoundList list in node)
            {
                List<SoundEntry> added = [.. sounds.Where(sound => list.Categories.Contains(sound.Group))
                    .Concat(sounds.Where(sound => !known.Contains(sound.Group)).Select(sound => sound with { Group = list.Categories[0] }))];
                if (added.Count > 0) file = new SerializedFile(file.With(list.Entry, Extend(file.Data(list.Entry).ToArray(), list, added)));
            }
            nodes[node.Key] = nodes[node.Key] with { Data = file.ToArray() };
        }
        return UnityArchive.Write(engine, nodes);
    }

    static List<SoundList> FindLists(List<ArchiveNode> nodes)
    {
        byte[] root = Encoding.ASCII.GetBytes(SoundRoot);
        var lists = new List<SoundList>();
        for (int i = 0; i < nodes.Count; i++)
        {
            if ((nodes[i].Flags & UnityArchive.SerializedFileNode) == 0 || nodes[i].Data.AsSpan().IndexOf(root) < 0) continue;
            var file = new SerializedFile(nodes[i].Data);
            foreach (SerializedFile.Entry entry in file.Objects.Where(entry => entry.ClassId == SerializedFile.MonoBehaviourClass))
                if (Read(file.Data(entry).ToArray(), root) is (int countAt, int count, List<string> categories))
                    lists.Add(new SoundList(i, entry, countAt, count, categories));
        }
        return lists;
    }

    static (int CountAt, int Count, List<string> Categories)? Read(byte[] data, byte[] root)
    {
        int first = data.AsSpan().IndexOf(root);
        if (first < 8) return null;

        int countAt = first - 8;
        var reader = new ByteReader(data, countAt);
        int count = reader.Int();
        if (count is < 1 or > MaxSounds) return null;
        var categories = new List<string>();
        try
        {
            for (int i = 0; i < count; i++)
            {
                reader.Text();
                reader.Text();
                string category = reader.Text();
                if (!categories.Contains(category)) categories.Add(category);
            }
        }
        catch (Exception error) when (error is InvalidDataException or ArgumentException or IndexOutOfRangeException)
        {
            return null;
        }
        return reader.Position == data.Length ? (countAt, count, categories) : null;
    }

    static byte[] Extend(byte[] data, SoundList list, IReadOnlyList<SoundEntry> sounds)
    {
        using var output = new MemoryStream();
        output.Write(data, 0, list.CountAt);
        WriteInt(output, list.Count + sounds.Count);
        output.Write(data, list.CountAt + 4, data.Length - list.CountAt - 4);
        foreach (SoundEntry sound in sounds)
        {
            WriteText(output, sound.Url);
            WriteText(output, sound.Name);
            WriteText(output, sound.Group);
        }
        return output.ToArray();
    }

    static void WriteInt(Stream stream, int value)
    {
        Span<byte> bytes = stackalloc byte[4];
        BinaryPrimitives.WriteInt32LittleEndian(bytes, value);
        stream.Write(bytes);
    }

    static void WriteText(Stream stream, string text)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(text);
        WriteInt(stream, bytes.Length);
        stream.Write(bytes);
        stream.Write(new byte[(4 - bytes.Length % 4) % 4]);
    }
}
