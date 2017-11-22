// +------------------------------------------------------------------+
// |             ____ _               _        __  __ _  __           |
// |            / ___| |__   ___  ___| | __   |  \/  | |/ /           |
// |           | |   | '_ \ / _ \/ __| |/ /   | |\/| | ' /            |
// |           | |___| | | |  __/ (__|   <    | |  | | . \            |
// |            \____|_| |_|\___|\___|_|\_\___|_|  |_|_|\_\           |
// |                                                                  |
// | Copyright Mathias Kettner 2017             mk@mathias-kettner.de |
// +------------------------------------------------------------------+
//
// This file is part of Check_MK.
// The official homepage is at http://mathias-kettner.de/check_mk.
//
// check_mk is free software;  you can redistribute it and/or modify it
// under the  terms of the  GNU General Public License  as published by
// the Free Software Foundation in version 2.  check_mk is  distributed
// in the hope that it will be useful, but WITHOUT ANY WARRANTY;  with-
// out even the implied warranty of  MERCHANTABILITY  or  FITNESS FOR A
// PARTICULAR PURPOSE. See the  GNU General Public License for more de-
// ails.  You should have  received  a copy of the  GNU  General Public
// License along with GNU Make; see the file  COPYING.  If  not,  write
// to the Free Software Foundation, Inc., 51 Franklin St,  Fifth Floor,
// Boston, MA 02110-1301 USA.

#include "wmiHelper.h"

#include <comdef.h>
#include <oleauto.h>
#include <ios>
#include <iostream>
#include <sstream>
#include <string>

#include "Logger.h"
#include "stringutil.h"

using namespace wmi;
using namespace std;

std::string ComException::resolveError(HRESULT result,
                                       const WinApiAdaptor &winapi) {
    switch (static_cast<ULONG>(result)) {
        case WBEM_E_INVALID_NAMESPACE:
            return string("Invalid Namespace");
        case WBEM_E_ACCESS_DENIED:
            return string("Access Denied");
        case WBEM_E_INVALID_CLASS:
            return string("Invalid Class");
        case WBEM_E_INVALID_QUERY:
            return string("Invalid Query");
        default:
            return _com_error(result, getErrorInfo(winapi)).ErrorMessage();
    }
}

ComException::ComException(const string &message, HRESULT result,
                           const WinApiAdaptor &winapi)
    : runtime_error(message + string(": ") + resolveError(result, winapi) +
                    string(" (") + toStringHex(result) + string(")")) {}

ComTypeException::ComTypeException(const string &message)
    : runtime_error(message) {}

IErrorInfo *ComException::getErrorInfo(const WinApiAdaptor &winapi) {
    IErrorInfo *result;
    winapi.GetErrorInfo(0, &result);
    return result;
}

string ComException::toStringHex(HRESULT res) {
    ostringstream out;
    out << hex << res;
    return out.str();
}

Variant::Variant(const VARIANT &val, const WinApiAdaptor &winapi)
    : _value(val), _winapi(winapi) {}

Variant::~Variant() { _winapi.VariantClear(&_value); }

void releaseInterface(IUnknown *ptr) {
    if (ptr != nullptr) {
        ptr->Release();
    }
}

ObjectWrapper::ObjectWrapper(IWbemClassObject *object,
                             const WinApiAdaptor &winapi)
    : _current(object, releaseInterface), _winapi(winapi) {}

ObjectWrapper::ObjectWrapper(const ObjectWrapper &reference)
    : _current(reference._current), _winapi(reference._winapi) {}

bool ObjectWrapper::contains(const wchar_t *key) const {
    VARIANT value;
    HRESULT res = _current->Get(key, 0, &value, nullptr, nullptr);
    if (FAILED(res)) {
        return false;
    }
    bool not_null = value.vt != VT_NULL;
    _winapi.VariantClear(&value);
    return not_null;
}

int ObjectWrapper::typeId(const wchar_t *key) const {
    VARIANT value;
    HRESULT res = _current->Get(key, 0, &value, nullptr, nullptr);
    if (FAILED(res)) {
        return 0;
    }
    int type_id = value.vt;
    _winapi.VariantClear(&value);
    return type_id;
}

