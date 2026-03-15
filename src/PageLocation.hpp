#pragma once

struct PageLocation
{
    int pageno;
    float x, y;

    bool operator==(const PageLocation &other) const
    {
        return pageno == other.pageno && x == other.x && y == other.y;
    }
};
