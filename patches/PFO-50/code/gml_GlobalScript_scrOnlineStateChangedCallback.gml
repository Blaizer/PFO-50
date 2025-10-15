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

function scrOnlineCleanup()
{
    scrInitAch();

    if (is_array(global.onlineDefaultLanguage))
    {
        global.defaultLanguage = global.onlineDefaultLanguage[1];

        global.profileLanguage = global.defaultLanguage;
        if (global.currFile == 0)
        {
           scrUpdateLanguage(global.defaultLanguage);
        }
    }

    global.onlineDefaultLanguage = undefined;
    global.onlineClientNames = undefined;
    global.onlineRandomizeSeed = undefined;
}

function scrOnlineStateChangedCallback(state)
{
    if (state == PFO_OnlineState.Online)
    {
        steam_lobby_leave();
        pfo_start();
        scrUnpause();

        pfo_set_randomize_seed(global.onlineRandomizeSeed);
        scrRandomize(0);

        global.currGame = 0;
        global.currGameID = 0;
        global.solutionState = false;
        global.memoryMessage = "";
        global.memoryMessagePage = 1;
        global.numPlayers = 1;
        global.playIntro = false;
        global.resetGame = false;

        global.attractMode = false;
        global.attractModeLibrary = false;
        global.attractModeLibraryTimer = 0;
        global.attractModeIndex = 0;
        scrInitAttractModePlaylist();
        global.playbackMode = false;
        global.inputPlayback = array_create(0);
        global.inputFrame = 0;
        global.newInputFrame = true;
        global.playbackOver = false;

        pfo_game_set_speed(global.STANDARD_FPS, gamespeed_fps);
        global.halfTime = 0;
        global.timeStamp = 0;
        global.timeStampGame = 0;
        global.timeStampSpeedRun = 0;
        global.speedRunPrevious = 0;
        global.timeStampIncremental = -1;
        global.timeSumIncremental = 0;

        scrClearCheats();
        global.currFile = 0;
        global.currFileName = "";
        global.backupTimer = global.BACKUP_MINIMUM_TIME;
        global.all50 = 0;

        global.crashToTerminal = 0;
        global.currCutscene = -1;
        global.currGardenWin = 0;
        global.currWin = 0;
        global.multStyle = 0;
        global.selGame = 1;
        global.selSort = 0;

        if (is_array(global.onlineDefaultLanguage))
        {
            global.defaultLanguage = global.onlineDefaultLanguage[0];
        }
        global.profileLanguage = global.defaultLanguage;
        scrUpdateLanguage(global.defaultLanguage);

        global.SKIP_INTRO = 2;
        global.roomPrev = rmInit;
        room_goto(rmLibrary);
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
            scrUnpause();
            scrCloseProfile();
        }
    }
    else if (state == PFO_OnlineState.Offline)
    {
        scrOnlineCleanup();

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
                scrClearCheats();
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
