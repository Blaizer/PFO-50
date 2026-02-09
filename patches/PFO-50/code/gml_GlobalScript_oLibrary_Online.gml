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
    lobbyData = scrGetLobbyData(lobbyId);

    if (!is_string(lobbyData.name) || lobbyData.name == "" || lobbyData.memberLimit <= 0)
    {
        errorMessage = "LOBBY IS INVALID OR NO LONGER EXISTS.";
        scrSwitchSub(SUB_ONLINE_ERROR);
        return false;
    }

    return true;
}

function scrUpdateLobbyUsers(lobbyId)
{
    if (lobbyId == steam_lobby_get_lobby_id())
    {
        var newIsOwner = steam_lobby_is_owner();
        var mySteamId = steam_get_user_steam_id();
        if (!isOwner && newIsOwner)
        {
            var personaName = scrGetUserPersonaName(mySteamId);
            steam_lobby_set_data("name", string_upper(personaName) + "'S LOBBY");
        }
        isOwner = newIsOwner;

        var compat = pfo_steam_lobby_get_member_data(lobbyId, mySteamId, "compat");
        if (!is_string(compat) || compat == "")
        {
            pfo_steam_lobby_set_member_data(lobbyId, "compat", json_stringify(global.onlineCompatibilityInfo));
        }

        ds_list_clear(lobbyUsers);

        var hasOnlineSettings = !is_undefined(global.onlineSettings);
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

        myLobbyIndex = -1;
        var readyCount = 0;
        var connectReadyCount = 0;
        for (var i = 0; i < ds_list_size(lobbyUsers); i++)
        {
            var steamId = ds_list_find_value(lobbyUsers, i);
            var personaName = scrGetUserPersonaName(steamId);
            var ready = pfo_steam_lobby_get_member_data(lobbyId, steamId, "ready");
            var compatString = pfo_steam_lobby_get_member_data(lobbyId, steamId, "compat");
            
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
                myLobbyIndex = i;
            }

            var compat = undefined;
            if (is_string(compatString) && compatString != "")
            {
                compat = scrCreateCompatibilityInfoFromString(compatString);
            }

            var data = { steamId: steamId, personaName: personaName, ready: ready, compat: compat };
            ds_list_set(lobbyUsers, i, data);
        }

        if (myLobbyIndex < 0)
        {
            if (substate == SUB_ONLINE_INIT)
            {
                errorMessage = "FAILED TO JOIN. LOBBY HAS ALREADY STARTED.";
                ds_list_clear(lobbyUsers);
                scrSwitchSub(SUB_ONLINE_ERROR);
            }
            else if (hasOnlineSettings)
            {
                errorMessage = "THIS LOBBY HAS ALREADY STARTED.";
                scrSwitchSub(SUB_ONLINE_ERROR);
            }
            else
            {
                errorMessage = "NO LONGER A MEMBER OF THIS LOBBY.";
                scrSwitchSub(SUB_ONLINE_ERROR);
            }

            return false;
        }

        if (isOwner && substate < SUB_ONLINE_SET_START_GAME_SETTINGS && readyCount > 1 && readyCount == ds_list_size(lobbyUsers))
        {
            steam_lobby_set_joinable(false);
            
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

    return true;
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
    var backY = 182;
    var backX = 340;
    var selected = sel2 == 2 && subsel == 1;
    draw_text_centered(backX, backY, "BACK", 8);
    if (selected)
    {
        draw_sprite(sMenuHand, 0, backX - 42, backY - 3);
    }
}

function scrGetCompatibilityDiffList(ours, theirs)
{
    var comparers = [ [], [] ];
    for (var n = 0; n < 2; n++)
    {
        for (var i = 0; i < array_length(argument[n].mods); i += 3)
        {
            // for string comparison we use a key that sorts first case-insensitively, then case sensitively
            var cmp = string_lower(argument[n].mods[i]) + argument[n].mods[i];
            array_push(comparers[n], { 
                name: argument[n].mods[i],
                version: argument[n].mods[i + 1],
                hash: argument[n].mods[i + 2],
                cmp: cmp
            });
        }

        array_sort(comparers[n], function (a, b)
        {
            // sort PFO 50 first in the list of mods
            var pfoCmp = (b.name == "PFO 50") - (a.name == "PFO 50");
            if (pfoCmp != 0)
            {
                return pfoCmp;
            }

            if (a.cmp < b.cmp)
            {
                return -1;
            }
            if (a.cmp > b.cmp)
            {
                return 1;
            }
            return 0;
        });
    }

    var modLoaderStatus = argument[0].version == argument[1].version ? 0 : (argument[0].version == "" || argument[1].version == "" ? 1 : 2);
    var modLoaderDiff = { name: "UFO 50 Mod Loader", status: modLoaderStatus, versions: [] };

    for (var n = 0; n < 2; n++)
    {
        modLoaderDiff.versions[n] = argument[n].version == "" ? "UNKNOWN" : argument[n].version;
    }

    var diffs = [ modLoaderDiff ];

    var indexes = [ 0, 0 ];
    while (indexes[0] < array_length(comparers[0]) || indexes[1] < array_length(comparers[1]))
    {
        var next;
        var both = false;
        var otherValue;

        if (indexes[0] >= array_length(comparers[0]))
        {
            next = 1;
        }
        else if (indexes[1] >= array_length(comparers[1]))
        {
            next = 0;
        }
        else if (comparers[0][indexes[0]].cmp < comparers[1][indexes[1]].cmp)
        {
            next = 0;
        }
        else if (comparers[0][indexes[0]].cmp > comparers[1][indexes[1]].cmp)
        {
            next = 1;
        }
        else
        {
            next = 0;
            both = true;
            otherValue = comparers[1][indexes[1]]; 
            indexes[1]++;
        }

        var nextValue = comparers[next][indexes[next]];
        indexes[next]++;

        var status = both ? (nextValue.hash == otherValue.hash ? 0 : 1) : 1;
        var diff = { name: nextValue.name, status: status, versions: [] };

        for (var n = 0; n < 2; n++)
        {
            if (n == 1 && both)
            {
                next = 1;
                nextValue = otherValue;
            }

            diff.versions[n] = next == n ? (nextValue.version == "" ? "VERSION UNKNOWN" : nextValue.version) : "";
        }

        array_push(diffs, diff);
    }

    return diffs;
}
