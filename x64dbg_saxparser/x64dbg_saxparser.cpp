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

struct EmptyHandler : public BaseReaderHandler<UTF8<>>
{
    std::function<void(const GenericDocument<UTF8<>>&)> cb;
};

struct ArrayElementCollector
{
    virtual bool CollectElement(Document& document) = 0;
};

class ArrayHandler : public BaseReaderHandler<UTF8<>, ArrayHandler>
{
    int objectNesting = 0;
    Document document;
    ArrayElementCollector* collector = nullptr;

public:
    ArrayHandler() { }
    ArrayHandler(ArrayElementCollector* collector) : collector(collector) { }

    bool Null() { return document.Null(); }
    bool Bool(bool b) { return document.Bool(b); }
    bool Int(int i) { return document.Int(i); }
    bool Uint(unsigned u) { return document.Uint(u); }
    bool Int64(int64_t i) { return document.Int64(i); }
    bool Uint64(uint64_t u) { return document.Uint64(u); }
    bool Double(double d) { return document.Double(d); }
    bool String(const char* str, SizeType length, bool copy) { return document.String(str, length, copy); }
    bool Key(const char* str, SizeType length, bool copy) { return document.Key(str, length, copy); }
    bool StartArray() { return document.StartArray(); }
    bool EndArray(SizeType elementCount) { return document.EndArray(elementCount); }

    bool StartObject()
    {
        objectNesting++;
        return document.StartObject();
    }

    bool EndObject(SizeType memberCount)
    {
        objectNesting--;
        if(document.EndObject(memberCount))
        {
            if(objectNesting == 0)
            {
                auto g = [](Document&) { return true; };
                document.Populate(g);
                if(!collector->CollectElement(document))
                    return false;
                document.SetNull();
            }
            return true;
        }
        return false;
    }
};

class SaxHandler : public BaseReaderHandler<UTF8<>, SaxHandler>
{
    std::map<std::string, ArrayHandler> handlers;
    ArrayHandler* h = nullptr;
    bool inArray = false;
    int objectNesting = 0;
    int arrayNesting = 0;

public:
    bool Null() { return inArray ? h->Null() : true; }
    bool Bool(bool b) { return inArray ? h->Bool(b) : true; }
    bool Int(int i) { return inArray ? h->Int(i) : true; }
    bool Uint(unsigned u) { return inArray ? h->Uint(u) : true; }
    bool Int64(int64_t i) { return inArray ? h->Int64(i) : true; }
    bool Uint64(uint64_t u) { return inArray ? h->Uint64(u) : true; }
    bool Double(double d) { return inArray ? h->Double(d) : true; }
    bool String(const char* str, SizeType length, bool copy) { return inArray ? h->String(str, length, copy) : true; }

    bool Key(const char* str, SizeType length, bool copy)
    {
        if(inArray)
            return h->Key(str, length, copy);
        if(objectNesting == 1 && arrayNesting == 0)
        {
            string key(str, length);
            auto itr = handlers.find(key);
            if(itr != handlers.end())
                h = &itr->second;
            else
                h = nullptr;
        }
        return true;
    }

    bool StartObject()
    {
        objectNesting++;
        return inArray ? h->StartObject() : true;
    }

    bool EndObject(SizeType memberCount)
    {
        objectNesting--;
        return inArray ? h->EndObject(memberCount) : true;
    }

    bool StartArray()
    {
        arrayNesting++;
        if(inArray)
            return h->StartArray();
        if(objectNesting == 1 && arrayNesting == 1 && h != nullptr)
            inArray = true;
        return true;
    }

    bool EndArray(SizeType elementCount)
    {
        if(arrayNesting == 1 && objectNesting == 1)
            inArray = false;
        arrayNesting--;
        return inArray ? h->EndArray(elementCount) : true;
    }

    void SetArrayCollector(const std::string& key, ArrayElementCollector* collector)
    {
        handlers[key] = ArrayHandler(collector);
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