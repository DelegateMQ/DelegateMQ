#ifndef DATA_H
#define DATA_H

/// @file data.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief Data classes sent by remote delegate, serialized as JSON with
/// RapidJSON. Each argument class writes and reads itself with Write()/Read().

#include "DelegateMQ.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include <iostream>
#include <string>
#include <vector>

/// The serializer class for a remote delegate signature
template <class Sig>
using SampleSerializer = dmq::serialization::rapidjson::Serializer<Sig>;

/// 0: the remote delegate takes one argument, void(Data&). The RapidJSON
/// serializer writes each message as one JSON document, so a signature
/// with several arguments would need them combined into one object.
#define SAMPLE_AUX 0

class DataPoint
{
public:
    int x = 0;
    int y = 0;

    void Read(const rapidjson::Value& value, dmq::ByteReader& is)
    {
        if (value.HasMember("x") && value["x"].IsInt())
            x = value["x"].GetInt();
        else
            std::cout << "Parse error: missing or invalid 'x' value." << std::endl;

        if (value.HasMember("y") && value["y"].IsInt())
            y = value["y"].GetInt();
        else
            std::cout << "Parse error: missing or invalid 'y' value." << std::endl;
    }

    void Write(rapidjson::PrettyWriter<rapidjson::StringBuffer>& writer, const dmq::ByteWriter& os)
    {
        writer.StartObject();

        writer.String("x");
        writer.Int(x);
        writer.String("y");
        writer.Int(y);

        writer.EndObject();
    }
};

/// @brief Argument data sent to endpoint.
class Data
{
public:
    std::vector<DataPoint> dataPoints;
    std::string msg;

    void Read(rapidjson::Document& doc, dmq::ByteReader& is)
    {
        if (doc.HasMember("msg") && doc["msg"].IsString())
            msg = doc["msg"].GetString();
        else
            std::cout << "Parse error: missing or invalid 'msg' value." << std::endl;

        if (doc.HasMember("dataPoints") && doc["dataPoints"].IsArray())
        {
            const rapidjson::Value& points = doc["dataPoints"];
            for (auto& point : points.GetArray())
            {
                DataPoint p;
                p.Read(point, is);
                dataPoints.push_back(p);
            }
        }
        else
        {
            std::cout << "Parse error: missing or invalid 'dataPoints' array." << std::endl;
        }
    }

    void Write(rapidjson::PrettyWriter<rapidjson::StringBuffer>& writer, const dmq::ByteWriter& os)
    {
        writer.StartObject();

        writer.String("msg");
        writer.String(msg.c_str());

        writer.String("dataPoints");
        writer.StartArray();

        for (auto it = dataPoints.begin(); it != dataPoints.end(); ++it)
            (*it).Write(writer, os);

        writer.EndArray();

        writer.EndObject();
    }
};

/// The remote delegate's signature
using SampleSig = void(Data&);

#endif
