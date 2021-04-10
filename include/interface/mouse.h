#pragma once


namespace Input
{

//TODO: rename those to btn_*
enum Button { Left = 0, Right, Middle, Count};

enum Cursor {
    Cursor_None = -1,
    Cursor_Arrow = 0,
    Cursor_Text_Input,
    Cursor_Resize_Horizental,
    Cursor_Resize_Vertical,
    Cursor_Resize_Bottom_Left,
    Cursor_Resize_Bottom_Right,
    Cursor_Hand,
    Cursor_Count,
};


}
