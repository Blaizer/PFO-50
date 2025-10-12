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
        pfo_reset();
        pfo_start();
        pfo_set_randomize_seed(global.onlineRandomizeSeed);
        scrRandomize(0);

        global.attractMode = false;
        global.attractModeLibrary = false;
        global.attractModeLibraryTimer = 0;
        global.attractModeIndex = 0;
        scrInitAttractModePlaylist();

        if (is_array(global.onlineDefaultLanguage))
        {
            global.defaultLanguage = global.onlineDefaultLanguage[0];
        }
        global.profileLanguage = global.defaultLanguage;
        scrUpdateLanguage(global.defaultLanguage);

        global.halfTime = 0;

        global.SKIP_INTRO = 2;
        global.roomPrev = rmInit;
        room_restart();
    }
    else if (state == PFO_OnlineState.Disconnecting)
    {
        var otherClientIndex = !pfo_get_client_index();
        var clientName = global.EXTERNAL_TEXT_ERROR;
        if (!is_undefined(global.onlineClientNames) && array_length(global.onlineClientNames) > otherClientIndex)
        {
            clientName = global.onlineClientNames[otherClientIndex];
        }

        alertMessageText = clientName + " Disconnected";
        alertMessageTimer = current_time + 5000;
        saveFileOwned = !is_int64(pfo_file_status(global.ACCOUNT_FILE));
        previousOnlinePlayerIndex = pfo_client_get_player_index();
        
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
            global.attractModeLibraryTimer = 0;
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
