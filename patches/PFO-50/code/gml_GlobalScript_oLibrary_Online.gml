function scrHorzNav(sel, lastSel)
{
    if (pressLeft)
    {
        sel--;
    }
    if (pressRight)
    {
        sel++;
    }
    
    if (sel > lastSel)
    {
        sel = 0;
    }
    else if (sel < 0)
    {
        sel = lastSel;
    }
    
    return sel;
}

function scrCheckRequestTimeout(message)
{
    if (requestTimeoutTime <= current_time)
    {
        errorMessage = message;
        scrSwitchSub(SUB_ONLINE_ERROR);
        return true;
    }
    return false;
}

function scrGetUserPersonaName(steamId)
{
    var name = steam_get_user_persona_name_sync(steamId);
    return (is_string(name) && name != "") ? name : global.EXTERNAL_TEXT_ERROR;
}

function scrGetLobbyData(lobbyId)
{
    var memberCount = pfo_steam_get_num_lobby_members(lobbyId);
    var memberLimit = pfo_steam_get_lobby_member_limit(lobbyId);
    var name = pfo_steam_get_lobby_data(lobbyId, "name");
    var ufoVersion = pfo_steam_get_lobby_data(lobbyId, "ufoVersion");
    var pfoVersion = pfo_steam_get_lobby_data(lobbyId, "pfoVersion");
    return { lobbyId: lobbyId, memberCount: memberCount, memberLimit: memberLimit, name: name, ufoVersion: ufoVersion, pfoVersion: pfoVersion };
}

function scrRequestJoinLobby(data)
{
    if (!is_string(data.name) || data.name == "" ||
        !is_string(data.ufoVersion) || data.ufoVersion == "" ||
        !is_string(data.pfoVersion) || data.pfoVersion == "")
    {
        errorMessage = "CAN'T JOIN LOBBY. LOBBY IS INVALID.";
        scrSwitchSub(SUB_ONLINE_ERROR);
    }
    else if (data.ufoVersion != global.ufoVersion || data.pfoVersion != global.pfoVersion)
    {
        errorMessage = [ "CAN'T JOIN LOBBY. GAME VERSIONS MUST MATCH.", { ufoVersion: global.ufoVersion, pfoVersion: global.pfoVersion }, data ];
        scrSwitchSub(SUB_ONLINE_VERSION_ERROR);
    }
    else if (data.memberCount >= data.memberLimit)
    {
        errorMessage = "CAN'T JOIN LOBBY. LOBBY IS FULL.";
        scrSwitchSub(SUB_ONLINE_ERROR);
    }
    else
    {
        requestTimeoutTime = current_time + ONLINE_REQUEST_TIMEOUT;
        steam_lobby_join_id(data.lobbyId);
        scrSwitchSub(SUB_ONLINE_JOINING_LOBBY);
    }
}

function scrUpdateLobbyData(lobbyId)
{
    var data = scrGetLobbyData(lobbyId);
    if (!is_string(data.name) || data.name == "" || data.memberLimit <= 0)
    {
        errorMessage = "LOBBY IS INVALID OR NO LONGER EXISTS.";
        scrSwitchSub(SUB_ONLINE_ERROR);
        return false;
    }

    lobbyData = data;
    return true;
}

function scrUpdateLobbyUsers(lobbyId)
{
    if (lobbyId == steam_lobby_get_lobby_id())
    {
        var newIsOwner = steam_lobby_is_owner();
        if (!isOwner && newIsOwner)
        {
            var personaName = scrGetUserPersonaName(steam_get_user_steam_id());
            steam_lobby_set_data("name", string_upper(personaName) + "'S LOBBY");
        }
        isOwner = newIsOwner;

        ds_list_clear(lobbyUsers);

        var hasOnlineSettings = substate >= SUB_ONLINE_SET_STARTING_GAME && !is_undefined(global.onlineSettings);
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

        readyState = 0;
        var mySteamId = steam_get_user_steam_id();
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

            if (steamId == mySteamId)
            {
                readyState = ready;
            }

            var data = { steamId: steamId, personaName: personaName, ready: ready };
            ds_list_set(lobbyUsers, i, data);
        }

        if (isOwner && substate < SUB_ONLINE_SET_START_GAME_SETTINGS && readyCount > 1 && readyCount == ds_list_size(lobbyUsers))
        {
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

            requestTimeoutTime = current_time + ONLINE_REQUEST_TIMEOUT;
            var startGameSettingsJson = json_stringify(startGameSettings);
            steam_lobby_set_data("start_game_settings", startGameSettingsJson);
            scrSwitchSub(SUB_ONLINE_SET_START_GAME_SETTINGS);
        }
        else if (hasOnlineSettings && substate < SUB_ONLINE_CONNECTING && connectReadyCount == ds_list_size(lobbyUsers))
        {
            requestTimeoutTime = current_time + ONLINE_CONNECT_TIMEOUT;
            pfo_connect();
            scrSwitchSub(SUB_ONLINE_CONNECTING);
        }
    }
}

function scrDrawOnlineMenuSelection(text, enabled)
{
    var selected = sel == sel2 && subsel == 1;
    var buttonLeft = 292;
    var buttonWidth = 66;
    var buttonHeight = 57;

    scrSetFont(global.fontTall);
    scrDrawMenuBorder(buttonLeft, selY, buttonWidth, buttonHeight);
   
    if (selected)
    {
        draw_set_color(global.palette[3]);
    }
    
    var buttonTextY = selY + 5;
    var textOffsetY = argument_count > 2 ? argument[2] : 0;

    draw_text_centered(buttonLeft + (buttonWidth / 2), buttonTextY + textOffsetY,      text[0], 8);
    draw_text_centered(buttonLeft + (buttonWidth / 2), buttonTextY + textOffsetY + 16, text[1], 8);
    draw_text_centered(buttonLeft + (buttonWidth / 2), buttonTextY + textOffsetY + 32, text[2], 8);
    
    if (selected)
    {
        draw_sprite(sMenuHand, 0, buttonLeft - 18, buttonTextY + 17);
    }
    
    scrSetFont(global.fontDefault);
    draw_set_color(c_white);

    selY += 64;
    sel++;
}

function scrDrawOnlineMenuBack()
{
    var backY = 180;
    var backX = 340;
    var selected = sel2 == 2 && subsel == 1;
    draw_text_centered(backX, backY, "BACK", 8);
    if (selected)
    {
        draw_sprite(sMenuHand, 0, backX - 42, backY - 3);
    }
}
