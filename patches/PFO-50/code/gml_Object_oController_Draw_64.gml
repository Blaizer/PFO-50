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
    xx = real(SCREEN.WIDTH);
}
else if (halign == fa_center)
{
    xx = real(SCREEN.WIDTH) / 2;
}

if (valign == fa_bottom)
{
    yy = real(SCREEN.HEIGHT) + 1;
    ydelta *= -1;
}

if (pfo_is_online())
{
    if (showDelay && pfo_get_assigned_clients_count() > 0)
    {
        if (pfo_client_get_player_index() >= 0)
        {
            var mode = pfo_get_input_delay_mode();

            if (mode == InputDelayMode.Manual)
            {
                draw_set_color(global.palette[16]);
            }
            else if (mode == InputDelayMode.Automatic && pfo_get_input_delay_favored_client_index() == pfo_get_client_index())
            {
                draw_set_color(global.palette[12]);
            }

            draw_text(xx, yy, "Delay: " + string(pfo_client_get_input_delay()) + "f");
            yy += ydelta;

            draw_set_color(c_white);
        }
        else
        {
            draw_text(xx, yy, "Spectating");
            yy += ydelta;
        }
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
    draw_text(xx, yy, string(current_time));
    yy += ydelta;
    draw_text(xx, yy, string(current_time - global.pfo_current_time));
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

enum InputDelayMode
{
    Automatic,
    Manual,
}

enum SCREEN
{
    WIDTH = 384,
    HEIGHT = 216,
}
