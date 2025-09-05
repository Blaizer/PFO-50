function scrInitAttractModePlaylist()
{
    ds_list_clear(global.attractModePlaylist);
    
    for (var i = 1; i <= global.NUM_LIBRARY_GAMES; i++)
    {
        if (global.mGameAttractModeTimer[i] && global.mGameRoom[i] != -1)
            ds_list_add(global.attractModePlaylist, i);
    }
    
    scrShuffle(global.attractModePlaylist);
}

function scrOnlineStateChangedCallback(state)
{
    if (state == PFO_OnlineState.Online)
    {
        steam_lobby_leave();
        pfo_start();
        scrRandomize(0);
        scrInitAttractModePlaylist();
        global.attractModeIndex = 0;

        if (is_array(global.onlineDefaultLanguage))
        {
            global.defaultLanguage = global.onlineDefaultLanguage[0];
        }
        global.profileLanguage = global.defaultLanguage;
        scrUpdateLanguage(global.defaultLanguage);

        with (oLibrary)
        {
            scrSwitchState(STATE_LASER_X);
            event_perform(ev_step, ev_step_begin);
        }
    }
    else if (state == PFO_OnlineState.Disconnecting)
    {
        var otherPlayer = pfo_get_online_player_index() == 1 ? 1 : 2;
        alertMessageText = "PLAYER " + string(otherPlayer) + " DISCONNECTED";
        alertMessageTimer = current_time + 5000;
        saveFileOwned = !is_int64(pfo_file_status(global.ACCOUNT_FILE));
        previousOnlinePlayerIndex = pfo_get_online_player_index();
        
        if (!saveFileOwned)
        {
            scrCloseProfile();
        }
    }
    else if (state == PFO_OnlineState.Offline)
    {
        if (is_array(global.onlineDefaultLanguage))
        {
            global.defaultLanguage = global.onlineDefaultLanguage[1];
            global.onlineDefaultLanguage = undefined;

            global.profileLanguage = global.defaultLanguage;
            scrUpdateLanguage(global.defaultLanguage);
        }

        if (saveFileOwned)
        {
            // make sure we don't get softlocked in the pause screen
            if (global.paused)
            {
                var previousPlayer = previousOnlinePlayerIndex;
                with (oPauseMenu)
                {
                    if (previousPlayer != player)
                    {
                        scrUnpause();
                    }
                    else
                    {
                        player = 2;
                    }
                }
            }
        }
        else
        {
            global.currFile = 0;
            scrInitAch();
            scrClearCheats();
            scrUnpause();

            // we need to return to the title screen to make sure we don't keep using the current save file
            if (room == rmLibrary)
            {
                with (oLibrary)
                {
                    scrSwitchState(STATE_LOGO);
                    event_perform(ev_step, ev_step_begin);
                }
            }
            else
            {
                scrExitToLibrary();
                global.SKIP_INTRO = true;
                global.roomPrev = rmInit;
            }
        }
    }
}

enum PFO_OnlineState
{
    Offline,
    Online,
    Disconnecting
}
