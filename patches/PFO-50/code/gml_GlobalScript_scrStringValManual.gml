function scrStringValManual()
{
    var _str = scrStringManual(argument[0], argument[1]);
    var _valIndex = 2;
    var _newStr = "";
    var _prevChar = "";
    
    for (var i = 1; i <= string_length(_str); i++)
    {
        var _char = string_char_at(_str, i);
        
        if (_char == "*" && _prevChar != "*")
        {
            _newStr += string(argument[_valIndex]);
            _valIndex++;
            
            if (_valIndex == argument_count)
                _valIndex = 2;
        }
        else if (_char == "*" && _prevChar == "*")
        {
        }
        else
        {
            _newStr += _char;
        }
        
        _prevChar = _char;
    }
    
    return _newStr;
}
