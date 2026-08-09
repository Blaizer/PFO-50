#macro SETTINGS_FILE "settings"

if (stateCounter == 0)
{
    global.drawLibraryBG = false;
    requestTimeoutTime = current_time + ONLINE_SYNC_FILES_TIMEOUT;

    if (pfo_get_client_index() == 0)
    {
        var settings =
        {
            seed: int64(get_timer() + delta_time * 1000000),
            lang: global.onlineBackupDefaultLanguage,
        };
        var settingsJson = json_stringify(settings);
        var settingsBuffer = buffer_create(string_byte_length(settingsJson) + 1, buffer_fixed, 1);
        buffer_write(settingsBuffer, buffer_string, settingsJson);
        pfo_buffer_send(SETTINGS_FILE, settingsBuffer);
        buffer_delete(settingsBuffer);
    }
}
else if (stateCounter == 1)
{
    var allFilesShared = pfo_file_exists(SETTINGS_FILE) >= 0
        && pfo_file_exists(global.ACCOUNT_FILE) >= 0
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
    var randomizeSeed;
    var defaultLanguage;

    try
    {
        var settingsBuffer = pfo_buffer_load(SETTINGS_FILE);
        var settingsJson = buffer_read(settingsBuffer, buffer_string);
        var settings = json_parse(settingsJson);
        randomizeSeed = int64(settings.seed);
        defaultLanguage = real(settings.lang);
        buffer_delete(settingsBuffer);
    }
    catch (_exception)
    {
        scrOnlineCleanup(false);
        scrSwitchState(STATE_ONLINE_LOBBY_LIST);
        exit;
    }

    if (defaultLanguage < 0 || defaultLanguage >= global.NUM_LANG)
    {
        scrOnlineCleanup(false);
        scrSwitchState(STATE_ONLINE_LOBBY_LIST);
        exit;
    }

    pfo_set_randomize_seed(randomizeSeed);
    scrRandomize(0);
    scrInitAttractModePlaylist();

    global.defaultLanguage = defaultLanguage;
    scrUpdateLanguage(defaultLanguage);
    if (pfo_get_client_index() == 0)
    {
        global.onlineBackupDefaultLanguage = undefined;
    }

    global.disableSettingOnlinePlayers = false;
    scrSetOnlinePlayers(global.onlinePlayers);
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
