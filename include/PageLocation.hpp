#pragma once

#include <QJsonArray>

struct PageLocation
{
    int pageno;
    float x, y;

    bool operator==(const PageLocation &other) const
    {
        return pageno == other.pageno && x == other.x && y == other.y;
    }

    QJsonArray toJson() const
    {
        QJsonArray json_array;
        json_array.append(pageno);
        json_array.append(x);
        json_array.append(y);
        return json_array;
    }

    static PageLocation fromJson(const QJsonArray &json_array)
    {
        if (json_array.size() != 3)
        {
            throw std::invalid_argument("Invalid JSON array for PageLocation");
        }

        return PageLocation{json_array[0].toInt(),
                            static_cast<float>(json_array[1].toDouble()),
                            static_cast<float>(json_array[2].toDouble())};
    }
};
