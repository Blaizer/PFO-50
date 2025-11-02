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

if (substate == SUB_ONLINE_INIT)
{
    creatingLobby = false;
    joiningLobby = false;
    lobbyListCount = 0;
    lobbyListNextSearchTime = -1;
    ds_list_clear(lobbyList);
    ds_list_clear(lobbyUsers);

    arrowSel = 0;
    arrowBlink = -1;
    arrowBlinkMax = 26;

    scrSwitchSub(SUB_ONLINE_MAIN);
}

function scrOnlineSelect(active, lastSel)
{
    if (active)
    {
        if (++arrowBlink >= arrowBlinkMax)
        {
            arrowBlink = -arrowBlinkMax;
        }

        var oldArrowSel = arrowSel;
        arrowSel = scrVertNav(arrowSel, lastSel);
        if (arrowSel != oldArrowSel) arrowBlink = 0;

        if (fire2pressed)
        {
            return arrowSel;
        }
        else
        {
            return -1;
        }
    }
    else
    {
        arrowBlink = -1;
        return -2;
    }
}

if (substate == SUB_ONLINE_MAIN)
{
    if (lobbyListNextSearchTime <= current_time && !steam_lobby_list_is_loading())
    {
        lobbyListNextSearchTime = current_time + 750;
        steam_lobby_list_add_string_filter("ufo50_version", global.betaVersion, steam_lobby_list_filter_eq);
        steam_lobby_list_request();
    }

    var action = scrOnlineSelect(stateCounter >= 1 && !creatingLobby && !joiningLobby, 2);
    if (action == -2)
    {
    }
    else if (action == 0)
    {
        creatingLobby = true;
        steam_lobby_create(steam_lobby_type_public, 8);
    }
    else if (action == 1 && lobbyListCount > 0)
    {
        joiningLobby = true;
        steam_lobby_join_id(ds_list_find_value(lobbyList, 0));
    }
    else if (action == 2 || fire1pressed)
    {
        scrSwitchState(STATE_PROFILE);
    }
}

if (substate == SUB_ONLINE_INIT_LOBBY)
{
    global.onlineSettings = undefined;
    readyState = false;
    arrowSel = 0;
    arrowBlink = -1;

    scrSwitchSub(SUB_ONLINE_LOBBY);
}

if (substate == SUB_ONLINE_LOBBY)
{
    var mySteamId = steam_get_user_steam_id();
    var lobbyId = steam_lobby_get_lobby_id();

    readyState = false;
    for (var i = 0; i < ds_list_size(lobbyUsers); i++)
    {
        var user = ds_list_find_value(lobbyUsers, i);
        if (user.steamId == mySteamId)
        {
            readyState = user.ready;
            break;
        }
    }

    var action = scrOnlineSelect(stateCounter == 0, 2);
    if (action == 0 && !readyState)
    {
        stateCounter = 0.5;
        pfo_steam_lobby_set_member_data(lobbyId, "ready", "1");
    }
    else if (action == 1 && readyState)
    {
        pfo_steam_lobby_set_member_data(lobbyId, "ready", "");
    }
    else if (action == 2 || fire1pressed)
    {
        steam_lobby_leave();
        scrSwitchSub(SUB_ONLINE_INIT);
    }
}

if (substate == SUB_ONLINE_CONNECT)
{
    if (stateCounter == 1)
    {
        var allFilesShared = pfo_file_exists(global.ACCOUNT_FILE) >= 0
            && pfo_file_exists(string_replace(global.SAVE_FILE, "*", "1")) >= 0 
            && pfo_file_exists(string_replace(global.SAVE_FILE, "*", "2")) >= 0 
            && pfo_file_exists(string_replace(global.SAVE_FILE, "*", "3")) >= 0;

        if (!allFilesShared)
        {
            global.onlineRunUpdate = false;
            exit;
        }
    }
    else if (stateCounter == 2)
    {
        pfo_set_players([0, 1]);
        scrInitAch();
    }
    else if (stateCounter == 3)
    {
        scrSwitchState(STATE_PROFILE);
    }

    stateCounter++;
}

