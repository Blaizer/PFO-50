function scrUpdateLobbyUsers(lobbyId)
{
    if (lobbyId == steam_lobby_get_lobby_id())
    {
        var count = steam_lobby_get_member_count();
        var ownerSteamId = steam_lobby_get_owner_id();
        ds_list_clear(lobbyUsers);
        var readyCount = 0;

        for (var i = 0; i < count; i++)
        {
            var steamId = steam_lobby_get_member_id(i);
            var personaName = string(steam_get_user_persona_name_sync(steamId));
            var ready = pfo_steam_lobby_get_member_data(lobbyId, steamId, "ready") != "";
            if (ready) readyCount++;
            var data = { steamId: steamId, personaName: personaName, ready: ready };

            if (steamId == ownerSteamId)
            {
                ds_list_insert(lobbyUsers, 0, data);
            }
            else
            {
                ds_list_add(lobbyUsers, data);
            }
        }

        if (!lobbyStartingGame && readyCount > 1 && readyCount == ds_list_size(lobbyUsers) && steam_get_user_steam_id() == ownerSteamId)
        {
        	lobbyStartingGame = true;
            pfo_create_listen_socket();
            steam_lobby_set_joinable(false);
            
            randomize();
            var seed = int64(irandom(0xffffffff)) | (int64(irandom(0xffffffff)) << int64(32));
            
            pfo_set_seed(seed);
            pfo_steam_lobby_set_game_server(steam_lobby_get_lobby_id(), 0, global.defaultLanguage, seed);
        }
    }
}

var param = async_load;
if (LOG.LEVEL >= LOG.VERBOSE) show_debug_message("Steam Async Event: " + json_encode(param));

var eventType = ds_map_find_value(param, "event_type");

if (eventType == "lobby_list")
{
	lobbyListCount = ds_map_find_value(param, "lobby_count");
	ds_list_clear(lobbyList);
	for (var i = 0; i < lobbyListCount; i++)
	{
		ds_list_add(lobbyList, steam_lobby_list_get_lobby_id(i));
	}

    if (!lobbyListSearchedFirstTime)
    {
        lobbyListSearchedFirstTime = true;
        arrowSel = lobbyListCount > 0 ? 1 : 0;
    }
}
else if (eventType == "lobby_created")
{
    creatingLobby = false;
    if (ds_map_find_value(param, "success"))
    {
        steam_lobby_set_data("ufo50_version", global.betaVersion);
        scrUpdateLobbyUsers(ds_map_find_value(param, "lobby_id"));
        scrSwitchSub(SUB_ONLINE_INIT_LOBBY);
    }
}
else if (eventType == "lobby_joined")
{
    joiningLobby = false;
    if (ds_map_find_value(param, "success"))
    {
        scrUpdateLobbyUsers(ds_map_find_value(param, "lobby_id"));
        scrSwitchSub(SUB_ONLINE_INIT_LOBBY);
    }
}
else if (eventType == "lobby_chat_update")
{
    scrUpdateLobbyUsers(ds_map_find_value(param, "lobby_id"));
}
else if (eventType == "lobby_data_update")
{
    var lobbyId = ds_map_find_value(param, "lobby_id");
    var memberId = ds_map_find_value(param, "member_id");

    if (lobbyId == steam_lobby_get_lobby_id() && memberId == steam_get_user_steam_id())
    {
    	settingReady = false;
    }

    if (ds_map_find_value(param, "success") && memberId != lobbyId)
    {
	    scrUpdateLobbyUsers(lobbyId);
    }
}
else if (eventType == "lobby_game_created")
{
	var ownerSteamId = steam_lobby_get_owner_id();
    if (steam_get_user_steam_id() != ownerSteamId)
    {
        var lobbyId = ds_map_find_value(param, "lobby_id");
        var seed = ds_map_find_value(param, "server_id");
        var defaultLanguage = ds_map_find_value(param, "port");
        if (!lobbyStartingGame && lobbyId == steam_lobby_get_lobby_id())
        {
        	lobbyStartingGame = true;
        	pfo_set_seed(seed);
            global.onlineDefaultLanguage = [ defaultLanguage, global.defaultLanguage ];
            pfo_connect(ownerSteamId);
        }
    }
}

enum LOG
{
    NONE = 0,
    INFO = 1,
    VERBOSE = 2,

    LEVEL = 2
}
