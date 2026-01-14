#load "../../patcher/lib/_Utils.csx"
#load "../../patcher/lib/_Patch.csx"
#load "../../patcher/lib/_Extension.csx"
#load "../../patcher/lib/_GameObject.csx"
#load "../../patcher/lib/_Graphics.csx"

using System.Threading.Tasks;
using System.Diagnostics;

void PatchPFO50Extension()
{
    var extDllName = "PFO.dll";
    var extensionName = "PFO";
    var extensionVersion = GetModVersion();
    var gameVersion = GetGameVersion();

    var pfoExtension = Data.Extensions.ByName(extensionName);
    if (pfoExtension == null)
    {
        pfoExtension = new UndertaleExtension();
        Data.Extensions.Add(pfoExtension);
    }

    pfoExtension.FolderName = Data.Strings.MakeString("");
    pfoExtension.Name = Data.Strings.MakeString(extensionName);
    pfoExtension.ClassName = Data.Strings.MakeString("");
    pfoExtension.Version = Data.Strings.MakeString(extensionVersion);
    pfoExtension.Files.Clear();
    pfoExtension.Options.Clear();

    uint lastExtFuncId = 0;
    foreach (var extension in Data.Extensions)
    {
        foreach (var file in extension.Files)
        {
            foreach (var func in file.Functions)
            {
                if (func.ID > lastExtFuncId)
                {
                    lastExtFuncId = func.ID;
                }
            }
        }
    }

    {
        void DefineExtensionOption(string name, string value)
        {
            var option = new UndertaleExtensionOption()
            {
                Name = Data.Strings.MakeString(name),
                Value = Data.Strings.MakeString(System.Convert.ToBase64String(System.Text.Encoding.UTF8.GetBytes(value))),
                Kind = UndertaleExtensionOption.OptionKind.String,
            };
            pfoExtension.Options.Add(option);
        }

        DefineExtensionOption("gameVersion", gameVersion);
        DefineExtensionOption("onlineStateChangedCallback", "scrOnlineStateChangedCallback");
        DefineExtensionOption("getInputCallback", "scrGetInputCallback");
        DefineExtensionOption("getChecksumCallback", "scrGetChecksumCallback");
        DefineExtensionOption("checksumBufferMaxSize", "128");
    }

    {
        var file = DefineExtensionFile(Data, pfoExtension, extDllName);
        file.Kind = UndertaleExtensionKind.Generic;
        file.Functions.Clear();

        var funcIdOffset = lastExtFuncId;

        void DefineExtensionFunction(string name)
        {
            file.Functions.DefineExtensionFunction(Data.Functions, Data.Strings, ++funcIdOffset, 11, name, UndertaleExtensionVarType.Double, name);
        }

        DefineExtensionFunction("pfo_update");
        DefineExtensionFunction("pfo_player_get_input");
        DefineExtensionFunction("pfo_is_online");
        DefineExtensionFunction("pfo_client_get_input_delay");
        DefineExtensionFunction("pfo_client_set_input_delay");
        DefineExtensionFunction("pfo_get_input_delay_mode");
        DefineExtensionFunction("pfo_set_input_delay_mode");
        DefineExtensionFunction("pfo_get_input_delay_favored_client_index");
        DefineExtensionFunction("pfo_set_input_delay_favored_client_index");
        DefineExtensionFunction("pfo_get_min_automatic_input_delay");
        DefineExtensionFunction("pfo_set_min_automatic_input_delay");
        DefineExtensionFunction("pfo_get_max_automatic_input_delay");
        DefineExtensionFunction("pfo_set_max_automatic_input_delay");
        DefineExtensionFunction("pfo_get_frame");
        DefineExtensionFunction("pfo_init");
        DefineExtensionFunction("pfo_set_clients");
        DefineExtensionFunction("pfo_set_players");
        DefineExtensionFunction("pfo_get_client_index");
        DefineExtensionFunction("pfo_client_get_player_index");
        DefineExtensionFunction("pfo_player_get_client_index");
        DefineExtensionFunction("pfo_get_client_count");
        DefineExtensionFunction("pfo_get_assigned_clients_count");
        DefineExtensionFunction("pfo_file_exists");
        DefineExtensionFunction("pfo_buffer_load");
        DefineExtensionFunction("pfo_buffer_save");
        DefineExtensionFunction("pfo_file_delete");
        DefineExtensionFunction("pfo_file_copy");
        DefineExtensionFunction("pfo_file_status");
        DefineExtensionFunction("pfo_quit");
        DefineExtensionFunction("pfo_steam_lobby_get_member_data");
        DefineExtensionFunction("pfo_steam_lobby_set_member_data");
        DefineExtensionFunction("pfo_steam_request_lobby_data");
        DefineExtensionFunction("pfo_steam_get_lobby_data");
        DefineExtensionFunction("pfo_steam_get_num_lobby_members");
        DefineExtensionFunction("pfo_steam_get_lobby_member_limit");
        DefineExtensionFunction("pfo_connect");
        DefineExtensionFunction("pfo_client_get_ping");
        DefineExtensionFunction("pfo_reset");
        DefineExtensionFunction("pfo_game_get_speed");
        DefineExtensionFunction("pfo_game_set_speed");
        DefineExtensionFunction("pfo_client_is_connected");
        DefineExtensionFunction("pfo_show_debug_message");


        file.InitScript = Data.Strings.MakeString("pfo_init");
        file.CleanupScript = Data.Strings.MakeString("");
    }

    {
        void DefineFunction(string name)
        {
            Data.Functions.EnsureDefined(name, Data.Strings);
        }

        DefineFunction("steam_get_user_steam_id");
        DefineFunction("steam_get_user_persona_name");
        DefineFunction("steam_get_user_persona_name_sync");

        DefineFunction("steam_lobby_activate_invite_overlay");
        DefineFunction("steam_lobby_create");
        DefineFunction("steam_lobby_get_data");
        DefineFunction("steam_lobby_get_lobby_id");
        DefineFunction("steam_lobby_get_member_count");
        DefineFunction("steam_lobby_get_member_id");
        DefineFunction("steam_lobby_get_owner_id");
        DefineFunction("steam_lobby_is_owner");
        DefineFunction("steam_lobby_join_id");
        DefineFunction("steam_lobby_leave");
        DefineFunction("steam_lobby_set_data");
        DefineFunction("steam_lobby_set_joinable");
        DefineFunction("steam_lobby_set_owner_id");
        DefineFunction("steam_lobby_set_type");
        DefineFunction("steam_lobby_send_chat_message");
        DefineFunction("steam_lobby_get_chat_message_size");
        DefineFunction("steam_lobby_get_chat_message_text");
        DefineFunction("steam_lobby_get_chat_message_data");
        DefineFunction("steam_lobby_send_chat_message");
        DefineFunction("steam_lobby_send_chat_message_buffer");
        DefineFunction("steam_lobby_list_add_distance_filter");
        DefineFunction("steam_lobby_list_add_near_filter");
        DefineFunction("steam_lobby_list_add_numerical_filter");
        DefineFunction("steam_lobby_list_add_string_filter");
        DefineFunction("steam_lobby_list_request");
        DefineFunction("steam_lobby_list_get_count");
        DefineFunction("steam_lobby_list_get_data");
        DefineFunction("steam_lobby_list_get_lobby_id");
        DefineFunction("steam_lobby_list_get_lobby_member_count");
        DefineFunction("steam_lobby_list_get_lobby_member_id");
        DefineFunction("steam_lobby_list_get_lobby_owner_id");
        DefineFunction("steam_lobby_list_is_loading");
        DefineFunction("steam_lobby_list_join");
    }
}

async Task ImportCode()
{
    var dllPath = Path.Join(GetRootDir(), "UFO 50/PFO.dll");
    if (File.Exists(dllPath))
    {
        var dllVersionInfo = FileVersionInfo.GetVersionInfo(dllPath);
        Environment.SetEnvironmentVariable("DEBUG", dllVersionInfo.IsDebug ? "1" : "");
    }

    var scriptDir = Path.GetDirectoryName(GetCurrentScript());
    var codeDir = Path.Join(scriptDir, "code");
    var globalDir = Path.Join(scriptDir, "globals");
    await ImportCodeDir(codeDir, true, globalDir);
}

void ImportSprites()
{
    var scriptDir = Path.GetDirectoryName(GetCurrentScript());
    ImportGraphics(scriptDir, true);
}
