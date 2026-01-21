#load "../../PFO-50/patcher/lib/_UFO50.csx"

using System.Text.Json;
using System.Security;

var requiredVersion = new Version("1.3.10");
var requiredVersionMessage = $"GMLoader version {requiredVersion} or newer is required to install PFO 50.";

struct Settings
{
    public string Version { get; set; }
    public List<string> EnabledMods { get; set; }
}

string settingsText;
try
{
    settingsText = File.ReadAllText("settings.json");
}
catch (IOException ex)
{
    throw new ScriptException($"Failed to read settings.json: {ex.Message}");
}
catch (Exception ex) when (ex is FileNotFoundException || ex is DirectoryNotFoundException)
{
    throw new ScriptException($"Couldn't find settings.json: {ex.Message}");
}
catch (Exception ex) when (ex is UnauthorizedAccessException || ex is SecurityException)
{
    throw new ScriptException($"Don't have permission to read settings.json: {ex.Message}");
}

Settings settings;
try
{
    settings = JsonSerializer.Deserialize<Settings>(settingsText);
}
catch (JsonException ex)
{
    throw new ScriptException($"The settings.json file is invalid: {ex.Message}");
}

if (string.IsNullOrEmpty(settings.Version))
{
    throw new ScriptException($"The settings.json file does't contain a Version string. {requiredVersionMessage}");
}

Version settingsVersion;
try
{
    settingsVersion = new Version(settings.Version);
}
catch
{
    throw new ScriptException(@$"GMLoader version string ""{settings.Version}"" from settings.json is invalid. {requiredVersionMessage}");
}

if (settingsVersion < requiredVersion)
{
    throw new ScriptException($"{requiredVersionMessage} You are using version {settingsVersion}");
}

var ufo50Version = GetUFO50Version(Data);
var expectedUfo50Version = new Version(GetGameVersion());

if (ufo50Version != expectedUfo50Version)
{
    throw new ScriptException($"UFO 50 version {expectedUfo50Version} is required to install PFO 50. The version of your vanilla.win is {ufo50Version}");
}

Log.Information($"PFO 50 version check OK. You are using GMLoader version {settingsVersion} and UFO 50 version {ufo50Version}");
