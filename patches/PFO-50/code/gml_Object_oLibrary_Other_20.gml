if (substate == SUB_ONLINE_INIT)
{
    sel2 = 0;
    subsel = 1;
    isOwner = false;
    readyState = false;

    errorMessage = undefined;
    requestTimeoutTime = NaN;
    global.onlineSettings = undefined;
    ds_list_clear(lobbyUsers);

    scrUpdateLobbyUsers(steam_lobby_get_lobby_id());

    scrSwitchSub(SUB_ONLINE_LOBBY);
}
else if (substate == SUB_ONLINE_LOBBY)
{
    sel2 = scrVertNav(sel2, 2);

    if (fire2pressed && sel2 == 0)
    {
        requestTimeoutTime = current_time + ONLINE_REQUEST_TIMEOUT;
        pfo_steam_lobby_set_member_data(steam_lobby_get_lobby_id(), "ready", readyState ? "0" : "1");
    }
    else if (fire2pressed && sel2 == 1)
    {
        steam_lobby_activate_invite_overlay();
        scrInputClear();
    }
    else if ((fire2pressed && sel2 == 2) || fire1pressed)
    {
        scrSwitchState(STATE_ONLINE_LOBBY_LIST);
    }
}
else if (substate == SUB_ONLINE_SET_START_GAME_SETTINGS)
{
    scrCheckRequestTimeout("CONNECTION TO LOBBY WAS LOST.");
}
else if (substate == SUB_ONLINE_SET_STARTING_GAME)
{
    scrCheckRequestTimeout("LOST CONNECTION TO LOBBY.");
}
else if (substate == SUB_ONLINE_CONNECTING)
{
    scrCheckRequestTimeout("FAILED TO CONNECT TO ALL PLAYERS.");
}
else if (substate == SUB_ONLINE_ERROR)
{
    steam_lobby_leave();

    var _okay = scrGetOkay(errorMessage);
    if (_okay == true)
    {
        errorMessage = undefined;
        scrSwitchState(STATE_PROFILE);
    }
}

function scrLibraryOnlineLobbyDraw()
{
    var onlineXcenteroff = 0;

    scrDrawMenuBorder(96 + onlineXcenteroff, 8, 192, 24);
    draw_set_color(c_white);
    scrSetFont(global.fontDefault);
    var lobbyName = string_copy(lobbyData.name, 1, 20);
    draw_text_centered(192 + onlineXcenteroff, 16, lobbyName, 8);

    for (var player = 0; player < max(8, ds_list_size(lobbyUsers)); player++)
    {
        var text = "PLAYER " + string(player + 1) + ": ";
        var readyText = "";

        var color;
        if (ds_list_size(lobbyUsers) > player)
        {
            var user = ds_list_find_value(lobbyUsers, player);
            text += string_copy(user.personaName, 1, 20);

            if (user.ready)
            {
                readyText = "[READY] ";
                color = 0;
            }
            else
            {
                color = 8;
            }
        }
        else
        {
            text += "<EMPTY>";
            color = 16;
        }
        
        draw_set_color(global.palette[color]);
        draw_text(72, 50 + player * 16, text);
        draw_text(72 - string_length(readyText) * 8, 50 + player * 16, readyText);
    }
    
    sel = 0;
    selY = 44;
    if (readyState)
    {
        scrDrawOnlineMenuSelection([ "", "UNREADY", "" ], true);
    }
    else
    {
        scrDrawOnlineMenuSelection([ "", "READY", "" ], true);
    }
    scrDrawOnlineMenuSelection([ "INVITE", "FRIENDS", "" ], true, 8);

    scrDrawOnlineMenuBack();
}
