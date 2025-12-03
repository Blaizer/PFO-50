#macro SUB_PRE_INIT 0
#macro SUB_INIT 1
#macro SUB_NAV 2
#macro SUB_RESET 3

function scrUpdateManualInputDelays()
{
    if (pfo_get_input_delay_mode() == InputDelayMode.Manual)
    {
        for (var playerIndex = 0; playerIndex < 2; playerIndex++)
        {
            if (onlinePlayers[playerIndex] >= 0)
            {
                pfo_client_set_input_delay(itemIndex[OP_MANUAL_DELAY_P1 + playerIndex], onlinePlayers[playerIndex]);
            }
        }
    }
}

if (!pfo_is_online())
{
    scrSwitchState(statePrev);
    exit;
}

if (substate == SUB_PRE_INIT)
{
    menuSel = 0;
    scrSwitchSub(SUB_INIT);

    assignablePlayerCount = global.MAX_PLAYERS_SUPPORTED;

    var prevMaxAutomaticInputDelay = pfo_get_max_automatic_input_delay();
    inputDelayLimit = pfo_set_max_automatic_input_delay(0x7fffffff);
    pfo_set_max_automatic_input_delay(prevMaxAutomaticInputDelay);

    if (is_undefined(onlinePlayers))
    {
        for (var playerIndex = 0; playerIndex < assignablePlayerCount; playerIndex++)
        {
            onlinePlayers[playerIndex] = pfo_player_get_client_index(playerIndex);
        }
    }
}
else if (substate == SUB_INIT)
{
    drawMenu = true;
    scrMenuCreate("ONLINE SETTINGS", menuSel);
    
    onlinePlayerCount = array_length(global.onlineSettings.clientNames);

    var playerNames = array_create(onlinePlayerCount + 1);
    for (var clientIndex = 0; clientIndex < onlinePlayerCount; clientIndex++)
    {
        playerNames[clientIndex] = string_copy(global.onlineSettings.clientNames[clientIndex], 1, 15);
    }

    playerNames[onlinePlayerCount] = "NONE";

    OP_AUTO_DELAY_MIN = -1;
    OP_AUTO_DELAY_MAX = -1;
    OP_MANUAL_DELAY = -1;
    OP_MANUAL_DELAY_P1 = -1;

    if (!global.onlineSimultaneousTurns)
    {
        for (var i = 0; i < assignablePlayerCount; i++)
        {
            scrMenuItem(TYPE_DUAL_ONLINE_PLAYER, "P" + string(i + 1) + " ASSIGN", onlinePlayers[i] >= 0 ? onlinePlayers[i] : onlinePlayerCount, playerNames);
        }
    }

    var mode = pfo_get_input_delay_mode();
    OP_DELAY_MODE = scrMenuItem(TYPE_DUAL, "INPUT DELAY MODE", mode, ["AUTO", "MANUAL"]);

    if (mode == InputDelayMode.Automatic)
    {
        OP_AUTO_DELAY_MIN = scrMenuItem(TYPE_DUAL_INT, "MIN INPUT DELAY", pfo_get_min_automatic_input_delay(), 0, inputDelayLimit);
        OP_AUTO_DELAY_MAX = scrMenuItem(TYPE_DUAL_INT, "MAX INPUT DELAY", pfo_get_max_automatic_input_delay(), 0, inputDelayLimit);
    }
    else if (mode == InputDelayMode.Manual)
    {
        var manualInputDelays = array_create(assignablePlayerCount);
        for (var playerIndex = 0; playerIndex < assignablePlayerCount; playerIndex++)
        {
            if (onlinePlayers[playerIndex] >= 0)
            {
                manualInputDelays[playerIndex] = pfo_client_get_input_delay(onlinePlayers[playerIndex]);
            }
        }

        OP_MANUAL_DELAY = scrMenuItem(TYPE_DUAL_INT, "INPUT DELAY", max(manualInputDelays[0], manualInputDelays[1]), 0, inputDelayLimit * 2);

        for (var i = 0; i < assignablePlayerCount; i++)
        {
            var item = scrMenuItem(TYPE_DUAL_INT, "P" + string(i + 1) + " INPUT DELAY", manualInputDelays[i], 0, inputDelayLimit * 2);
            if (OP_MANUAL_DELAY_P1 == -1)
            {
                OP_MANUAL_DELAY_P1 = item;
            }
        }
    }

    var lastMenuIndex = menuSelBot;

    repeat (9 - lastMenuIndex)
    {
        scrMenuSpacer(MENU_MEDIUM_SPACER);
    }
    
    OP_BACK = scrMenuItem(TYPE_SINGLE, scrStringManual("menu_item_back_to_root", 0));
    
    if (menuSel > lastMenuIndex && menuSel != OP_BACK)
    {
        menuSel = lastMenuIndex;
    }

    scrSwitchSub(SUB_NAV);
}
else if (substate == SUB_NAV)
{
    if (receiveCommand(Command.Reset))
    {
        scrSfxLibrary(soundSet[currentSoundSet]);
        scrSwitchSub(SUB_RESET);
        exit;
    }

    var _arg = [ int64(0) ];
    if (receiveCommand(Command.SetSel, _arg))
    {
        var _menuSel = real(_arg[0] & int64(0xf));
        var _choice = real(_arg[0] >> int64(4));

        var _previousChoice = itemIndex[_menuSel];
        itemIndex[_menuSel] = _choice;

        onlinePlayers[_menuSel - firstOnlinePlayerMenuIndex] = _choice < onlinePlayerCount ? _choice : -1;

        if (_choice < onlinePlayerCount)
        {
            for (var _sel = _menuSel + 1; _sel <= lastOnlinePlayerMenuIndex; _sel++)
            {
                if (itemIndex[_sel] == _choice)
                {
                    itemIndex[_sel] = _previousChoice;
                    onlinePlayers[_sel - firstOnlinePlayerMenuIndex] = _previousChoice < onlinePlayerCount ? _previousChoice : -1;
                }
            }
        }

        scrUpdateManualInputDelays();
    }

    var choice = scrMenuNavigation();

    if (choice == -2)
    {
        scrSfxLibrary(soundSubExit[currentSoundSet]);
        scrSwitchState(statePrev);
        exit;
    }
    
    if (pressStart)
    {
        scrSwitchState(STATE_UNPAUSE);
        exit;
    }

    if (!localInputDisabled() && keyboard_check_pressed(vk_f1))
    {
        sendCommand(Command.Reset);
    }

    if (choice >= 0)
    {
        switch (menuSel)
        {
            case OP_DELAY_MODE:
                pfo_set_input_delay_mode(choice);
                scrSwitchSub(SUB_INIT);
                break;
            
            case OP_AUTO_DELAY_MIN:
                pfo_set_min_automatic_input_delay(choice);
                break;
            
            case OP_AUTO_DELAY_MAX:
                pfo_set_max_automatic_input_delay(choice);
                break;

            case OP_MANUAL_DELAY:
                for (var i = 0; i < assignablePlayerCount; i++)
                {
                    itemIndex[OP_MANUAL_DELAY_P1 + i] = choice;
                }
                scrUpdateManualInputDelays();
                break;
                
            case OP_BACK:
                scrSfxLibrary(soundSubExit[currentSoundSet]);
                scrSwitchState(statePrev);
                break;

            default:
                if (OP_MANUAL_DELAY_P1 != -1 && menuSel >= OP_MANUAL_DELAY_P1 && menuSel - OP_MANUAL_DELAY_P1 < assignablePlayerCount)
                {
                    scrUpdateManualInputDelays();
                }
                break;
        }
    }
}
else if (substate == SUB_RESET)
{
    if (stateCounter == 0)
    {
        onlinePlayers[0] = 0;
        onlinePlayers[1] = 1;
        pfo_set_input_delay_mode(InputDelayMode.Automatic);
        pfo_set_min_automatic_input_delay(0);
        pfo_set_max_automatic_input_delay(inputDelayLimit);

        stateCounter = 1;
    }
    
    if (stateCounter == 1)
    {
        var _okay = scrGetOkay("menu_misc_defaults_restored", int64(0));
        if (_okay == true)
        {
            scrSwitchSub(SUB_INIT);
        }
    }
}

enum InputDelayMode
{
    Automatic,
    Manual,
}

enum Command
{
    None    = 0,
    Back    = 1,
    Unpause = 2,
    Reset   = 3,
    SetSel  = 4,
}
