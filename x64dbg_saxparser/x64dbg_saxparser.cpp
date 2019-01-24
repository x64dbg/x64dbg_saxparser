#include <rapidjson/reader.h>
#include <rapidjson/writer.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <vector>
#include <functional>
#include <map>
#include <windows.h>

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
    Document document;

public:
    std::string doc()
    {
        auto g = [](Document&) { return true; };
        document.Populate(g);
        return serialize(document);
    }

    bool Null() { return inArray ? element.Null() : document.Null(); }
    bool Bool(bool b) { return inArray ? element.Bool(b) : document.Bool(b); }
    bool Int(int i) { return inArray ? element.Int(i) : document.Int(i); }
    bool Uint(unsigned u) { return inArray ? element.Uint(u) : document.Uint(u); }
    bool Int64(int64_t i) { return inArray ? element.Int64(i) : document.Int64(i); }
    bool Uint64(uint64_t u) { return inArray ? element.Uint64(u) : document.Uint64(u); }
    bool Double(double d) { return inArray ? element.Double(d) : document.Double(d); }
    bool String(const char* str, SizeType length, bool copy) { return inArray ? element.String(str, length, copy) : document.String(str, length, copy); }

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
        return document.Key(str, length, copy);
    }

    bool StartObject()
    {
        objectNesting++;
        return inArray ? element.StartObject() : document.StartObject();
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
        return document.EndObject(memberCount);
    }

    bool StartArray()
    {
        arrayNesting++;
        if(inArray)
            return element.StartArray();
        if(objectNesting == 1 && arrayNesting == 1 && collector != nullptr)
            inArray = true;
        return document.StartArray();
    }

    bool EndArray(SizeType elementCount)
    {
        if(inArray && arrayNesting == 1 && objectNesting == 1)
        {
            inArray = false;
            elementCount = 0;
        }
        arrayNesting--;
        return inArray ? element.EndArray(elementCount) : document.EndArray(elementCount);
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
    ReadFile(h, v.data(), (DWORD)v.size(), &r, nullptr);
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

#define QT_TRANSLATE_NOOP(ctx, str) str

static const char* GetParseErrorString(ParseErrorCode code)
{
    switch(code)
    {
    case kParseErrorDocumentEmpty: return QT_TRANSLATE_NOOP("DBG", "The document is empty.");
    case kParseErrorDocumentRootNotSingular: return QT_TRANSLATE_NOOP("DBG", "The document root must not be followed by other values.");
    case kParseErrorValueInvalid: return QT_TRANSLATE_NOOP("DBG", "Invalid value.");
    case kParseErrorObjectMissName: return QT_TRANSLATE_NOOP("DBG", "Missing a name for object member.");
    case kParseErrorObjectMissColon: return QT_TRANSLATE_NOOP("DBG", "Missing a colon after a name of object member.");
    case kParseErrorObjectMissCommaOrCurlyBracket: return QT_TRANSLATE_NOOP("DBG", "Missing a comma or '}' after an object member.");
    case kParseErrorArrayMissCommaOrSquareBracket: return QT_TRANSLATE_NOOP("DBG", "Missing a comma or ']' after an array element.");
    case kParseErrorStringUnicodeEscapeInvalidHex: return QT_TRANSLATE_NOOP("DBG", "Incorrect hex digit after \\u escape in string.");
    case kParseErrorStringUnicodeSurrogateInvalid: return QT_TRANSLATE_NOOP("DBG", "The surrogate pair in string is invalid.");
    case kParseErrorStringEscapeInvalid: return QT_TRANSLATE_NOOP("DBG", "Invalid escape character in string.");
    case kParseErrorStringMissQuotationMark: return QT_TRANSLATE_NOOP("DBG", "Missing a closing quotation mark in string.");
    case kParseErrorStringInvalidEncoding: return QT_TRANSLATE_NOOP("DBG", "Invalid encoding in string.");
    case kParseErrorNumberTooBig: return QT_TRANSLATE_NOOP("DBG", "Number too big to be stored in double.");
    case kParseErrorNumberMissFraction: return QT_TRANSLATE_NOOP("DBG", "Miss fraction part in number.");
    case kParseErrorNumberMissExponent: return QT_TRANSLATE_NOOP("DBG", "Miss exponent in number.");
    case kParseErrorTermination: return QT_TRANSLATE_NOOP("DBG", "Parsing was terminated.");
    case kParseErrorUnspecificSyntaxError: return QT_TRANSLATE_NOOP("DBG", "Unspecific syntax error.");
    default: return "";
    }
}

int main(int argc, char* argv[])
{
    if(argc < 2)
        return 1;
    //readfile(argv[1]);
    {
        SaxHandler handler;
        {
            Stopwatch t;
            handler.SetArrayCollector("xrefs", &xrefs);
            Reader reader;
            FILE* fp;
            fopen_s(&fp, argv[1], "rb");
            vector<char> sbuf(1024 * 1024);
            FileReadStream ss(fp, sbuf.data(), sbuf.size());
            ParseResult parseResult = reader.Parse(ss, handler);
            fclose(fp);
            if(!parseResult)
            {
                fprintf(stderr, "JSON parse error: %s Offset: %u\n",
                    GetParseErrorString(parseResult.Code()),
                    parseResult.Offset());
                return EXIT_FAILURE;
            }
        }
        fprintf(stderr, "%s\n", handler.doc().c_str());
    }
    puts(serialize(xrefs.xrefs).c_str());
    return EXIT_SUCCESS;
}