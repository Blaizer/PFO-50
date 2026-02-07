using System.Text.Json;

struct Settings
{
    public string Version { get; set; }
    public List<string> EnabledMods { get; set; }
}

var settings = JsonSerializer.Deserialize<Settings>(File.ReadAllText(Path.Combine(modsPath, "..", "settings.json")));
var enabledMods = new HashSet<string>(settings.EnabledMods);
var gmloaderVersion = settings.Version;

var modVersions = new Dictionary<string, string>();

struct DownloadedMods
{
    public string Name { get; set; }
    public string Version { get; set; }
}

var downloadedModsFile = Path.Combine(modsPath, "..", "downloaded_mods.json");
if (File.Exists(downloadedModsFile))
{
    var downloadedMods = JsonSerializer.Deserialize<List<DownloadedMods>>(File.ReadAllText(downloadedModsFile));
    foreach (var mod in downloadedMods)
    {
        modVersions[mod.Name] = mod.Version;
    }
}

struct GamebananaInfo
{
    public string Version { get; set; }
}

var myModsFolder = Path.Combine(modsPath, "..", "..", "my mods");
if (Directory.Exists(myModsFolder))
{
    foreach (var modPath in Directory.GetDirectories(myModsFolder))
    {
        var modName = Path.GetFileName(modPath);
        if (enabledMods.Contains(modName))
        {
            var gamebananaFile = Path.Combine(modPath, "gamebanana.json");
            if (File.Exists(gamebananaFile))
            {
                var gamebananaInfo = JsonSerializer.Deserialize<GamebananaInfo>(File.ReadAllText(gamebananaFile));
                modVersions[modName] = gamebananaInfo.Version;
            }
        }
    }
}

var modsList = new List<string>();
foreach (var modName in enabledMods)
{
    if (modName == "PFO 50") continue;

    var modVersion = "";
    if (modVersions.TryGetValue(modName, out var version))
    {
        modVersion = version;
    }

    modsList.Add(modName);
    modsList.Add(modVersion);
}
Log.Information($"Installed mods are: {JsonSerializer.Serialize(modsList)}");

static byte[] ReadExactly(Stream stream, long length)
{
    byte[] buffer = new byte[length];
    stream.ReadExactly(buffer);
    return buffer;
}

// CRC32 algorithm for PNG file chunks. Technically not needed since we recompile
// the data.win again, but here for correctness at all steps of the process
class Crc32
{
    static readonly uint[] crc_table = make_crc_table();

    static uint[] make_crc_table()
    {
        var crc_table = new uint[256];
        for (int n = 0; n < 256; n++)
        {
            uint c = (uint)n;
            for (int k = 0; k < 8; k++)
            {
                c = (c & 1) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            crc_table[n] = c;
        }
        return crc_table;
    }

    public static uint ComputeHash(byte[] buf)
    {
        uint c = 0xffffffffu;
        foreach (byte b in buf)
        {
            c = crc_table[(c ^ b) & 0xff] ^ (c >> 8);
        }
        return System.Buffers.Binary.BinaryPrimitives.ReverseEndianness(c ^ 0xffffffffu);
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
string hash;
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
                                uint crc = Crc32.ComputeHash(buffer);
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
    using var md5 = System.Security.Cryptography.MD5.Create();
    hash = Convert.ToHexString(md5.ComputeHash(stream)).ToLowerInvariant();
    Log.Information($"Canonical hash for data.win is: {hash}");
}

struct CompatInfo
{
    public string hash { get; set; }
    public string version { get; set; }
    public List<string> mods { get; set; }
}

var compat = new CompatInfo()
{
    hash = hash,
    version = gmloaderVersion,
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
    Value = Data.Strings.MakeString(System.Convert.ToBase64String(System.Text.Encoding.UTF8.GetBytes(JsonSerializer.Serialize(compat)))),
    Kind = UndertaleExtensionOption.OptionKind.String,
};
pfoExtension.Options.Add(option);

Log.Information("Recompiling the data again with compat info...");
using (var stream = new FileStream(gameDataPath, FileMode.Create, FileAccess.ReadWrite))
{
    UndertaleIO.Write(stream, Data);
}
