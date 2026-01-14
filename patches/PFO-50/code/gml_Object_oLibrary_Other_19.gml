#macro steam_lobby_type_private 0
#macro steam_lobby_type_friends_only 1
#macro steam_lobby_type_public 2
#macro steam_lobby_list_filter_eq 0
#macro steam_lobby_list_filter_ne 3
#macro steam_lobby_list_filter_lt -1
#macro steam_lobby_list_filter_gt 1
#macro steam_lobby_list_filter_le -2
#macro steam_lobby_list_filter_ge 2
#macro steam_lobby_list_distance_filter_close 0
#macro steam_lobby_list_distance_filter_default 1
#macro steam_lobby_list_distance_filter_far 2
#macro steam_lobby_list_distance_filter_worldwide 3

function scrRequestLobbyList()
{
    lobbyListNextSearchTime = current_time + 1000;
    steam_lobby_list_add_string_filter("ufoVersion", "", steam_lobby_list_filter_gt);
    steam_lobby_list_add_string_filter("pfoVersion", "", steam_lobby_list_filter_gt);
    steam_lobby_list_add_string_filter("name", "", steam_lobby_list_filter_gt);
    steam_lobby_list_request();
}

function scrRequestCreateLobby(type)
{
    requestTimeoutTime = current_time + ONLINE_REQUEST_TIMEOUT;
    steam_lobby_create(type, 8);
    scrSwitchSub(SUB_ONLINE_CREATING_LOBBY);
}

function scrListFindLobbyIndex(id, lobbyId)
{
    var size = ds_list_size(id);
    for (var i = 0; i < size; i++)
    {
        var elem = ds_list_find_value(id, i);
        if (elem.lobbyId == lobbyId)
        {
            return i;
        }
    }
    return -1;
}

function scrUpdateLobbySel()
{
    if (sel1 < ds_list_size(lobbyList))
    {
        selLobby = ds_list_find_value(lobbyList, sel1).lobbyId;
    }
}

if (substate == SUB_ONLINE_INIT)
{
    steam_lobby_leave();
    global.drawLibraryBG = false;

    sel1 = 0;
    sel2 = 0;
    subsel = 0;
    selLobby = int64(0);

    errorMessage = undefined;
    requestTimeoutTime = NaN;
    lobbyListNextSearchTime = -1;
    selectionAcceptLockoutTime = NaN;
    ds_list_clear(lobbyList);

    if (!steam_is_user_logged_on())
    {
        errorMessage = "MUST BE LOGGED ON TO STEAM TO PLAY ONLINE.";
        scrSwitchSub(SUB_ONLINE_ERROR);
        exit;
    }

    if (automaticallyJoinLobby != int64(0))
    {
        requestTimeoutTime = current_time + ONLINE_REQUEST_TIMEOUT;
        pfo_steam_request_lobby_data(automaticallyJoinLobby);
        scrSwitchSub(SUB_ONLINE_AUTOMATICALLY_JOINING_LOBBY);
        exit;
    }

    requestTimeoutTime = current_time + ONLINE_REQUEST_TIMEOUT;
    scrRequestLobbyList();
    scrSwitchSub(SUB_ONLINE_INITIAL_SEARCH);
}
else if (substate == SUB_ONLINE_INITIAL_SEARCH)
{
    scrCheckRequestTimeout("CONNECTION TO STEAM TIMED OUT.");
}
else if (substate == SUB_ONLINE_LIST)
{
    if (lobbyListNextSearchTime <= current_time && !steam_lobby_list_is_loading())
    {
        scrRequestLobbyList();
    }

    var oldSelLobby = selLobby;
    var oldSubsel = subsel;

    if (selLobby != int64(0))
    {
        var index = scrListFindLobbyIndex(lobbyList, selLobby);
        if (index != -1)
        {
            sel1 = index;
        }
    }

    var lobbyListCount = ds_list_size(lobbyList);

    if (lobbyListCount == 0)
    {
        sel1 = 0;
        subsel = 1;
    }
    else if (sel1 >= lobbyListCount)
    {
        sel1 = ds_list_size(lobbyList) - 1;
    }

    scrUpdateLobbySel();

    if (selLobby != oldSelLobby || subsel != oldSubsel)
    {
        selectionAcceptLockoutTime = current_time + 250;
    }

    if (subsel == 0 || lobbyListCount != 0)
    {
        subsel = scrHorzNav(subsel, 1);
    }

    if (subsel == 0)
    {
        sel1 = scrVertNav(sel1, lobbyListCount - 1);
    }
    else if (subsel == 1)
    {
        sel2 = scrVertNav(sel2, 2);
    }

    scrUpdateLobbySel();

    if (selectionAcceptLockoutTime > current_time)
    {
        fire2pressed = false;
    }

    if ((fire2pressed && subsel == 1 && sel2 == 2) || fire1pressed)
    {
        scrSwitchState(STATE_PROFILE);
    }
    else if (fire2pressed && subsel == 0)
    {
        var data = ds_list_find_value(lobbyList, sel1);
        scrRequestJoinLobby(data);
    }
    else if (fire2pressed && subsel == 1)
    {
        if (sel2 == 0)
        {
            scrRequestCreateLobby(steam_lobby_type_public);
        }
        else if (sel2 == 1)
        {
            scrRequestCreateLobby(steam_lobby_type_friends_only);
        }
    }
}
else if (substate == SUB_ONLINE_CREATING_LOBBY)
{
    scrCheckRequestTimeout("TIMED OUT ATTEMPTING TO CREATE LOBBY.");
}
else if (substate == SUB_ONLINE_JOINING_LOBBY)
{
    scrCheckRequestTimeout("TIMED OUT ATTEMPTING TO JOIN LOBBY.");
}
else if (substate == SUB_ONLINE_AUTOMATICALLY_JOINING_LOBBY)
{
    if (scrCheckRequestTimeout("TIMED OUT ATTEMPTING TO FIND LOBBY."))
    {
        automaticallyJoinLobby = int64(0);
    }
}
else if (substate == SUB_ONLINE_ERROR)
{
    var _okay = scrGetOkay(errorMessage);
    if (_okay == true)
    {
        errorMessage = undefined;
        scrSwitchState(STATE_PROFILE);
    }
}
else if (substate == SUB_ONLINE_VERSION_ERROR)
{
    var _okay = scrGetOkay(errorMessage);
    if (_okay == true)
    {
        errorMessage = undefined;
        scrSwitchSub(SUB_ONLINE_LIST);
    }
}