ObjectWrapper::~ObjectWrapper() noexcept {}

VARIANT ObjectWrapper::getVarByKey(const wchar_t *key) const {
    VARIANT value;
    HRESULT res = _current->Get(key, 0, &value, nullptr, nullptr);
    if (FAILED(res)) {
        throw ComException(
            std::string("Failed to retrieve key: ") + to_utf8(key), res,
            _winapi);
    }

    return value;
}

Result::Result(const WinApiAdaptor &winapi)
    : ObjectWrapper(nullptr, winapi), _enumerator(nullptr, releaseInterface) {}

Result::Result(const Result &reference)
    : ObjectWrapper(reference)
    , _enumerator(reference._enumerator)
    , _last_error(reference._last_error) {}

Result::Result(IEnumWbemClassObject *enumerator, const WinApiAdaptor &winapi)
    : ObjectWrapper(nullptr, winapi)
    , _enumerator(enumerator, releaseInterface) {
    if (!next()) {
        // if the first enumeration fails the result is empty
        // we abstract away two possible reasons:
        //   a) The class doesn't exist at all
        //   b) The result is indeed empty
        _enumerator = nullptr;
    }
}

Result::~Result() noexcept {}

Result &Result::operator=(const Result &reference) {
    if (&reference != this) {
        if (_enumerator != nullptr) {
            _enumerator->Release();
        }
        _enumerator = reference._enumerator;
        _current = reference._current;
        _last_error = reference._last_error;
    }
    return *this;
}

bool Result::valid() const { return _current.get() != nullptr; }

vector<wstring> Result::names() const {
    vector<wstring> result;
    SAFEARRAY *names = nullptr;
    HRESULT res = _current->GetNames(
        nullptr, WBEM_FLAG_ALWAYS | WBEM_FLAG_NONSYSTEM_ONLY, nullptr, &names);

    if (FAILED(res)) {
        throw ComException("Failed to retrieve field names", res, _winapi);
    }

    long lLower, lUpper;
    BSTR propName = nullptr;
    _winapi.SafeArrayGetLBound(names, 1, &lLower);
    _winapi.SafeArrayGetUBound(names, 1, &lUpper);

    for (long i = lLower; i <= lUpper; ++i) {
        res = _winapi.SafeArrayGetElement(names, &i, &propName);
        result.push_back(wstring(propName));
        _winapi.SysFreeString(propName);
    }

    _winapi.SafeArrayDestroy(names);
    return result;
}

bool Result::next() {
    if (_enumerator == nullptr) {
        return false;
    }

    IWbemClassObject *obj = nullptr;
    ULONG numReturned = 0;
    // always retrieve only one element
    HRESULT res = _enumerator->Next(2500, 1, &obj, &numReturned);

    switch (res) {
        case WBEM_NO_ERROR:
            _current.reset(obj, releaseInterface);
            return true;
        case WBEM_S_FALSE:
            // No more values. The current object remains at the last element so
            // that a call to get continues to work.
            return false;
        case WBEM_S_TIMEDOUT:
            // A timeout occurred before getting the object.
            throw Timeout("WMItimeout");
        default:
            // Any of the four possible errors: WBEM_E_INVALID_PARAMETER,
            // WBEM_E_OUT_OF_MEMORY, WBEM_E_UNEXPECTED or
            // WBEM_E_TRANSPORT_FAILURE. In this case the "current" object isn't
            // changed to guarantee that the Result remains valid.
            _last_error = res;
            return false;
    }
}

template <>
int Variant::get() const {
    switch (_value.vt) {
        case VT_I1:
            return _value.bVal;
        case VT_I2:
            return _value.iVal;
        case VT_I4:
            return _value.intVal;
        case VT_UI1:
            return _value.cVal;
        case VT_UI2:
            return _value.uiVal;
        case VT_UI4:
            return _value.intVal;
        default:
            throw ComTypeException(string("wrong value type requested: ") +
                                   to_string(_value.vt));
    }
}

