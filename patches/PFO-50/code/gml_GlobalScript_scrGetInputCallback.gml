function scrSetInputFromPFOFlags(c)
{
    holdUp        = (c & (int64(1) << int64(0))) != 0;
    holdDown      = (c & (int64(1) << int64(1))) != 0;
    holdLeft      = (c & (int64(1) << int64(2))) != 0;
    holdRight     = (c & (int64(1) << int64(3))) != 0;
    fire1         = (c & (int64(1) << int64(4))) != 0;
    fire2         = (c & (int64(1) << int64(5))) != 0;
    forcePause    = (c & (int64(1) << int64(6))) != 0;
    pressUp       = (c & (int64(1) << int64(7))) != 0;
    pressDown     = (c & (int64(1) << int64(8))) != 0;
    pressLeft     = (c & (int64(1) << int64(9))) != 0;
    pressRight    = (c & (int64(1) << int64(10))) != 0;
    fire1pressed  = (c & (int64(1) << int64(11))) != 0;
    fire2pressed  = (c & (int64(1) << int64(12))) != 0;
    pressStart    = (c & (int64(1) << int64(13))) != 0;
    fire1released = (c & (int64(1) << int64(14))) != 0;
    fire2released = (c & (int64(1) << int64(15))) != 0;
}

function scrGetPFOFlagsFromInput()
{
    var c = int64(0);
    if (holdUp)        c |= int64(1) << int64(0);
    if (holdDown)      c |= int64(1) << int64(1);
    if (holdLeft)      c |= int64(1) << int64(2);
    if (holdRight)     c |= int64(1) << int64(3);
    if (fire1)         c |= int64(1) << int64(4);
    if (fire2)         c |= int64(1) << int64(5);
    if (forcePause)    c |= int64(1) << int64(6);
    if (pressUp)       c |= int64(1) << int64(7);
    if (pressDown)     c |= int64(1) << int64(8);
    if (pressLeft)     c |= int64(1) << int64(9);
    if (pressRight)    c |= int64(1) << int64(10);
    if (fire1pressed)  c |= int64(1) << int64(11);
    if (fire2pressed)  c |= int64(1) << int64(12);
    if (pressStart)    c |= int64(1) << int64(13);
    if (fire1released) c |= int64(1) << int64(14);
    if (fire2released) c |= int64(1) << int64(15);
    return c;
}

function scrGetInputCallback(frame)
{
    if (argument_count == 1)
    {
        // Default mode: just get new input normally
        scrGetInput(0, GetInputType.Raw);
        return pfo_add_extra_input(frame, scrGetPFOFlagsFromInput());
    }
    else if (argument_count == 2)
    {
        // Extend mode: take the previous input and mask it so we only keep the "held" buttons
        return pfo_add_extra_input(frame, pfo_remove_extra_input(argument[1]) & int64(0x7f));
    }
    else if (argument_count == 3)
    {
        // Keep mode: keep it if it's different
        return pfo_remove_extra_input(argument[1]) != pfo_remove_extra_input(argument[2])
            || pfo_add_extra_input(frame, argument[1], argument[2]);
    }
}

enum LOG
{
    NONE = 0,
    INFO = 1,
    VERBOSE = 2,

    LEVEL = 1
}