function scrLibraryOnlineStateDraw()
{
    var onlineXcenteroff = 0;

    scrDrawMenuBorder(96 + onlineXcenteroff, 8, 192, 24);
    draw_set_color(c_white);
    scrSetFont(global.fontDefault);
    draw_text_centered(192 + onlineXcenteroff, 16, "CREATE OR JOIN LOBBY", 8);
    
    sel = 0;
    selY = 44;
    scrDrawOnlineMenuSelection([ "CREATE", "PUBLIC", "LOBBY" ], true);
    scrDrawOnlineMenuSelection([ "CREATE", "PRIVATE", "LOBBY" ], true);

    scrDrawOnlineMenuBack();

    var lobbyListCount = ds_list_size(lobbyList);

    if (lobbyListCount == 0 && (substate == SUB_ONLINE_LIST || substate == SUB_ONLINE_CREATING_LOBBY))
    {
        scrSetFont(global.fontDefault);
        draw_text_centered(192 + onlineXcenteroff - 40, 48 + 0, string_even("NO LOBBIES FOUND.", 1), 8);
        draw_text_centered(192 + onlineXcenteroff - 40, 48 + 16, string_even("MAYBE CREATE ONE?", 1), 8);
    }
    else
    {
        for (var i = 0; i < lobbyListCount; i++)
        {
            var data = ds_list_find_value(lobbyList, i);
            var listX = 28;
            var handX = listX - 14;
            var listY = 44 + i * 32;
            var listWidth = 240;
            var selected = sel1 == i && subsel == 0;

            if (selected)
            {
                listX += 4;
            }

            scrDrawMenuBorder(listX, listY, listWidth, 32);
            scrSetFont(global.fontTall);
            var _textTop = listY + 8;

            if (data.ufoVersion != global.ufoVersion || data.pfoVersion != global.pfoVersion)
            {
                draw_set_color(global.palette[16]);
            }

            var lobbyName = string_copy(data.name, 1, 20);
            draw_text(listX + 16, _textTop, lobbyName);

            scrSetFont(global.fontDefault);
            draw_text(listX + listWidth - 48, _textTop + 0, "P " + string(data.memberCount) + "/" + string(data.memberLimit));
            draw_text(listX + listWidth - 48, _textTop + 8, data.pfoVersion);

            if (selected)
            {
                draw_sprite(sMenuHand, 0, handX, listY + 9);
            }

            draw_set_color(c_white);
        }
    }
}
