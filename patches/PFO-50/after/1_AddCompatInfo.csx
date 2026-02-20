#load "../../PFO-50/patcher/lib/_Utils.csx"

using System.Text.Json;
using System.Collections.Concurrent;

struct Settings
{
    public string Version { get; set; }
}

var settings = JsonSerializer.Deserialize<Settings>(File.ReadAllText(Path.Combine(modsPath, "..", "settings.json")));
var modLoaderVersion = settings.Version;

var enabledMods = new List<string>();
var modVersions = new Dictionary<string, string>();
var modHashes = new ConcurrentDictionary<string, string>();

struct GamebananaInfo
{
    public string Version { get; set; }
}

foreach (var modPath in enabledModPaths)
{
    var modName = Path.GetFileName(modPath);
    enabledMods.Add(modName);

    var gamebananaFile = Path.Combine(modPath, "gamebanana.json");
    if (File.Exists(gamebananaFile))
    {
        var gamebananaInfo = JsonSerializer.Deserialize<GamebananaInfo>(File.ReadAllText(gamebananaFile));
        modVersions[modName] = gamebananaInfo.Version;
    }
}

IEnumerable<string> GetCanonicallyOrderedFiles(string path, string relativePath = "", bool skipFiles = false)
{
    var dirInfo = new DirectoryInfo(path);

    if (!skipFiles)
    {
        foreach (var file in dirInfo.GetFiles().OrderBy(f => f.Name, StringComparer.OrdinalIgnoreCase))
        {
            if ((file.Attributes & (FileAttributes.Hidden | FileAttributes.System)) == 0)
            {
                yield return $"{relativePath}/{file.Name}".TrimStart('/');
            }
        }
    }

    foreach (var dir in dirInfo.GetDirectories().OrderBy(d => d.Name, StringComparer.OrdinalIgnoreCase))
    {
        if ((dir.Attributes & (FileAttributes.Hidden | FileAttributes.System)) == 0)
        {
            foreach (var file in GetCanonicallyOrderedFiles(dir.FullName, $"{relativePath}/{dir.Name}".TrimStart('/')))
            {
                yield return file;
            }
        }
    }
}

var hashModsTask = Task.Run(() => Parallel.ForEach(enabledModPaths, modPath =>
{
    try
    {
        var hash = Crc32.InitialValue;
        var buffer = Crc32.CreateBuffer();

        // `skipFiles` is used to skip any files in the root mod directory
        foreach (var relativePath in GetCanonicallyOrderedFiles(modPath, skipFiles: true))
        {
            var canonicalPathBytes = Encoding.UTF8.GetBytes(relativePath.ToUpperInvariant());
            hash = Crc32.ComputeHash(canonicalPathBytes, hash);

            using var stream = File.OpenRead(Path.Combine(modPath, relativePath));
            hash = Crc32.ComputeHash(stream, buffer, hash);
        }

        var hashString = System.Convert.ToBase64String(BitConverter.GetBytes(hash)).TrimEnd('=');
        modHashes[Path.GetFileName(modPath)] = hashString;
    }
    catch {}
}));

byte[] ReadExactly(Stream stream, long length)
{
    byte[] buffer = new byte[length];
    stream.ReadExactly(buffer);
    return buffer;
}

// CRC32 algorithm for PNG file chunks. Technically not needed since we recompile
// the data.win again, but here for correctness at all steps of the process
class Crc32
{
    public const uint InitialValue = 0xffffffffu;
    static readonly uint[] table = MakeTable();

