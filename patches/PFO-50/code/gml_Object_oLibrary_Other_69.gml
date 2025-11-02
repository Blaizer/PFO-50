function scrGetUserPersonaName(steamId)
{
    var name = steam_get_user_persona_name_sync(steamId);
    return (is_string(name) && name != "") ? name : global.EXTERNAL_TEXT_ERROR;
}

function scrUpdateLobbyUsers(lobbyId)
{
    if (lobbyId == steam_lobby_get_lobby_id())
    {
        ds_list_clear(lobbyUsers);

        var hasOnlineSettings = substate == SUB_ONLINE_LOBBY && stateCounter >= 2 && !is_undefined(global.onlineSettings);
        if (hasOnlineSettings)
        {
            var ids = global.onlineSettings.clientIds;
            for (var i = 0; i < array_length(ids); i++)
            {
                ds_list_add(lobbyUsers, ids[i]);
            }   
        }
        else
        {
            var count = steam_lobby_get_member_count();
            var ownerSteamId = steam_lobby_get_owner_id();

            for (var i = 0; i < count; i++)
            {
                var steamId = steam_lobby_get_member_id(i);
                if (steamId == ownerSteamId)
                {
                    ds_list_insert(lobbyUsers, 0, steamId);
                }
                else
                {
                    ds_list_add(lobbyUsers, steamId);
                }
            }
        }

        var readyCount = 0;
        var connectReadyCount = 0;
        for (var i = 0; i < ds_list_size(lobbyUsers); i++)
        {
            var steamId = ds_list_find_value(lobbyUsers, i);
            var personaName = scrGetUserPersonaName(steamId);
            var ready = pfo_steam_lobby_get_member_data(lobbyId, steamId, "ready");
            
            if (ready == "2")
            {
                ready = 2;
                readyCount++;
                connectReadyCount++;
            }
            else if (ready == "1" || hasOnlineSettings)
            {
                ready = 1;
                readyCount++;
            }
            else
            {
                ready = 0;
            }

            var data = { steamId: steamId, personaName: personaName, ready: ready };
            ds_list_set(lobbyUsers, i, data);
        }

        if (substate == SUB_ONLINE_LOBBY && stateCounter < 1 && readyCount > 1 && readyCount == ds_list_size(lobbyUsers) && steam_get_user_steam_id() == ownerSteamId)
        {
        	stateCounter = 1;
            steam_lobby_set_joinable(false);
            
            randomize();
            var startGameSettings =
            {
                seed: int64(irandom(0xffffffff)) | (int64(irandom(0xffffffff)) << int64(32)),
                lang: global.defaultLanguage,
                ids: array_create(ds_list_size(lobbyUsers), int64(0)),
            };

            for (var i = 0; i < ds_list_size(lobbyUsers); i++)
            {
                startGameSettings.ids[i] = ds_list_find_value(lobbyUsers, i).steamId;
            }

            steam_lobby_set_data("start_game_settings", json_stringify(startGameSettings));
        }
        else if (hasOnlineSettings && stateCounter < 3 && connectReadyCount == ds_list_size(lobbyUsers))
        {
            stateCounter = 3;
            pfo_connect();
        }
    }
}

var param = async_load;
if (LOG.LEVEL >= LOG.VERBOSE) show_debug_message("Steam Async Event: " + json_encode(param));

if (state != STATE_ONLINE)
{
    exit;
}

var eventType = ds_map_find_value(param, "event_type");
if (eventType == "lobby_list")
{
	lobbyListCount = ds_map_find_value(param, "lobby_count");
	ds_list_clear(lobbyList);
	for (var i = 0; i < lobbyListCount; i++)
	{
		ds_list_add(lobbyList, steam_lobby_list_get_lobby_id(i));
	}

    if (substate == SUB_ONLINE_MAIN && stateCounter == 0)
    {
        stateCounter = 1;
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

    if (lobbyId == steam_lobby_get_lobby_id())
    { 
        if (memberId == steam_get_user_steam_id() && substate == SUB_ONLINE_LOBBY && stateCounter == 0.5)
        {
        	stateCounter = 0;
        }

        if (ds_map_find_value(param, "success"))
        {
            if (memberId != lobbyId)
            {
        	    scrUpdateLobbyUsers(lobbyId);
            }
            else
            {
                if (substate == SUB_ONLINE_LOBBY && stateCounter < 2)
                {
                    var startGameSettings = steam_lobby_get_data("start_game_settings");
                    if (is_string(startGameSettings) && startGameSettings != "")
                    {
                        if (LOG.LEVEL >= LOG.VERBOSE) show_debug_message("start_game_settings: " + startGameSettings);

                        stateCounter = 2;
                        global.onlineSettings =
                        {
                            randomizeSeed: int64(0),
                            defaultLanguage: 0,
                            clientIds: [],
                            clientNames: [],
                        };

                        try
                        {
                            var settings = json_parse(startGameSettings);
                            global.onlineSettings.randomizeSeed = int64(settings.seed);
                            global.onlineSettings.defaultLanguage = real(settings.lang);
                            for (var i = 0; i < array_length(settings.ids); i++)
                            {
                                global.onlineSettings.clientIds[i] = int64(settings.ids[i]);
                                global.onlineSettings.clientNames[i] = scrGetUserPersonaName(global.onlineSettings.clientIds[i]);
                            }
                        }
                        catch (_exception)
                        {
                        }

                        pfo_reset();
                        pfo_set_clients(global.onlineSettings.clientIds);
                        scrUpdateLobbyUsers(lobbyId);

                        pfo_steam_lobby_set_member_data(lobbyId, "ready", "2");
                    }
                }
            }
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
