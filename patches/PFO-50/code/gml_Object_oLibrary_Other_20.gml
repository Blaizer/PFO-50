#macro COMPAT_ITEMS_PER_COLUMN 4
#macro COMPAT_ITEMS_PER_PAGE (COMPAT_ITEMS_PER_COLUMN * 2)

if (substate == SUB_ONLINE_INIT)
{
    global.drawLibraryBG = false;

    sel1 = 0;
    sel2 = 0;
    subsel = 1;
    isOwner = false;
    myLobbyIndex = -1;
    compatDiffList = undefined;
    compatPage = 0;
    compatDiffListIsMine = false;
    compatPersonaName = "";
    compatIsDiff = false;
    arrowLeftActive = 0;
    arrowRightActive = 0;
    arrowActiveMax = 4;

    errorMessage = undefined;
    requestTimeoutTime = NaN;
    global.onlineSettings = undefined;
    ds_list_clear(lobbyUsers);

    if (!scrUpdateLobbyUsers(steam_lobby_get_lobby_id()))
    {
        exit;
    }

    scrSwitchSub(SUB_ONLINE_LOBBY);
}
else if (substate == SUB_ONLINE_LOBBY)
{
    var lobbyListCount = ds_list_size(lobbyUsers);

    if (lobbyListCount > 0 && sel1 >= lobbyListCount)
    {
        sel1 = lobbyListCount - 1;
    }

    if (is_undefined(compatDiffList))
    {
        subsel = scrHorzNav(subsel, 1);

        if (subsel == 0 && lobbyListCount > 0)
        {
            sel1 = scrVertNav(sel1, lobbyListCount - 1);
        }
        else if (subsel == 1)
        {
            sel2 = scrVertNav(sel2, 2);
        }

        if ((fire2pressed && subsel == 1 && sel2 == 2) || fire1pressed)
        {
            scrSwitchState(STATE_ONLINE_LOBBY_LIST);
        }
        else if (fire2pressed && subsel == 1 && sel2 == 0)
        {
            requestTimeoutTime = current_time + ONLINE_REQUEST_TIMEOUT;
            pfo_steam_lobby_set_member_data(steam_lobby_get_lobby_id(), "ready", ds_list_find_value(lobbyUsers, myLobbyIndex).ready ? "0" : "1");
        }
        else if (fire2pressed && subsel == 1 && sel2 == 1)
        {
            steam_lobby_activate_invite_overlay();
            scrInputClear();
        }
        else if (fire2pressed && subsel == 0 && sel1 < lobbyListCount && myLobbyIndex >= 0)
        {
            var user = ds_list_find_value(lobbyUsers, sel1);
            var myUser = ds_list_find_value(lobbyUsers, myLobbyIndex);
            if (!is_undefined(myUser.compat) && !is_undefined(user.compat))
            {
                compatDiffList = scrGetCompatibilityDiffList(myUser.compat, user.compat);
                compatPage = 0;
                compatDiffListIsMine = sel1 == myLobbyIndex;
                compatPersonaName = user.personaName;
                compatIsDiff = myUser.compat.hash != user.compat.hash;
                arrowLeftActive = 0;
                arrowRightActive = 0;
            }
        }
    }
    else
    {
        if (arrowLeftActive > 0) arrowLeftActive--;
        if (arrowRightActive > 0) arrowRightActive--;

        var numPages = (array_length(compatDiffList) + COMPAT_ITEMS_PER_PAGE - 1) div COMPAT_ITEMS_PER_PAGE;
        if (numPages > 1)
        {
            var oldCompatPage = compatPage;
            compatPage = scrHorzNav(compatPage, numPages - 1);
            if (oldCompatPage != compatPage && pressLeft) arrowLeftActive = arrowActiveMax;
            if (oldCompatPage != compatPage && pressRight) arrowRightActive = arrowActiveMax;
        }

        if (fire2pressed || fire1pressed)
        {
            compatDiffList = undefined;
        }
    }
}
else
{
    compatDiffList = undefined;

    if (substate == SUB_ONLINE_SET_START_GAME_SETTINGS)
    {
        scrCheckRequestTimeout("CONNECTION TO LOBBY WAS LOST.");
    }
    else if (substate == SUB_ONLINE_SET_STARTING_GAME)
    {
        scrCheckRequestTimeout("LOST CONNECTION TO LOBBY.");
    }
    else if (substate == SUB_ONLINE_CONNECTING)
    {
        scrCheckRequestTimeout("FAILED TO CONNECT TO ALL PLAYERS.");
    }
    else if (substate == SUB_ONLINE_ERROR)
    {
        steam_lobby_leave();

        var _okay = scrGetOkay(errorMessage);
        if (_okay == true)
        {
            errorMessage = undefined;
            scrSwitchState(STATE_PROFILE);
        }
    }
    else if (substate == SUB_ONLINE_COMPAT_WARNING)
    {
        var _okay = scrGetOkay("WARNING: YOUR MODS DIFFER FROM LOBBY OWNER.");
        if (_okay == true)
        {
            scrSwitchSub(SUB_ONLINE_LOBBY);
        }
    }
}