    static uint[] MakeTable()
    {
        var table = new uint[256];
        for (int n = 0; n < 256; n++)
        {
            uint c = (uint)n;
            for (int k = 0; k < 8; k++)
            {
                c = (c & 1) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            table[n] = c;
        }
        return table;
    }

    static uint UpdateHash(byte[] buf, int length, uint c)
    {
        for (int i = 0; i < length; i++)
        {
            c = table[(c ^ buf[i]) & 0xff] ^ (c >> 8);
        }
        return c;
    }

    public static uint ComputeHash(byte[] buf, uint c = InitialValue)
    {
        return UpdateHash(buf, buf.Length, c) ^ InitialValue;
    }

    public static uint ComputeHash(Stream stream, byte[] buf, uint c = InitialValue)
    {
        int length;
        while ((length = stream.Read(buf, 0, buf.Length)) > 0)
        {
            c = Crc32.UpdateHash(buf, length, c);
        }
        return c ^ InitialValue;
    }

    public static byte[] CreateBuffer()
    {
        return new byte[64 * 1024];
    }
}

byte[] bytes;
using (var stream = new FileStream(gameDataPath, FileMode.Open, FileAccess.Read))
{
    bytes = ReadExactly(stream, stream.Length);
};

// This code looks insane and it kinda is. We need to compute a hash for data.win.
// The problem is: data.win can contain PNG files with non-deterministic timestamps.
// To fix this, we read through the chunks of data.win, find the TXTR chunk, then
// find any timestamps within that, and replace them with a canonical timestamp.
// Then we can compute a hash for data.win, insert it in, then recompile it again.
string dataHash;
using (var stream = new MemoryStream(bytes))
{
    using var reader = new BinaryReader(stream);
    using var writer = new BinaryWriter(stream);

    uint form = reader.ReadUInt32();
    if (form != 0x4d524f46)
    {
        throw new ScriptException("Expected FORM at start");
    }

    uint formSize = reader.ReadUInt32();
    if (formSize + 8 != stream.Length)
    {
        throw new ScriptException("Size is wrong");
    }

    while (stream.Position + 8 <= stream.Length)
    {
        uint chunk = reader.ReadUInt32();
        uint chunkSize = reader.ReadUInt32();
        long chunkEnd = stream.Position + chunkSize;

        if (chunk == 0x52545854) // TXTR
        {
            while (stream.Position + 12 <= chunkEnd)
            {
                long nextPosition = stream.Position + 1;

                ulong textdate = reader.ReadUInt64();
                if (textdate == 0x6574616474584574) // tEXtdate
                {
                    stream.Position -= 12;
                    uint textChunkSize = System.Buffers.Binary.BinaryPrimitives.ReverseEndianness(reader.ReadUInt32());
                    if (stream.Position + textChunkSize + 4 <= stream.Length)
                    {
                        byte[] buffer = ReadExactly(stream, textChunkSize + 4);
                        if (buffer[8] == (byte)':')
                        {
                            int i = 9;
                            while (i < buffer.Length && buffer[i++] != 0) ;

                            var timestamp = "1970-01-01T00:00:00+00:00";
                            if (buffer.Length - i == timestamp.Length)
                            {
                                Encoding.ASCII.GetBytes(timestamp, 0, timestamp.Length, buffer, i);
                                stream.Position -= buffer.Length;
                                stream.Write(buffer, 0, buffer.Length);
                                uint crc = System.Buffers.Binary.BinaryPrimitives.ReverseEndianness(Crc32.ComputeHash(buffer));
                                writer.Write(crc);
                                continue;
                            }
                        }
                    }
                }

                stream.Position = nextPosition;
            }
        }

        stream.Position = chunkEnd;
    }

    stream.Position = 0;
    var hash = Crc32.ComputeHash(stream, Crc32.CreateBuffer());
    dataHash = System.Convert.ToBase64String(BitConverter.GetBytes(hash)).TrimEnd('=');
    Log.Information($"Canonical hash for data.win is: {dataHash}");
}

await hashModsTask;

var modsList = new List<string>();
foreach (var modName in enabledMods)
{
    var modVersion = "";
    if (modName == "PFO 50")
    {
        modVersion = GetModVersion();
        if (modVersion.EndsWith(".0"))
        {
            modVersion = modVersion.Substring(0, modVersion.Length - 2);
        }
    }
    else if (modVersions.TryGetValue(modName, out var v))
    {
        modVersion = v;
    }

    var modHash = "";
    if (modHashes.TryGetValue(modName, out var h))
    {
        modHash = h;
    }

    modsList.Add(modName);
    modsList.Add(modVersion);
    modsList.Add(modHash);
}

var jsonOptions = new JsonSerializerOptions { Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping };
Log.Information($"Installed mods are: {JsonSerializer.Serialize(modsList, jsonOptions)}");

struct CompatInfo
{
    public string hash { get; set; }
    public string version { get; set; }
    public List<string> mods { get; set; }
}

var compat = new CompatInfo()
{
    hash = dataHash,
    version = modLoaderVersion,
    mods = modsList,
};

var pfoExtension = Data.Extensions.ByName("PFO");
if (pfoExtension == null)
{
    throw new ScriptException("No PFO extension");
}

var option = new UndertaleExtensionOption()
{
    Name = Data.Strings.MakeString("compat"),
    Value = Data.Strings.MakeString(System.Convert.ToBase64String(System.Text.Encoding.UTF8.GetBytes(JsonSerializer.Serialize(compat, jsonOptions)))),
    Kind = UndertaleExtensionOption.OptionKind.String,
};
pfoExtension.Options.Add(option);

Log.Information("Recompiling the data again with compat info...");
using (var stream = new FileStream(gameDataPath, FileMode.Create, FileAccess.ReadWrite))
{
    UndertaleIO.Write(stream, Data);
}