template <>
bool Variant::get() const {
    switch (_value.vt) {
        case VT_BOOL:
            return _value.boolVal;
        default:
            throw ComTypeException(string("wrong value type requested: ") +
                                   to_string(_value.vt));
    }
}

template <>
ULONG Variant::get() const {
    switch (_value.vt) {
        case VT_UI1:
            return _value.cVal;
        case VT_UI2:
            return _value.uiVal;
        case VT_UI4:
            return _value.ulVal;
        default:
            throw ComTypeException(string("wrong value type requested: ") +
                                   to_string(_value.vt));
    }
}

template <>
ULONGLONG Variant::get() const {
    switch (_value.vt) {
        case VT_UI8:
            return _value.ullVal;
        default:
            throw ComTypeException(string("wrong value type requested: ") +
                                   to_string(_value.vt));
    }
}

template <>
string Variant::get() const {
    switch (_value.vt) {
        case VT_BSTR:
            return to_utf8(_value.bstrVal);
        default:
            throw ComTypeException(string("wrong value type requested: ") +
                                   to_string(_value.vt));
    }
}

template <>
float Variant::get() const {
    switch (_value.vt) {
        case VT_R4:
            return _value.fltVal;
        default:
            throw ComTypeException(string("wrong value type requested: ") +
                                   to_string(_value.vt));
    }
}

template <>
double Variant::get() const {
    switch (_value.vt) {
        case VT_R4:
            return _value.fltVal;
        case VT_R8:
            return _value.dblVal;
        default:
            throw ComTypeException(string("wrong value type requested: ") +
                                   to_string(_value.vt));
    }
}

VARTYPE Variant::type() const { return _value.vt; }

template <>
wstring Variant::get() const {
    if (_value.vt & VT_ARRAY) {
        return L"<array>";
    }
    if (_value.vt & VT_VECTOR) {
        return L"<vector>";
    }

    switch (_value.vt) {
        case VT_BSTR:
            return wstring(_value.bstrVal);
        case VT_R4:
            return std::to_wstring(get<float>());
        case VT_R8:
            return std::to_wstring(get<double>());
        case VT_I1:
        case VT_I2:
        case VT_I4:
            return std::to_wstring(get<int>());
        case VT_UI1:
        case VT_UI2:
        case VT_UI4:
            return std::to_wstring(get<ULONG>());
        case VT_UI8:
            return std::to_wstring(get<ULONGLONG>());
        case VT_BOOL:
            return std::to_wstring(get<bool>());
        case VT_NULL:
            return L"";
        default:
            throw ComTypeException(string("wrong value type requested: ") +
                                   to_string(_value.vt));
    }
}

class COMManager {
public:
    static void init(const WinApiAdaptor &winapi) {
        // this is apparently thread safe in C++11 and in gcc even before that
        // see §6.4 in C++11 standard or §6.7 in C++14
        static COMManager s_Instance(winapi);
    }

    ~COMManager() { _winapi.CoUninitialize(); }

private:
    explicit COMManager(const WinApiAdaptor &winapi) : _winapi(winapi) {
        // Dr.Memory reports a memory leak here, despite the fact CoUninitialize
        // does get called. Am I doing something wrong?
        HRESULT res = _winapi.CoInitializeEx(0, COINIT_MULTITHREADED);
        if (FAILED(res)) {
            throw ComException("Failed to initialize COM", res, _winapi);
        }

        res = _winapi.CoInitializeSecurity(
            nullptr,                      // security descriptor
            -1,                           // authentication
            nullptr,                      // authentication services
            nullptr,                      // reserved
            RPC_C_AUTHN_LEVEL_DEFAULT,    // authentication level
            RPC_C_IMP_LEVEL_IMPERSONATE,  // impersonation level
            nullptr,                      // authentication info
            EOAC_NONE,                    // additional capabilities
            nullptr                       // reserved
            );
        if (FAILED(res)) {
            throw ComException("Failed to initialize COM security", res,
                               _winapi);
        }
    }

private:
    const WinApiAdaptor &_winapi;
};

