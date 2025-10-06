steam_update();

if (!global.steamReady)
{
    if (steam_initialised())
    {
        if (steam_stats_ready())
            global.steamReady = true;
    }
}

if (steam_is_overlay_activated())
    global.steamOverlayActivated = true;
else
    global.steamOverlayActivated = false;

if (pfo_is_online())
{
    var playerIndex = global.onlineFavoredPlayer;
    global.onlineFavoredPlayer = -1;

    pfo_set_input_delay_favored_client_index(playerIndex >= 0 ? pfo_player_get_client_index(playerIndex) : -1);
}

if (!pfo_update(global.onlineRunUpdate))
{
    show_message("Error: PFO.dll does not exist or failed to load.\n\nPlease make sure you have copied PFO.dll into the same folder as data.win.");
    game_end();
}

if (global.onlineRunUpdate)
{
    pfo_update_extra_input();
}

global.onlineRunUpdate = true;

enum Command
{
    None    = 0,
    Back    = 1,
    Unpause = 2,
    Reset   = 3,
}

enum PFO_InputDelayMode
{
    ManualAll          = 0,
    ManualSelf         = 1,
    AutomaticShared    = 2,
    AutomaticExclusive = 3,
}