function draw_text_bg_centered_2(arg0, arg1, arg2, arg3, arg4, arg5, arg6)
{
    var numChars = string_length(arg2);
    var oldColor = draw_get_color();
    var pixelWidth = numChars * arg4;
    var startX = arg0 - floor(pixelWidth / 2);
    
    for (var q = 1; q <= numChars; q++)
    {
        var c = string_char_at(arg2, q);
        
        if (c != " " || !arg6)
        {
            draw_set_color(arg3);
            draw_rectangle((startX + ((q - 1) * arg4)) - 2, arg1 - 2, ((startX + (q * arg4)) - 1) + 1, (arg1 + arg5) - 1, false);
        }
    }
    
    draw_set_color(oldColor);
    draw_text(startX, arg1, arg2);
}

function scrDrawOnlineMenuSelection(text, enabled)
{
    var color;
    var selected = sel == arrowSel;
    if (enabled)
    {
        color = selected ? 12 : 11;
    }
    else
    {
        color = 28;
    }

    var selX = 104;
    draw_set_color(global.palette[color]);
    draw_text(selX, selY, text);

    if (selected && arrowBlink >= 0)
    {
        draw_sprite_ext(sLittleArrow, 0, selX - 16, selY, 1, 1, 0, draw_get_color(), 1);
    }

    selY += 16;
    sel++;
}

function scrLibraryOnlineStateDraw()
{
    scrFillScreen(0);
    scrSetFont(global.fontDefaultNoShadow);
    draw_set_color(c_black);
    
    if (substate == SUB_ONLINE_MAIN)
    {
        draw_text_bg_centered_2(192, 48, "CREATE OR JOIN A LOBBY", global.palette[11], 8, 8, false);

        var joinLobbyText = "";
        if (stateCounter >= 1)
        {
            joinLobbyText = "(" + (lobbyListCount > 0 ? string(lobbyListCount) + " OPEN" : "NONE FOUND") + ")";
        }

        sel = 0;
        selY = 88;
        scrDrawOnlineMenuSelection("CREATE LOBBY", true);
        scrDrawOnlineMenuSelection("JOIN LOBBY " + joinLobbyText, lobbyListCount > 0);
        scrDrawOnlineMenuSelection("BACK TO PROFILE SELECT", true);

        draw_set_color(global.palette[11]);
        scrDrawTextInput(192, 160, "[2] CONFIRM       [1] BACK", 0, 0, 2);
    }
    else if (substate == SUB_ONLINE_LOBBY)
    {
        draw_text_bg_centered_2(192, 40, "YOU ARE IN A LOBBY", global.palette[11], 8, 8, false);
        draw_set_color(global.palette[11]);
        
        for (var player = 0; player < max(2, ds_list_size(lobbyUsers)); player++)
        {
            var text = "PLAYER " + string(player + 1) + ": ";
            var readyText = "";

            var color;
            if (ds_list_size(lobbyUsers) > player)
            {
                var user = ds_list_find_value(lobbyUsers, player);
                text += user.personaName;

                if (user.ready)
                {
                    readyText = "[READY] ";
                    color = 12;
                }
                else
                {
                    color = 11;
                }
            }
            else
            {
                text += "<EMPTY>";
                color = 28;
            }
            
            draw_set_color(global.palette[color]);
            draw_text(104, 64 + player * 16, text);
            draw_text(104 - string_length(readyText) * 8, 64 + player * 16, readyText);
        }
        
        sel = 0;
        selY = 112;
        scrDrawOnlineMenuSelection("READY", !readyState);
        scrDrawOnlineMenuSelection("UNREADY", readyState);
        scrDrawOnlineMenuSelection("LEAVE LOBBY", true);

        draw_set_color(global.palette[11]);
        scrDrawTextInput(192, 168, "[2] CONFIRM       [1] BACK", 0, 0, 2);
    }
    else if (substate == SUB_ONLINE_CONNECT)
    {
        draw_text_bg_centered_2(192, 100, "CONNECTING ...", global.palette[11], 8, 8, false);
    }
}

enum LOG
{
    NONE = 0,
    INFO = 1,
    VERBOSE = 2,

    LEVEL = 2
}
