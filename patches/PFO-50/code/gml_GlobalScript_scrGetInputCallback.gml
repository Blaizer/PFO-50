function scrSetInputFromPFOFlags(c)
{
    holdUp        = (c & (int64(1) << int64(0)))  != int64(0);
    holdDown      = (c & (int64(1) << int64(1)))  != int64(0);
    holdLeft      = (c & (int64(1) << int64(2)))  != int64(0);
    holdRight     = (c & (int64(1) << int64(3)))  != int64(0);
    pressUp       = (c & (int64(1) << int64(4)))  != int64(0);
    pressDown     = (c & (int64(1) << int64(5)))  != int64(0);
    pressLeft     = (c & (int64(1) << int64(6)))  != int64(0);
    pressRight    = (c & (int64(1) << int64(7)))  != int64(0);
    fire1         = (c & (int64(1) << int64(8)))  != int64(0);
    fire2         = (c & (int64(1) << int64(9)))  != int64(0);
    fire1pressed  = (c & (int64(1) << int64(10))) != int64(0);
    fire2pressed  = (c & (int64(1) << int64(11))) != int64(0);
    fire1released = (c & (int64(1) << int64(12))) != int64(0);
    fire2released = (c & (int64(1) << int64(13))) != int64(0);
    pressStart    = (c & (int64(1) << int64(14))) != int64(0);
    forcePause    = (c & (int64(1) << int64(15))) != int64(0);
}

function scrGetPFOFlagsFromInput()
{
    var c = int64(0);
    if (holdUp)        c |= int64(1) << int64(0);
    if (holdDown)      c |= int64(1) << int64(1);
    if (holdLeft)      c |= int64(1) << int64(2);
    if (holdRight)     c |= int64(1) << int64(3);
    if (pressUp)       c |= int64(1) << int64(4);
    if (pressDown)     c |= int64(1) << int64(5);
    if (pressLeft)     c |= int64(1) << int64(6);
    if (pressRight)    c |= int64(1) << int64(7);
    if (fire1)         c |= int64(1) << int64(8);
    if (fire2)         c |= int64(1) << int64(9);
    if (fire1pressed)  c |= int64(1) << int64(10);
    if (fire2pressed)  c |= int64(1) << int64(11);
    if (fire1released) c |= int64(1) << int64(12);
    if (fire2released) c |= int64(1) << int64(13);
    if (pressStart)    c |= int64(1) << int64(14);
    if (forcePause)    c |= int64(1) << int64(15);
    return c;
}

function scrGetInputCallback(frame)
{
    if (argument_count > 1)
    {
        // Extend mode: take the previous input and mask it so we only keep the "held" buttons
        return argument[1] & int64(0x830f);
    }

    {
        // Default mode: just get new input normally
        scrGetInput(0, GetInputType.Raw);
        return pfo_add_extra_input(frame, scrGetPFOFlagsFromInput());
    }
}

function scrCheckPause()
{
    if (bPressStart[0] || bForcePause[0])
    {
        scrPause(0);
    }
    else if (bPressStart[1] || bForcePause[1])
    {
        scrPause(1);
    }
    else if (global.MAX_PLAYERS_SUPPORTED > 2)
    {
        with (oScreenHandler)
        {
            scrInputClear();
            for (var i = 2; i < global.MAX_PLAYERS_SUPPORTED; i++)
            {
                scrGetInput(i);
                if (pressStart || forcePause)
                {
                    scrPause(i);
                    break;
                }
            }
        }
    }
}

function scrGetInputAllN(n)
{
    n = min(n, global.MAX_PLAYERS_SUPPORTED);
    var _getInputType = (argument_count > 1) ? argument[1] : int64(0);

    for (var p = n; p >= 0; p--)
    {
        scrGetInput(p, _getInputType);
        bHoldUp[p] = holdUp;
        bHoldDown[p] = holdDown;
        bHoldLeft[p] = holdLeft;
        bHoldRight[p] = holdRight;
        bPressUp[p] = pressUp;
        bPressDown[p] = pressDown;
        bPressLeft[p] = pressLeft;
        bPressRight[p] = pressRight;
        bFire1[p] = fire1;
        bFire2[p] = fire2;
        bFire1pressed[p] = fire1pressed;
        bFire2pressed[p] = fire2pressed;
        bFire1released[p] = fire1released;
        bFire2released[p] = fire2released;
        bPressStart[p] = pressStart;
        bForcePause[p] = forcePause;
    }
}

function scrGetInputMergedN(n)
{
    n = min(n, global.MAX_PLAYERS_SUPPORTED);
    scrGetInputAllN(n, (argument_count > 1) ? argument[1] : int64(0));

    for (var p = n; p >= 1; p--)
    {
        holdUp = holdUp || bHoldUp[p];
        holdDown = holdDown || bHoldDown[p];
        holdLeft = holdLeft || bHoldLeft[p];
        holdRight = holdRight || bHoldRight[p];
        pressUp = pressUp || bPressUp[p];
        pressDown = pressDown || bPressDown[p];
        pressLeft = pressLeft || bPressLeft[p];
        pressRight = pressRight || bPressRight[p];
        fire1 = fire1 || bFire1[p];
        fire2 = fire2 || bFire2[p];
        fire1pressed = fire1pressed || bFire1pressed[p];
        fire2pressed = fire2pressed || bFire2pressed[p];
        pressStart = pressStart || bPressStart[p];
        forcePause = forcePause || bForcePause[p];
    }
}