function scrLibraryOnlineLobbyDraw()
{
    var onlineXcenteroff = 0;
    var warningColor = global.palette[10];

    scrDrawMenuBorder(96 + onlineXcenteroff, 8, 192, 24);
    draw_set_color(c_white);
    scrSetFont(global.fontDefault);
    var lobbyName = string_copy(lobbyData.name, 1, 20);
    draw_text_centered(192 + onlineXcenteroff, 16, lobbyName, 8);

    var ownerCompat = ds_list_size(lobbyUsers) > 0 ? ds_list_find_value(lobbyUsers, 0).compat : undefined;
    var myCompat = myLobbyIndex >= 0 ? ds_list_find_value(lobbyUsers, myLobbyIndex).compat : undefined;
    var compatWarning = false;

    for (var player = 0; player < max(8, ds_list_size(lobbyUsers)); player++)
    {
        var text = "";
        var yy = 50 + player * 16;
        var selected = subsel == 0 && sel1 == player;

        var color;
        if (ds_list_size(lobbyUsers) > player)
        {
            var user = ds_list_find_value(lobbyUsers, player);
            text += string_copy(user.personaName, 1, 20);

            color = user.ready ? 0 : 8;
            draw_sprite(sOnlineReadyCheckbox, user.ready ? 1 : 0, 24, yy - 1);

            var isModded = false;
            var isDiff = false;
            if (!is_undefined(user.compat))
            {
                isModded = !(array_length(user.compat.mods) == 0 || (array_length(user.compat.mods) == 3 && user.compat.mods[0] == "PFO 50"));
                isDiff = !is_undefined(myCompat) ? user.compat.hash != myCompat.hash : false;
                compatWarning |= isDiff;
            }

            var modBadgeColor = isDiff ? warningColor : global.palette[8];
            var modBadgeText = (isDiff ? "MODS  " : "MODS: ");

            scrSetFont(global.fontThinOutline);
            draw_set_color(selected ? global.palette[3] : modBadgeColor);
            draw_text(212, yy, modBadgeText);
            if (!is_undefined(user.compat))
            {
                draw_set_color(global.palette[8]);
                draw_text(212 + string_length(modBadgeText) * 6, yy, isModded ? "YES" : "NO");
            }
            scrSetFont(global.fontDefault);

            if (isDiff)
            {
                draw_sprite_ext(sOnlineWarningIcon, 0, 235, yy, 1, 1, 0, selected ? global.palette[3] : warningColor, 1);
            }
        }
        else
        {
            text += "<EMPTY>";
            color = 16;
        }

        draw_set_color(global.palette[color]);
        draw_text(40, yy, text);

        if (selected)
        {
            draw_sprite(sMenuHand, 0, 188, yy - 3);
        }
    }
    
    sel = 0;
    selY = 44;

    var isReady = myLobbyIndex >= 0 && ds_list_find_value(lobbyUsers, myLobbyIndex).ready;
    scrDrawOnlineMenuSelection([ isReady ? "UNREADY" : "READY", "", "" ], true, 8);
    draw_sprite(sOnlineReadyCheckboxLarge, isReady ? 1 : 0, 316, 75);

    scrDrawOnlineMenuSelection([ "INVITE", "FRIENDS", "" ], true, 8);

    scrDrawOnlineMenuBack();

    var myCompatWarning = !is_undefined(ownerCompat) && !is_undefined(myCompat) && ownerCompat.hash != myCompat.hash;
    if (compatWarning || myCompatWarning)
    {
        var warningText = myCompatWarning ? "YOUR MODS DIFFER FROM THE LOBBY OWNER'S" : "NOT ALL PLAYERS ARE USING THE SAME MODS";
        warningText = "WARNING: " + warningText + ". DESYNCS MAY OCCUR IF YOU PLAY. YOU CAN SELECT A PLAYER FOR MORE INFO";
        var lines = string_line_breaks(warningText, 42, 3);
        var xx = 44;
        var yy = 182;
        scrSetFont(global.fontThinOutline);
        draw_set_color(warningColor);
        draw_text(xx, yy, lines[0]);
        draw_text(xx, yy + 8, lines[1]);
        draw_text(xx, yy + 16, lines[2]);
        draw_sprite_ext(sOnlineWarningIconLarge, 0, xx - 22, yy + 4, 1, 1, 0, warningColor, 1);
    }

    if (!is_undefined(compatDiffList))
    {
        draw_set_alpha(0.5);
        draw_rectangle_color(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, c_black, c_black, c_black, c_black, 0);
        draw_set_alpha(1);

        var diffCount = array_length(compatDiffList);
        var numPages = (diffCount + COMPAT_ITEMS_PER_PAGE - 1) div COMPAT_ITEMS_PER_PAGE;
        var width = SCREEN_WIDTH - 36;
        var left = (SCREEN_WIDTH - width) / 2;
        var height = 168;
        var heightPerItem = 32;
        if (numPages > 1)
        {
            height += 16;
        }
        if (diffCount <= COMPAT_ITEMS_PER_COLUMN)
        {
            //width /= 2;

            if (diffCount < COMPAT_ITEMS_PER_COLUMN)
            {
                height -= heightPerItem * (COMPAT_ITEMS_PER_COLUMN - diffCount);
            }
        }
        var top = (SCREEN_HEIGHT - height) / 2;
        scrDrawMenuBorder(left, top, width, height);

        scrSetFont(global.fontTall);
        var title = compatDiffListIsMine ? "MY MODS" : "COMPARING WITH " + string_copy(string_upper(compatPersonaName), 1, 20);
        var titleOff = 12;
        var titleWidth = string_length(title) * 8;
        if (compatIsDiff)
        {
            titleWidth += 16;
            var titleWarningOff = (SCREEN_WIDTH - titleWidth) / 2 + titleWidth - 16;
            draw_sprite_ext(sOnlineWarningIconLarge, 0, titleWarningOff, top + titleOff, 1, 1, 0, warningColor, 1);
        }
        draw_text((SCREEN_WIDTH - titleWidth) / 2, top + titleOff, title);
        scrSetFont(global.fontThinOutline);

        for (var i = 0; i < COMPAT_ITEMS_PER_PAGE; i++)
        {
            var index = i + compatPage * COMPAT_ITEMS_PER_PAGE;
            if (index >= array_length(compatDiffList))
            {
                break;
            }

            var yy = top + titleOff + 28 + (i mod COMPAT_ITEMS_PER_COLUMN) * heightPerItem;
            var xx = left + 10 + (i div COMPAT_ITEMS_PER_COLUMN) * ((width / 2) - 2);
            var maxCharacters = 25;

            draw_set_color(c_white);
            var name = string_copy(compatDiffList[index].name, 1, maxCharacters);
            var nameWidth = string_length(name) * 6 - 1;
            if (compatIsDiff && compatDiffList[index].status == 1)
            {
                draw_sprite_ext(sOnlineWarningIcon, 0, xx + nameWidth, yy - 1, 1, 1, 0, warningColor, 1);
            }
            draw_text(xx, yy - 1, name);
            draw_line(xx - 1, yy + 6, xx - 1 + nameWidth, yy + 6);

            for (var n = 0; n < 2; n++)
            {
                var prefix = (compatDiffListIsMine ? "" : (n == 0 ? "OURS:   " : "THEIRS: "));
                var prefixLength = string_length(prefix);
                var version = compatDiffList[index].versions[n];

                if (n == 0 || !compatDiffListIsMine)
                {
                    var yoff = yy + (n + 1) * 8 + (compatDiffListIsMine ? 4 : 0);
                    draw_set_color(global.palette[8]);
                    draw_text(xx, yoff, prefix);
                    draw_set_color(version == "" ? global.palette[16] : global.palette[8]);
                    draw_text(xx + prefixLength * 6, yoff, string_copy(version == "" ? "<NOT INSTALLED>" : version, 1, maxCharacters - prefixLength));
                }
            }
        }

        if (numPages > 1)
        {
            var ypages = top + height - 16;
            var right = left + width;
            scrSetFont(global.fontDefault);
            draw_set_color(global.palette[3]);
            draw_set_halign(fa_center);
            var numPagesText = string(numPages);
            var pageNumText = string(compatPage + 1);
            var pad = string_length(numPagesText) - string_length(pageNumText);
            draw_text(SCREEN_WIDTH / 2, ypages, "PAGE  " + string_repeat("0", pad) + pageNumText + "/" + numPagesText);
            draw_set_halign(fa_left);
            draw_sprite(sMenuArrow, arrowLeftActive  > 0 ? 1 : 0, left, ypages + 2);
            draw_sprite(sMenuArrow, arrowRightActive > 0 ? 3 : 2, right, ypages + 2);
        }
    }
}