Helper::Helper(const WinApiAdaptor &winapi, LPCWSTR path)
    : _locator(nullptr), _path(path), _winapi(winapi) {
    COMManager::init(winapi);

    _locator = getWBEMLocator();
    _services = connectServer(_locator);
}

Helper::~Helper() {
    if (_locator != nullptr) {
        _locator->Release();
    }
    if (_services != nullptr) {
        _services->Release();
    }
}

IWbemLocator *Helper::getWBEMLocator() {
    IWbemLocator *locator = nullptr;

    HRESULT res =
        _winapi.CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                                 IID_IWbemLocator, (LPVOID *)&locator);

    if (FAILED(res)) {
        throw ComException("Failed to create locator object", res, _winapi);
    }
    return locator;
}

IWbemServices *Helper::connectServer(IWbemLocator *locator) {
    IWbemServices *services = nullptr;

    HRESULT res = locator->ConnectServer(BSTR(_path.c_str()),  // WMI path
                                         nullptr,              // user name
                                         nullptr,              // user password
                                         0,                    // locale
                                         0,                    // security flags
                                         0,                    // authority
                                         0,                    // context object
                                         &services             // services proxy
                                         );

    if (FAILED(res)) {
        throw ComException("Failed to connect", res, _winapi);
    }
    return services;
}

void Helper::setProxyBlanket(IWbemServices *services) {
    HRESULT res = _winapi.CoSetProxyBlanket(
        services,                     // the proxy to set
        RPC_C_AUTHN_WINNT,            // authentication service
        RPC_C_AUTHZ_NONE,             // authorization service
        nullptr,                      // server principal name
        RPC_C_AUTHN_LEVEL_CALL,       // authentication level
        RPC_C_IMP_LEVEL_IMPERSONATE,  // impersonation level
        nullptr,                      // client identity
        EOAC_NONE                     // proxy capabilities
        );

    if (FAILED(res)) {
        throw ComException("Failed to set proxy blanket", res, _winapi);
    }
}

Result Helper::query(LPCWSTR query) {
    IEnumWbemClassObject *enumerator = nullptr;
    // WBEM_FLAG_RETURN_IMMEDIATELY makes the call semi-synchronous which means
    // we can return to caller immediately, iterating the result may break until
    // data is available. WBEM_FLAG_FORWARD_ONLY allows wmi to free the memory
    // of results already iterated, thus reducing memory usage
    HRESULT res = _services->ExecQuery(
        BSTR(L"WQL"), BSTR(query),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
        &enumerator);

    if (FAILED(res)) {
        throw ComException(
            string("Failed to execute query \"") + to_utf8(query) + "\"", res,
            _winapi);
    }
    return Result(enumerator, _winapi);
}

Result Helper::getClass(LPCWSTR className) {
    IEnumWbemClassObject *enumerator = nullptr;
    HRESULT res = _services->CreateInstanceEnum(
        BSTR(className), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &enumerator);
    if (FAILED(res)) {
        throw ComException(
            string("Failed to enum class \"") + to_utf8(className) + "\"", res,
            _winapi);
    }
    return Result(enumerator, _winapi);
}

ObjectWrapper Helper::call(ObjectWrapper &result, LPCWSTR method) {
    // NOTE: currently broken and unused, only here as a starting point for
    // future implementation
    IWbemClassObject *outParams = nullptr;

    BSTR className;
    HRESULT res = result._current->GetMethodOrigin(method, &className);
    if (FAILED(res)) {
        throw ComException(
            string("Failed to determine method origin: ") + to_utf8(method),
            res, _winapi);
    }

    // please don't ask me why ExecMethod needs the method name in a writable
    // buffer...
    BSTR methodName = _winapi.SysAllocString(method);

    res = _services->ExecMethod(className, methodName, 0, nullptr,
                                result._current.get(), &outParams, nullptr);

    _winapi.SysFreeString(methodName);

    return ObjectWrapper(outParams, _winapi);
}
