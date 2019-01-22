#include <rapidjson/reader.h>
#include <rapidjson/writer.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <staticjson/staticjson.hpp>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <vector>
#include <functional>
#include <windows.h>

using duint = size_t;

struct AddrInfo
{
    duint modhash;
    duint addr;
    bool manual;

    std::string mod() const
    {
        return std::to_string(modhash);
    }
};

typedef enum
{
    XREF_NONE,
    XREF_DATA,
    XREF_JMP,
    XREF_CALL
} XREFTYPE;

typedef struct
{
    duint addr;
    XREFTYPE type;
} XREF_RECORD;


struct XREFSINFO : AddrInfo
{
    XREFTYPE type;
    std::unordered_map<duint, XREF_RECORD> references;
};

using namespace rapidjson;
using namespace std;

static string serialize(const Document& doc)
{
    rapidjson::StringBuffer buffer;

    buffer.Clear();

    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

struct ArrayElementCollector
{
    virtual bool CollectElement(Document& document) = 0;
};

/// <summary>
/// The purpose of this structure is to handle SAX events for JSON documents in the following form:
/// {
///   "array1": [],
///   "array2": [],
///   "other": {},
///   "data": 1
/// }
/// The user can specify ArrayElementCollector instances to collect array elements in a streaming fashion
/// (eg not building up a DOM in memory), but keys that do not have handlers will combine to a DOM.
/// This is meant for JSON files where the arrays with handlers contain many elements (taking memory),
/// but there the rest of the document is simple configuration.
/// </summary>
class SaxHandler : public BaseReaderHandler<UTF8<>, SaxHandler>
{
    std::map<std::string, ArrayElementCollector*> handlers;
    ArrayElementCollector* collector = nullptr;
    bool inArray = false;
    int objectNesting = 0;
    int arrayNesting = 0;
    Document element;

public:
    bool Null() { return inArray ? element.Null() : true; }
    bool Bool(bool b) { return inArray ? element.Bool(b) : true; }
    bool Int(int i) { return inArray ? element.Int(i) : true; }
    bool Uint(unsigned u) { return inArray ? element.Uint(u) : true; }
    bool Int64(int64_t i) { return inArray ? element.Int64(i) : true; }
    bool Uint64(uint64_t u) { return inArray ? element.Uint64(u) : true; }
    bool Double(double d) { return inArray ? element.Double(d) : true; }
    bool String(const char* str, SizeType length, bool copy) { return inArray ? element.String(str, length, copy) : true; }

    bool Key(const char* str, SizeType length, bool copy)
    {
        if(inArray)
            return element.Key(str, length, copy);
        if(objectNesting == 1 && arrayNesting == 0)
        {
            string key(str, length);
            auto itr = handlers.find(key);
            if(itr != handlers.end())
                collector = itr->second;
            else
                collector = nullptr;
        }
        return true;
    }

    bool StartObject()
    {
        objectNesting++;
        return inArray ? element.StartObject() : true;
    }

    bool EndObject(SizeType memberCount)
    {
        objectNesting--;
        if(inArray)
        {
            if(element.EndObject(memberCount))
            {
                if(objectNesting == 1)
                {
                    auto g = [](Document&) { return true; };
                    element.Populate(g);
                    if(!collector->CollectElement(element))
                        return false;
                    element.SetNull();
                }
                return true;
            }
            return false;
        }
        return true;
    }

    bool StartArray()
    {
        arrayNesting++;
        if(inArray)
            return element.StartArray();
        if(objectNesting == 1 && arrayNesting == 1 && collector != nullptr)
            inArray = true;
        return true;
    }

    bool EndArray(SizeType elementCount)
    {
        if(arrayNesting == 1 && objectNesting == 1)
            inArray = false;
        arrayNesting--;
        return inArray ? element.EndArray(elementCount) : true;
    }

    void SetArrayCollector(const std::string& key, ArrayElementCollector* collector)
    {
        handlers[key] = collector;
    }
};

struct Stopwatch
{
    Stopwatch() { start(); }
    ~Stopwatch() { dump(); }
    void start()
    {
        t = GetTickCount();
    }
    void stop()
    {
        e = GetTickCount() - t;
    }
    void dump()
    {
        stop();
        fprintf(stderr, "%ums\n", e);
        start();
    }
    DWORD t;
    DWORD e;
};

void readfile(const char* f)
{
    fprintf(stderr, "readfile ");
    Stopwatch w;
    auto h = CreateFileA(f, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    vector<unsigned char> v(GetFileSize(h, nullptr));
    DWORD r = 0;
    ReadFile(h, v.data(), v.size(), &r, nullptr);
    CloseHandle(h);
}

struct XrefsCollector : ArrayElementCollector
{
    Document xrefs;

    XrefsCollector()
    {
        xrefs.SetArray();
    }

    bool CollectElement(Document & document) override
    {
        xrefs.PushBack(Value(document, xrefs.GetAllocator()), xrefs.GetAllocator());
        return true;
    }
} xrefs;

int main(int argc, char* argv[])
{
    if(argc < 2)
        return 1;
    //readfile(argv[1]);
    {
        Stopwatch t;
        SaxHandler handler;
        handler.SetArrayCollector("xrefs", &xrefs);
        Reader reader;
        ifstream s = ifstream(argv[1]);
        vector<char> sbuf(1024 * 1024 * 1);
        IStreamWrapper ss(s, sbuf.data(), sbuf.size());
        reader.Parse(ss, handler);
    }
    puts(serialize(xrefs.xrefs).c_str());
}