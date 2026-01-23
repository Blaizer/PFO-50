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

function scrCreateCompatibilityInfoFromString(compatString)
{
    var _compat =
    {
        hash: "",
        version: "",
        mods: [],
    };

    if (is_string(compatString) && compatString != "")
    {
        try
        {
            var _info = json_parse(compatString);

            if (is_string(_info.hash))
            {
                _compat.hash = _info.hash;
            }

            if (is_string(_info.version))
            {
                _compat.version = _info.version;
            }

            if (is_array(_info.mods))
            {
                for (var i = 0; i < array_length(_info.mods) - 1; i += 2)
                {
                    if (is_string(_info.mods[i]) && is_string(_info.mods[i + 1]))
                    {
                        array_push(_compat.mods, _info.mods[i]);
                        array_push(_compat.mods, _info.mods[i + 1]);
                    }
                }
            }
        }
        catch (_exception)
        {
        }
    }

    return _compat;
}

function scrInitCompatibilityInfo()
{
    global.onlineCompatibilityInfo = scrCreateCompatibilityInfoFromString(extension_get_option_value("PFO", "compat"));

    if (global.onlineCompatibilityInfo.hash == "")
    {
        var _dataPath = working_directory + "data.win";
        if (file_exists(_dataPath))
        {
            var _buf = -1;
            
            try
            {
                _buf = buffer_load(_dataPath);
                var _size = buffer_get_size(_buf);
                global.onlineCompatibilityInfo.hash = buffer_md5(_buf, 0, _size);
            }
            catch (_exception)
            {
            }

            if (_buf != -1)
            {
                buffer_delete(_buf);
            }
        }
    }
}

function scrSetOnlinePlayers()
{
    if (pfo_is_online())
    {
        var players = argument_count > 0 ? argument[0] : global.onlinePlayers;
        
        var playersLength = array_length(players);
        var playerCount;
        for (playerCount = 0; playerCount < playersLength; playerCount++)
        {
            if (players[playerCount] < 0)
            {
                break;
            }
        }

        var assignablePlayerCount = global.MAX_PLAYERS_SUPPORTED;
        if (global.currGame >= 1 && global.currGame <= global.NUM_GAMES && global.attractModeLibraryTimer < global.AM_LIB_TIME)
        {
            assignablePlayerCount = global.mGamePlayersSupported[global.currGame];
        }
        if (playerCount < assignablePlayerCount)
        {
            assignablePlayerCount = playerCount;
        }

        // we call this function every frame with no arguments, so we want to skip the work if we already have the right number
        // of players assigned. we will call this function with arguments when we actually change what's in the array
        if (argument_count <= 0 && assignablePlayerCount == pfo_get_assigned_clients_count())
        {
            return;
        }

        if (playerCount != assignablePlayerCount)
        {
            var tempPlayers = [];
            array_copy(tempPlayers, 0, players, 0, assignablePlayerCount);
            players = tempPlayers;
        }

        pfo_set_players(players);

        if (pfo_client_get_player_index() >= 0 && global.onlineSpectatorPauseMenu != noone)
        {
            instance_destroy(global.onlineSpectatorPauseMenu);
            global.onlineSpectatorPauseMenu = noone;
        }
    }
}

function scrOnlineCleanup(forceExitToTitleScreen)
{
    var saveFileOwned = !is_int64(pfo_file_status(global.ACCOUNT_FILE));
    var onlinePlayerIndex = pfo_client_get_player_index();
    
    if (!saveFileOwned || forceExitToTitleScreen)
    {
        scrUnpause();
        scrCloseProfile();

        if (!saveFileOwned)
        {
            instance_destroy(oSaveIcon);
        }
    }
    else
    {
        // make sure we don't get softlocked in the pause screen
        if (global.paused)
        {
            with (oPauseMenu)
            {
                if (menuType == 0)
                {
                    if (player != -1 && player != onlinePlayerIndex)
                    {
                        scrUnpause();
                    }
                    else
                    {
                        player = -1;
                    }
                }
            }
        }
    }

    pfo_reset();
    scrInitAch();

    if (!is_undefined(global.onlineBackupDefaultLanguage))
    {
        global.defaultLanguage = global.onlineBackupDefaultLanguage;

        if (global.currFile == 0)
        {
           scrUpdateLanguage(global.defaultLanguage);
        }
    }

    instance_destroy(global.onlineSpectatorPauseMenu);
    global.onlineSpectatorPauseMenu = noone;

    global.onlineSettings = undefined;
    global.onlineBackupDefaultLanguage = undefined;
    global.onlinePlayers = undefined;
    global.disableSettingOnlinePlayers = false;

    if (!saveFileOwned || forceExitToTitleScreen)
    {
        global.attractModeLibraryTimer = 0;

        // we need to return to the title screen to make sure we don't keep using the current save file
        if (room == rmLibrary)
        {
            instance_destroy(oOkay);
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
            global.roomPrev = rmLibrary;
        }
    }
}

function scrOnlineStateChangedCallback(state)
{
    if (state == PFO_OnlineState.Online)
    {
        steam_lobby_leave();

        pfo_start();
        global.onlineSimultaneousTurns = false;
        global.onlineRunUpdate = true;
        global.onlineFavoredPlayer = -1;
        global.onlinePlayers = [];
        global.disableSettingOnlinePlayers = true;
        scrUnpause();

        var clientCount = pfo_get_client_count();
        for (var i = 0; i < global.MAX_PLAYERS_SUPPORTED; i++)
        {
            global.onlinePlayers[i] = i < clientCount ? i : -1;
        }

        pfo_set_randomize_seed(global.onlineSettings.randomizeSeed);
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

        if (pfo_get_client_index() != 0)
        {
            global.onlineBackupDefaultLanguage = global.defaultLanguage;
        }
        global.defaultLanguage = global.onlineSettings.defaultLanguage;
        scrUpdateLanguage(global.defaultLanguage);
        global.profileLanguage = global.defaultLanguage;

        global.SKIP_INTRO = 2;
        global.roomPrev = rmLibrary;
        room_goto(rmLibrary);
    }
    else if (state == PFO_OnlineState.Disconnecting)
    {
        scrOnlineCleanup(false);
    }
}
