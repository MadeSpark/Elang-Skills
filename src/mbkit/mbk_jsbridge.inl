/* Included after WV/ExecState. The synchronous COM bridge preserves the native
   miniblink contract: JS arguments -> native callback -> jsValue return value. */
struct JsBinding {
    unsigned fn, data;
    bool extended;
    void* allocation = nullptr;
};
static void* binding_allocation(unsigned address)
{
    MEMORY_BASIC_INFORMATION info = {};
    if (!address || !VirtualQuery((void*)(UINT_PTR)address, &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return nullptr;
    return info.AllocationBase;
}
static std::map<std::wstring, JsBinding>& js_bindings()
{
    static auto* bindings = new std::map<std::wstring, JsBinding>();
    return *bindings;
}

static std::wstring js_quote(const std::wstring& text)
{
    std::wstring result = L"\"";
    for (wchar_t c : text) {
        wchar_t hex[7];
        swprintf(hex, 7, L"\\u%04x", (unsigned)c);
        result += hex;
    }
    return result + L"\"";
}

class NativeFunction : public IDispatch {
    LONG refs = 1;
    unsigned view;
    JsBinding binding;
public:
    NativeFunction(unsigned id, const JsBinding& b) : view(id), binding(b) {}
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = InterlockedDecrement(&refs); if (!n) delete this; return n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_IDispatch) return E_NOINTERFACE;
        *out = static_cast<IDispatch*>(this); AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* n) override {
        if (!n) return E_POINTER; *n = 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR* names, UINT n,
                                            LCID, DISPID* ids) override {
        for (UINT i = 0; i < n; ++i) {
            if (wcscmp(names[i], L"invoke")) return DISP_E_UNKNOWNNAME;
            ids[i] = 1;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID, LCID, WORD flags,
        DISPPARAMS* params, VARIANT* result, EXCEPINFO*, UINT*) override {
        if (id != 1 || !(flags & DISPATCH_METHOD)) return DISP_E_MEMBERNOTFOUND;
        WV* w = view_by_id(view);
        if (!w || !binding.fn) return RPC_E_DISCONNECTED;
        /* Pin the business module for the duration of the native call. Extended
           bindings pass an allocated ABI trampoline as fn and the E callback as data. */
        HMODULE owner = nullptr;
        unsigned address = binding.extended ? binding.data : binding.fn;
        if (!binding.allocation || binding_allocation(address) != binding.allocation ||
            !binding_allocation(binding.fn)) return RPC_E_DISCONNECTED;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                               (LPCWSTR)(UINT_PTR)address, &owner)) owner = nullptr;
        ExecState state;
        state.magic = WV_EXEC_MAGIC; state.wv = w;
        state.args.resize(params ? params->cArgs : 0);
        for (size_t i = 0; i < state.args.size(); ++i) {
            const VARIANT& value = params->rgvarg[state.args.size() - 1 - i];
            Jsv& arg = state.args[i];
            switch (value.vt) {
            case VT_EMPTY: arg.kind = 0; break;
            case VT_NULL: arg.kind = 1; break;
            case VT_BOOL: arg.kind = 2; arg.b = value.boolVal != VARIANT_FALSE;
                arg.d = arg.b; arg.ws = arg.b ? L"true" : L"false"; break;
            case VT_BSTR: arg.kind = 5;
                if (value.bstrVal) arg.ws.assign(value.bstrVal, SysStringLen(value.bstrVal)); break;
            default: {
                VARIANT number; VariantInit(&number);
                if (FAILED(VariantChangeType(&number, const_cast<VARIANT*>(&value), 0, VT_R8))) {
                    if (owner) FreeLibrary(owner); return DISP_E_TYPEMISMATCH;
                }
                arg.kind = 4; arg.d = number.dblVal;
                wchar_t buf[64]; swprintf(buf, 64, L"%.17g", arg.d); arg.ws = buf;
                VariantClear(&number); break;
            }
            }
            arg.u8 = w2utf8(arg.ws.c_str(), (int)arg.ws.size());
        }
        long long value = binding.extended
            ? ((long long (__cdecl*)(unsigned, unsigned))(UINT_PTR)binding.fn)
                ((unsigned)(UINT_PTR)&state, binding.data)
            : ((long long (__cdecl*)(unsigned))(UINT_PTR)binding.fn)((unsigned)(UINT_PTR)&state);
        if (result) {
            VariantInit(result);
            Jsv* js = value ? jsv_of((unsigned)value) : nullptr;
            if (js) switch (js->kind) {
            case 1: result->vt = VT_NULL; break;
            case 2: result->vt = VT_BOOL; result->boolVal = js->b ? VARIANT_TRUE : VARIANT_FALSE; break;
            case 3: case 4: result->vt = VT_R8; result->dblVal = js->d; break;
            case 5: result->vt = VT_BSTR;
                result->bstrVal = SysAllocStringLen(js->ws.data(), (UINT)js->ws.size()); break;
            }
        }
        if (owner) FreeLibrary(owner);
        return S_OK;
    }
};

