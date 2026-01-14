if (state == STATE_ONLINE_LOBBY_LIST)
{
    var param = async_load;
    var eventType = ds_map_find_value(param, "event_type");

    if (eventType == "lobby_list")
    {
        if (substate > SUB_ONLINE_INIT)
        {
            var lobbyListCount = ds_map_find_value(param, "lobby_count");
            ds_list_clear(lobbyList);
            for (var i = 0; i < lobbyListCount; i++)
            {
                var lobbyId = steam_lobby_list_get_lobby_id(i);
                ds_list_add(lobbyList, scrGetLobbyData(lobbyId));
            }

            if (substate == SUB_ONLINE_INITIAL_SEARCH)
            {
                if (ds_list_size(lobbyList) > 0)
                {
                    selLobby = ds_list_find_value(lobbyList, 0).lobbyId;
                }
                else
                {
                    subsel = 1;
                }
                scrSwitchSub(SUB_ONLINE_LIST);
            }
        }
    }
    else if (eventType == "lobby_created")
    {
        if (substate == SUB_ONLINE_CREATING_LOBBY)
        {
            if (ds_map_find_value(param, "success"))
            {
                var lobbyId = ds_map_find_value(param, "lobby_id");
                if (lobbyId == steam_lobby_get_lobby_id())
                {
                    steam_lobby_set_data("ufoVersion", global.ufoVersion);
                    steam_lobby_set_data("pfoVersion", global.pfoVersion);

                    scrUpdateLobbyData(lobbyId);
                    scrSwitchState(STATE_ONLINE_LOBBY);
                    scrRunStateMachine();
                }
            }
            else
            {
                errorMessage = "FAILED TO CREATE LOBBY.";
                scrSwitchSub(SUB_ONLINE_ERROR);
            }
        }
        else
        {
            steam_lobby_leave();
        }
    }
    else if (eventType == "lobby_joined")
    {
        if (substate == SUB_ONLINE_JOINING_LOBBY)
        {
            if (ds_map_find_value(param, "success"))
            {
                var lobbyId = ds_map_find_value(param, "lobby_id");
                if (lobbyId == steam_lobby_get_lobby_id())
                {
                    scrUpdateLobbyData(lobbyId);
                    scrSwitchState(STATE_ONLINE_LOBBY);
                    scrRunStateMachine();
                }
            }
            else
            {
                errorMessage = "FAILED TO JOIN LOBBY.";
                scrSwitchSub(SUB_ONLINE_ERROR);
            }
        }
        else
        {
            steam_lobby_leave();
        }
    }
    else if (eventType == "lobby_data_update")
    {
        if (substate == SUB_ONLINE_AUTOMATICALLY_JOINING_LOBBY)
        {
            var lobbyId = ds_map_find_value(param, "lobby_id");
            var memberId = ds_map_find_value(param, "member_id");

            if (lobbyId == automaticallyJoinLobby && lobbyId == memberId)
            {
                automaticallyJoinLobby = int64(0);
                if (ds_map_find_value(param, "success"))
                {
                    scrRequestJoinLobby(scrGetLobbyData(lobbyId));
                }
                else
                {
                    errorMessage = "LOBBY NO LONGER EXISTS.";
                    scrSwitchSub(SUB_ONLINE_ERROR);
                }
            }
        }
    }
}
else if (state == STATE_ONLINE_LOBBY)
{
    var param = async_load;
    var eventType = ds_map_find_value(param, "event_type");

    if (eventType == "lobby_chat_update")
    {
        if (substate > SUB_ONLINE_INIT)
        {
            scrUpdateLobbyUsers(ds_map_find_value(param, "lobby_id"));
        }
    }
    else if (eventType == "lobby_data_update")
    {
        var lobbyId = ds_map_find_value(param, "lobby_id");
        var memberId = ds_map_find_value(param, "member_id");

        if (lobbyId == steam_lobby_get_lobby_id())
        {
            if (substate > SUB_ONLINE_INIT && memberId != lobbyId)
            {
                if (ds_map_find_value(param, "success"))
                {
                    scrUpdateLobbyUsers(lobbyId);
                }
            }

            if (substate > SUB_ONLINE_INIT && memberId == lobbyId)
            {
                if (ds_map_find_value(param, "success"))
                {
                    if (scrUpdateLobbyData(lobbyId))
                    {
                        if (substate < SUB_ONLINE_SET_STARTING_GAME)
                        {
                            var startGameSettings = steam_lobby_get_data("start_game_settings");
                            if (is_string(startGameSettings) && startGameSettings != "")
                            {
                                LOG_INFO("### STARTING SESSION ###");
                                LOG_DEBUG("start_game_settings: " + startGameSettings);

                                try
                                {
                                    var settings = json_parse(startGameSettings);
                                    global.onlineSettings =
                                    {
                                        randomizeSeed: int64(settings.seed),
                                        defaultLanguage: real(settings.lang),
                                        clientIds: [],
                                        clientNames: [],
                                    };
                                    for (var i = 0; i < array_length(settings.ids); i++)
                                    {
                                        global.onlineSettings.clientIds[i] = int64(settings.ids[i]);
                                        global.onlineSettings.clientNames[i] = scrGetUserPersonaName(global.onlineSettings.clientIds[i]);
                                    }
                                }
                                catch (_exception)
                                {
                                    errorMessage = "AN ERROR OCCURED WHILE STARTING GAME.";
                                    scrSwitchSub(SUB_ONLINE_ERROR);
                                    exit;
                                }

                                if (array_length(global.onlineSettings.clientIds) < 2 || global.onlineSettings.defaultLanguage < 0 || global.onlineSettings.defaultLanguage >= global.NUM_LANG)
                                {
                                    errorMessage = "ERROR ENCOUNTERED WHILE STARTING GAME.";
                                    scrSwitchSub(SUB_ONLINE_ERROR);
                                    exit;
                                }

                                pfo_reset();
                                if (!pfo_set_clients(global.onlineSettings.clientIds))
                                {
                                    errorMessage = "THIS LOBBY HAS ALREADY STARTED.";
                                    scrSwitchSub(SUB_ONLINE_ERROR);
                                    exit;
                                }

                                for (var i = 0; i < ds_list_size(lobbyUsers); i++)
                                {
                                    var data = ds_list_find_value(lobbyUsers, i);
                                    LOG_INFO("Client " + string(i) + ": " + string({ hash: data.hash }));
                                }

                                requestTimeoutTime = current_time + ONLINE_REQUEST_TIMEOUT;
                                pfo_steam_lobby_set_member_data(lobbyId, "ready", "2");

                                scrSwitchSub(SUB_ONLINE_SET_STARTING_GAME);
                                scrUpdateLobbyUsers(lobbyId);

                                LOG_INFO("########################");
                            }
                        }
                    }
                }
                else if (isOwner)
                {
                    errorMessage = "NO LONGER THE OWNER OF THIS LOBBY.";
                    scrSwitchSub(SUB_ONLINE_ERROR);
                }
            }
        }
    }
}
