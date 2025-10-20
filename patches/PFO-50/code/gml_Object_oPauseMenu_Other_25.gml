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

function scrGetPlayerNames(fromNames, excludingIndex)
{
    var names = array_create(array_length(fromNames));
    var index = 0;
    for (var clientIndex = 0; clientIndex < array_length(global.onlineClientNames); clientIndex++)
    {
        if (clientIndex != excludingIndex)
        {
            names[index] = fromNames[clientIndex];
            selectionClientIndex[index++] = clientIndex;
        }
    }

    names[index] = "NONE";
    selectionClientIndex[index] = -1;

    return names;
}

SUB_PRE_INIT = 0;
SUB_INIT = 1;
SUB_NAV = 2;
SUB_RESET = 3;

if (!pfo_is_online())
{
    scrSwitchState(statePrev);
    exit;
}

if (substate == SUB_PRE_INIT)
{
    menuSel = 0;
    scrSwitchSub(SUB_INIT);

    var prevMaxAutomaticInputDelay = pfo_get_max_automatic_input_delay();
    inputDelayLimit = pfo_set_max_automatic_input_delay(0x7fffffff);
    pfo_set_max_automatic_input_delay(prevMaxAutomaticInputDelay);

    if (is_undefined(onlinePlayers))
    {
        for (var playerIndex = 0; playerIndex < 2; playerIndex++)
        {
            onlinePlayers[playerIndex] = pfo_player_get_client_index(playerIndex);
        }
    }
}
else if (substate == SUB_INIT)
{
    drawMenu = true;
    scrMenuCreate("ONLINE SETTINGS", menuSel);
    
    var playerNames = array_create(array_length(global.onlineClientNames));
    for (var clientIndex = 0; clientIndex < array_length(global.onlineClientNames); clientIndex++)
    {
        playerNames[clientIndex] = string_copy(global.onlineClientNames[clientIndex], 1, 15);
    }

    var playerNames2 = scrGetPlayerNames(playerNames, onlinePlayers[0]);

    var allowPlayerAssignment = true;
    if (global.currGameID == 14 && global.numPlayers == 2 && pfo_is_online())
    {
        allowPlayerAssignment = false;
    }

    OP_PLAYER1 = noone;
    OP_PLAYER2 = noone;
    OP_AUTO_DELAY_MIN = noone;
    OP_AUTO_DELAY_MAX = noone;
    OP_MANUAL_DELAY = noone;
    OP_MANUAL_DELAY_P1 = noone;
    OP_MANUAL_DELAY_P2 = noone;

    if (allowPlayerAssignment)
    {
        OP_PLAYER1 = scrMenuItem(TYPE_DUAL, "P1 ASSIGN", onlinePlayers[0], playerNames);
        OP_PLAYER2 = scrMenuItem(TYPE_DUAL, "P2 ASSIGN", onlinePlayers[1] < 0 ? 1 : 0, playerNames2);
    }

    var mode = pfo_get_input_delay_mode();
    OP_DELAY_MODE = scrMenuItem(TYPE_DUAL, "INPUT DELAY MODE", mode, ["AUTO", "MANUAL"]);

    if (mode == InputDelayMode.Automatic)
    {
        OP_AUTO_DELAY_MIN = scrMenuItem(TYPE_DUAL_INT, "MIN INPUT DELAY", pfo_get_min_automatic_input_delay(), 0, inputDelayLimit);
        OP_AUTO_DELAY_MAX = scrMenuItem(TYPE_DUAL_INT, "MAX INPUT DELAY", pfo_get_max_automatic_input_delay(), 0, inputDelayLimit);
        scrMenuSpacer(MENU_MEDIUM_SPACER);
    }
    else if (mode == InputDelayMode.Manual)
    {
        var manualInputDelays = array_create(2);
        for (var playerIndex = 0; playerIndex < 2; playerIndex++)
        {
            if (onlinePlayers[playerIndex] >= 0)
            {
                manualInputDelays[playerIndex] = pfo_client_get_input_delay(onlinePlayers[playerIndex]);
            }
        }

        OP_MANUAL_DELAY = scrMenuItem(TYPE_DUAL_INT, "INPUT DELAY", max(manualInputDelays[0], manualInputDelays[1]), 0, inputDelayLimit);
        OP_MANUAL_DELAY_P1 = scrMenuItem(TYPE_DUAL_INT, "P1 INPUT DELAY", manualInputDelays[0], 0, inputDelayLimit);
        OP_MANUAL_DELAY_P2 = scrMenuItem(TYPE_DUAL_INT, "P2 INPUT DELAY", manualInputDelays[1], 0, inputDelayLimit);
    }

    scrMenuSpacer(MENU_MEDIUM_SPACER);
    scrMenuSpacer(MENU_MEDIUM_SPACER);
    scrMenuSpacer(MENU_MEDIUM_SPACER);
    scrMenuSpacer(MENU_MEDIUM_SPACER);

    if (!allowPlayerAssignment)
    {
        scrMenuSpacer(MENU_MEDIUM_SPACER);
        scrMenuSpacer(MENU_MEDIUM_SPACER);
    }
    
    OP_BACK = scrMenuItem(TYPE_SINGLE, scrString("menu_item_back_to_root"));
    
    scrSwitchSub(SUB_NAV);
}
else if (substate == SUB_NAV)
{
    var choice = scrMenuNavigation();
    
    if (!localInputDisabled() && keyboard_check_pressed(vk_f1))
    {
        pfo_send_command(Command.Reset);
    }

    if (pfo_receive_command(Command.Reset))
    {
        scrSfxLibrary(soundSet[currentSoundSet]);
        scrSwitchSub(SUB_RESET);
    }
    else if (choice == -2)
    {
        scrSfxLibrary(soundSubExit[currentSoundSet]);
        scrSwitchState(statePrev);
    }
    else if (pressStart)
    {
        scrSwitchState(STATE_UNPAUSE);
    }
    else if (choice >= 0)
    {
        switch (menuSel)
        {
            case OP_PLAYER1:
                onlinePlayers[0] = itemIndex[OP_PLAYER1];
                itemValues[OP_PLAYER2] = scrGetPlayerNames(itemValues[OP_PLAYER1], onlinePlayers[0]);
                // fallthrough;
            
            case OP_PLAYER2:
                onlinePlayers[1] = selectionClientIndex[itemIndex[OP_PLAYER2]];
                scrUpdateManualInputDelays();
                break;
            
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
                itemIndex[OP_MANUAL_DELAY_P1] = choice;
                itemIndex[OP_MANUAL_DELAY_P2] = choice;
                scrUpdateManualInputDelays();
                break;
                
            case OP_MANUAL_DELAY_P1:
            case OP_MANUAL_DELAY_P2:
                scrUpdateManualInputDelays();
                break;
                
            case OP_BACK:
                scrSfxLibrary(soundSubExit[currentSoundSet]);
                scrSwitchState(statePrev);
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
}