static void install_binding(WV* w, const std::wstring& name, const JsBinding& b)
{
    if (!w->core) return;
    std::wstring hostName = L"mbkit_" + name;
    VARIANT value; VariantInit(&value); value.vt = VT_DISPATCH;
    value.pdispVal = new NativeFunction(view_id(w), b);
    HRESULT hr = w->core->AddHostObjectToScript(hostName.c_str(), &value);
    VariantClear(&value);
    if (FAILED(hr)) { diagf("AddHostObjectToScript hr=0x%08lX", (unsigned long)hr); return; }
    std::wstring script = L"window[" + js_quote(name) +
        L"]=(...args)=>chrome.webview.hostObjects.sync[" + js_quote(hostName) + L"].invoke(...args);";
    w->core->AddScriptToExecuteOnDocumentCreated(script.c_str(), nullptr);
    w->core->ExecuteScript(script.c_str(), nullptr);
}

extern "C" void mbkwv_jsBindFunction(unsigned name, unsigned fn, unsigned count)
{
    (void)count;
    if (!name) return;
    auto key = utf8w((const char*)(UINT_PTR)name);
    JsBinding b = {fn, 0, false}; js_bindings()[key] = b;
    b.allocation = binding_allocation(fn); js_bindings()[key] = b;
    for (const auto& entry : g_liveViews) install_binding(entry.second, key, b);
}
extern "C" void mbkwv_wkeJsBindFunction(unsigned name, unsigned fn, unsigned data, unsigned count)
{
    (void)count;
    if (!name) return;
    auto key = utf8w((const char*)(UINT_PTR)name);
    JsBinding b = {fn, data, true}; js_bindings()[key] = b;
    b.allocation = binding_allocation(data); js_bindings()[key] = b;
    for (const auto& entry : g_liveViews) install_binding(entry.second, key, b);
}
extern "C" unsigned mbkwv_jsArgCount(unsigned es)
{
    auto* e = (ExecState*)(UINT_PTR)es;
    return e && e->magic == WV_EXEC_MAGIC ? (unsigned)e->args.size() : 0;
}
extern "C" long long mbkwv_jsArg(unsigned es, unsigned index)
{
    auto* e = (ExecState*)(UINT_PTR)es;
    return index < mbkwv_jsArgCount(es) ? (long long)(UINT_PTR)&e->args[index] : mbkwv_jsUndefined();
}
extern "C" unsigned mbkwv_jsTypeOf(long long value)
{
    Jsv* p = jsv_of((unsigned)value);
    if (!p) return 5;
    switch (p->kind) { case 1: return 7; case 2: return 3;
        case 3: case 4: return 0; case 5: return 1; default: return 5; }
}
extern "C" unsigned mbkwv_jsArgType(unsigned es, unsigned index)
{ return mbkwv_jsTypeOf(mbkwv_jsArg(es, index)); }
extern "C" double mbkwv_jsToDouble(unsigned es, long long value)
{
    (void)es; Jsv* p = jsv_of((unsigned)value);
    return !p ? 0 : p->kind == 5 ? wcstod(p->ws.c_str(), nullptr) : p->d;
}
extern "C" float mbkwv_jsToFloat(unsigned es, long long value)
{ return (float)mbkwv_jsToDouble(es, value); }
extern "C" unsigned mbkwv_jsToInt(unsigned es, long long value)
{ return (unsigned)(int)mbkwv_jsToDouble(es, value); }
extern "C" unsigned mbkwv_jsToBoolean(unsigned es, long long value)
{
    (void)es; Jsv* p = jsv_of((unsigned)value);
    return p && (p->kind == 5 ? !p->ws.empty() : p->d != 0);
}
