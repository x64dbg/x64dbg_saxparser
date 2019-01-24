# x64dbg_saxparser

Proof of concept JSON parsing strategy for x64dbg that will drastically improve performance and memory usage.

Example code:

```c++
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
```
