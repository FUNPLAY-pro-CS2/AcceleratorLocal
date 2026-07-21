#pragma once
#include <json.hpp>
#include <steam/isteamhttp.h>
#include <steam/steam_gameserver.h>
#include <functional>
#include <vector>

using json = nlohmann::json;

#define CompletedCallback std::function<void(HTTPRequestHandle, json)>
#define ErrorCallback std::function<void(HTTPRequestHandle, EHTTPStatusCode, json)>

class HTTPHeader
{
public:
    HTTPHeader(std::string strName, std::string strValue)
    {
        m_strName = strName;
        m_strValue = strValue;
    }

    const char* GetName() const { return m_strName.c_str(); }
    const char* GetValue() const { return m_strValue.c_str(); }

private:
    std::string m_strName;
    std::string m_strValue;
};

class HTTPManager
{
public:
    void Get(const char* pszUrl, CompletedCallback callbackCompleted, ErrorCallback callbackError = nullptr, std::vector<HTTPHeader>* headers = nullptr);
    void Post(const char* pszUrl, const char* pszText, CompletedCallback callbackCompleted, ErrorCallback callbackError = nullptr, std::vector<HTTPHeader>* headers = nullptr);
    void Put(const char* pszUrl, const char* pszText, CompletedCallback callbackCompleted, ErrorCallback callbackError = nullptr, std::vector<HTTPHeader>* headers = nullptr);
    void Patch(const char* pszUrl, const char* pszText, CompletedCallback callbackCompleted, ErrorCallback callbackError = nullptr, std::vector<HTTPHeader>* headers = nullptr);
    void Delete(const char* pszUrl, const char* pszText, CompletedCallback callbackCompleted, ErrorCallback callbackError = nullptr, std::vector<HTTPHeader>* headers = nullptr);

    bool HasAnyPendingRequests() const { return m_PendingRequests.size() > 0; }

    void DrainQueue();

    static void GenerateRequestOverride(EHTTPMethod method, const char* pszUrl, uint8* data, int size, const char* contentType, std::function<void(HTTPRequestHandle, json)> callbackCompleted, std::function<void(HTTPRequestHandle, EHTTPStatusCode, json)> callbackError, std::vector<HTTPHeader>* headers);

private:
    struct QueuedRequest
    {
        EHTTPMethod method;
        std::string url;
        std::string body;
        CompletedCallback callback;
        ErrorCallback errorCallback;
        std::vector<HTTPHeader> headers;
    };

    class TrackedRequest
    {
    public:
        TrackedRequest(const TrackedRequest& req) = delete;
        TrackedRequest(HTTPRequestHandle hndl, SteamAPICall_t hCall, std::string strUrl, std::string strText, CompletedCallback callbackCompleted, ErrorCallback callbackError);

        ~TrackedRequest();

    private:
        void OnHTTPRequestCompleted(HTTPRequestCompleted_t* arg, bool bFailed);

        HTTPRequestHandle m_hHTTPReq;
        CCallResult<TrackedRequest, HTTPRequestCompleted_t> m_CallResult;
        std::string m_strUrl;
        std::string m_strText;
        CompletedCallback m_callbackCompleted;
        ErrorCallback m_callbackError;
    };

private:
    std::vector<TrackedRequest*> m_PendingRequests;
    std::vector<QueuedRequest> m_QueuedRequests;

    void GenerateRequest(EHTTPMethod method, const char* pszUrl, const char* pszText, CompletedCallback callbackCompleted, ErrorCallback callbackError, std::vector<HTTPHeader>* headers);
};
