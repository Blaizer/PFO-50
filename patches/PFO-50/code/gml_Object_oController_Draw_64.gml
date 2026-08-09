surface_set_target(application_surface);
var origFont = draw_get_font();
var origColor = draw_get_color();
var origHAlign = draw_get_halign();
var origVAlign = draw_get_valign();

if (keyboard_check_pressed(vk_f2))
{
    showDelay = !showDelay;
}

if (keyboard_check_pressed(vk_f3))
{
    showPing = !showPing;
    showFps = showPing;
    showFrame = showPing && keyboard_check(vk_shift);
}

function scrFib(n)
{
    if (n <= 3) return n;
    return scrFib(n - 1) + scrFib(n - 2);
}

var playingReplay = pfo_is_playing_replay();
if (playingReplay)
{
    for (var i = 1; i < 10; i++)
    {
        if (keyboard_check_pressed(ord("0") + i))
        {
           pfo_game_set_speed(scrFib(i), pfo_gamespeed_multiplier); 
        }
    }
}

// if (keyboard_check_pressed(vk_f5))
//     global.mGameOnlineHUDHAlign[global.currGame] = 0;
// if (keyboard_check_pressed(vk_f6))
//     global.mGameOnlineHUDHAlign[global.currGame] = 1;
// if (keyboard_check_pressed(vk_f7))
//     global.mGameOnlineHUDHAlign[global.currGame] = 2;

// if (keyboard_check_pressed(vk_f9))
//     global.mGameOnlineHUDVAlign[global.currGame] = 0;
// if (keyboard_check_pressed(vk_f10))
//     global.mGameOnlineHUDVAlign[global.currGame] = 2;

scrSetFont(global.fontThinOutline);
draw_set_color(c_white);

// fixes the HUD moving back to the default library position when switching between games in the library attract mode
var currGame = global.currGame;
if (currGame == 0 && global.attractModeLibraryTimer >= global.AM_LIB_TIME)
{
    currGame = prevGame;
}
prevGame = currGame;

var halign = fa_right;
var valign = fa_top;
if (currGame >= 1 && currGame <= global.NUM_GAMES)
{
    halign = global.mGameOnlineHUDHAlign[currGame];
    valign = global.mGameOnlineHUDVAlign[currGame];
}
draw_set_halign(halign);
draw_set_valign(valign);

var xx = 1;
var yy = 1;
var ydelta = 8;

if (halign == fa_right)
{
    xx = SCREEN_WIDTH;
}
else if (halign == fa_center)
{
    xx = SCREEN_WIDTH / 2;
}

if (valign == fa_bottom)
{
    yy = SCREEN_HEIGHT + 1;
    ydelta *= -1;
}

if (pfo_is_online())
{
    if (!global.paused && !global.disableSettingOnlinePlayers)
    {
        scrSetOnlinePlayers();
    }

    var favoredPlayer = -1;
    var favoredClient = -1;
    if (global.paused)
    {
        with (oPauseMenu)
        {
            if (menuType == 0)
            {
                favoredClient = pfo_player_get_client_index(player);
            }
        }
    }
    else if (global.currGame >= 1 && global.currGame <= global.NUM_GAMES && global.attractModeLibraryTimer < global.AM_LIB_TIME)
    {
        if (global.mGamePlayersSupported[global.currGame] == 1)
        {
            favoredClient = pfo_player_get_client_index(0);
        }
        else if (room != rmTitleScreens && !global.attractMode && !global.playIntro)
        {
            if (global.mGameOnlineFavoredPlayerFunction[global.currGame] != -1)
            {
                favoredPlayer = scrCallFunction(global.mGameOnlineFavoredPlayerFunction[global.currGame]);
                favoredClient = favoredPlayer >= 0 ? pfo_player_get_client_index(favoredPlayer) : -1;
            }
            else if (global.numPlayers == 1)
            {
                favoredClient = pfo_player_get_client_index(0);
            }
        }
    }
    pfo_set_input_delay_favored_client_index(favoredClient);

    var catchupMultiplier = pfo_game_get_speed(pfo_gamespeed_catchup_multiplier);
    if (catchupMultiplier >= 2.0)
    {
        showingCatchupWarning = true;
    }
    else if (catchupMultiplier <= 1.0)
    {
        showingCatchupWarning = false;
    }

    if (showDelay && (pfo_get_frame() > 4 || playingReplay))
    {
        var text;

        if (playingReplay)
        {
            var speedMultiplier = pfo_game_get_speed(pfo_gamespeed_multiplier);
            text = "Replay: " + string(round(speedMultiplier)) + "x";

            if (speedMultiplier == 1.0)
            {
                draw_set_color(global.palette[16]);
            }
        }
        else if (pfo_client_get_player_index() >= 0)
        {
            var mode = pfo_get_input_delay_mode();

            if (mode == PFO_InputDelayMode.Manual)
            {
                draw_set_color(global.palette[16]);
            }
            else if (mode == PFO_InputDelayMode.Automatic && pfo_get_input_delay_favored_client_index() == pfo_get_client_index())
            {
                draw_set_color(global.palette[12]);
            }

            text = (favoredPlayer == -2 ? "Delay: 0/" : "Delay: ") + string(pfo_client_get_input_delay()) + "f";
        }
        else
        {
            text = "Spectating";
        }

        if (showingCatchupWarning)
        {
            var iconX;
            if (halign == fa_center)
            {
                iconX = xx + string_length(text) * 3 - 1;
            }
            else if (halign == fa_right)
            {
                iconX = xx - string_length(text) * 6 - 9;
            }
            else
            {
                iconX = xx + string_length(text) * 6;
            }

            var iconY = yy;
            if (valign == fa_bottom)
            {
                iconY -= 8;
            }

            draw_set_color(global.palette[10]);
            draw_sprite_ext(sOnlineWarningIcon, 0, iconX, iconY, 1, 1, 0, global.palette[10], 1);
        }

        draw_text(xx, yy, text);
        yy += ydelta;

        draw_set_color(c_white);
    }
    
    if (showPing)
    {
        draw_text(xx, yy, "Ping: " + string(pfo_client_get_ping()));
        yy += ydelta;
    }
    
    if (showFps)
    {
        draw_text(xx, yy, "FPS: " + string(fps));
        yy += ydelta;
    }
}

if (showFrame)
{
    draw_text(xx, yy, string(pfo_get_frame()));
    yy += ydelta;
    draw_text(xx, yy, string(global.pfo_current_time));
    yy += ydelta;
}

if (alertMessageTimer > current_time)
{
    scrSetFont(global.fontDefault);
    draw_set_color(c_white);
    draw_set_halign(fa_left);
    draw_set_valign(fa_top);
    draw_text(1, 208, alertMessageText);
}

scrSetFont(origFont);
draw_set_color(origColor);
draw_set_halign(origHAlign);
draw_set_valign(origVAlign);
surface_reset_target();
