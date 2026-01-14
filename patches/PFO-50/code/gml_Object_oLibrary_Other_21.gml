if (stateCounter == 0)
{
    global.drawLibraryBG = false;
    requestTimeoutTime = current_time + ONLINE_SYNC_FILES_TIMEOUT;
}
else if (stateCounter == 1)
{
    var allFilesShared = pfo_file_exists(global.ACCOUNT_FILE) >= 0
        && pfo_file_exists(string_replace(global.SAVE_FILE, "*", "1")) >= 0 
        && pfo_file_exists(string_replace(global.SAVE_FILE, "*", "2")) >= 0 
        && pfo_file_exists(string_replace(global.SAVE_FILE, "*", "3")) >= 0;

    if (!allFilesShared)
    {
        if (requestTimeoutTime <= current_time)
        {
            scrOnlineCleanup(false);
            scrSwitchState(STATE_ONLINE_LOBBY_LIST);
            exit;
        }

        global.onlineRunUpdate = false;
        exit;
    }
}
else if (stateCounter == 2)
{
    var players = [];
    var assignedPlayerCount = min(pfo_get_client_count(), global.MAX_PLAYERS_SUPPORTED);
    for (var i = 0; i < assignedPlayerCount; i++)
    {
        players[i] = i;
    }
    scrSetOnlinePlayers(players);

    scrInitAch();
}
else if (stateCounter == 3)
{
    scrSwitchState(STATE_PROFILE);
}

stateCounter++;

function scrLibraryOnlineConnectingDraw()
{
    scrSetFont(global.fontTall);
    draw_set_color(c_white);
    draw_text_centered(SCREEN_WIDTH / 2, 96, "CONNECTING ...", 8);
}
