#include "httpmanager.h"

#include "plugin.h"

#include <json.hpp>
#include <string>

#undef strdup

extern Plugin g_Plugin;
extern HTTPManager g_httpManager;
extern ISteamHTTP* g_pSteamHttp;

HTTPManager::TrackedRequest::TrackedRequest(HTTPRequestHandle hndl, SteamAPICall_t hCall, std::string strUrl, std::string strText, CompletedCallback callbackCompleted, ErrorCallback callbackError) {
    m_hHTTPReq = hndl;
    m_CallResult.SetGameserverFlag();
    m_CallResult.Set(hCall, this, &TrackedRequest::OnHTTPRequestCompleted);

    m_strUrl = strUrl;
    m_strText = strText;
    m_callbackCompleted = callbackCompleted;
    m_callbackError = callbackError;

    g_httpManager.m_PendingRequests.push_back(this);
}

HTTPManager::TrackedRequest::~TrackedRequest() {
    for (auto e = g_httpManager.m_PendingRequests.begin(); e != g_httpManager.m_PendingRequests.end();
         ++e) {
        if (*e == this) {
            g_httpManager.m_PendingRequests.erase(e);
            break;
        }
    }
}

void HTTPManager::TrackedRequest::OnHTTPRequestCompleted(HTTPRequestCompleted_t *arg, bool bFailed) {
    if (bFailed || (!m_callbackError && (arg->m_eStatusCode < 200 || arg->m_eStatusCode > 299))) {
        META_LOG(&g_Plugin, "g_pExceptionHandlerHTTP request to %s failed with status code %d\n", m_strUrl.c_str(), arg->m_eStatusCode);
    } else {
        uint32 size;
        g_pSteamHttp->GetHTTPResponseBodySize(arg->m_hRequest, &size);

        uint8 *response = new uint8[size + 1];
        g_pSteamHttp->GetHTTPResponseBodyData(arg->m_hRequest, response, size);
        response[size] = 0;

        json jsonResponse;

        if (V_strcmp((char*)response, "")) {
            jsonResponse = json::parse((char *) response, nullptr, false);

            if (jsonResponse.is_discarded())
                META_LOG(&g_Plugin, "g_pExceptionHandlerFailed parsing JSON from HTTP response: %s\n", (char*)response);
        }

        if (arg->m_eStatusCode < 200 || arg->m_eStatusCode > 299) {
            if (m_callbackError)
                m_callbackError(arg->m_hRequest, arg->m_eStatusCode,
                                jsonResponse.is_discarded() ? json() : jsonResponse);
        } else {
            if (m_callbackCompleted)
                m_callbackCompleted(arg->m_hRequest,
                                    jsonResponse.is_discarded() ? json() : jsonResponse);
        }

        delete[] response;
    }

    if (g_pSteamHttp)
        g_pSteamHttp->ReleaseHTTPRequest(arg->m_hRequest);

    delete this;
}

void HTTPManager::DrainQueue() {
    if (!g_pSteamHttp || m_QueuedRequests.empty())
        return;

    auto queued = std::move(m_QueuedRequests);
    m_QueuedRequests.clear();

    for (auto& q : queued)
        GenerateRequest(q.method, q.url.c_str(), q.body.c_str(), q.callback, q.errorCallback,
                        q.headers.empty() ? nullptr : &q.headers);
}

void HTTPManager::Get(const char *pszUrl, CompletedCallback callbackCompleted, ErrorCallback callbackError, std::vector<HTTPHeader> *headers) {
    GenerateRequest(k_EHTTPMethodGET, pszUrl, "", callbackCompleted, callbackError, headers);
}

void HTTPManager::Post(const char *pszUrl, const char *pszText, CompletedCallback callbackCompleted, ErrorCallback callbackError, std::vector<HTTPHeader> *headers) {
    GenerateRequest(k_EHTTPMethodPOST, pszUrl, pszText, callbackCompleted, callbackError, headers);
}

void HTTPManager::Put(const char *pszUrl, const char *pszText, CompletedCallback callbackCompleted, ErrorCallback callbackError, std::vector<HTTPHeader> *headers) {
    GenerateRequest(k_EHTTPMethodPUT, pszUrl, pszText, callbackCompleted, callbackError, headers);
}

void HTTPManager::Patch(const char *pszUrl, const char *pszText, CompletedCallback callbackCompleted, ErrorCallback callbackError, std::vector<HTTPHeader> *headers) {
    GenerateRequest(k_EHTTPMethodPATCH, pszUrl, pszText, callbackCompleted, callbackError, headers);
}

void HTTPManager::Delete(const char *pszUrl, const char *pszText, CompletedCallback callbackCompleted, ErrorCallback callbackError, std::vector<HTTPHeader> *headers) {
    GenerateRequest(k_EHTTPMethodDELETE, pszUrl, pszText, callbackCompleted, callbackError, headers);
}

void HTTPManager::GenerateRequest(EHTTPMethod method, const char *pszUrl, const char *pszText, CompletedCallback callbackCompleted, ErrorCallback callbackError, std::vector<HTTPHeader> *headers) {
    if (!g_pSteamHttp) {
        m_QueuedRequests.push_back({
            method,
            std::string(pszUrl),
            std::string(pszText),
            callbackCompleted,
            callbackError,
            headers ? *headers : std::vector<HTTPHeader>{}
        });
        return;
    }

    auto hReq = g_pSteamHttp->CreateHTTPRequest(method, pszUrl);
    int size = strlen(pszText);

    bool shouldHaveBody = method == k_EHTTPMethodPOST
                          || method == k_EHTTPMethodPATCH
                          || method == k_EHTTPMethodPUT
                          || method == k_EHTTPMethodDELETE;

    if (shouldHaveBody && !g_pSteamHttp->SetHTTPRequestRawPostBody(
            hReq, "application/json", (uint8 *) pszText, size)) {
        return;
    }

    if (headers != nullptr)
        for (HTTPHeader header: *headers)
            g_pSteamHttp->SetHTTPRequestHeaderValue(hReq, header.GetName(), header.GetValue());

    SteamAPICall_t hCall;
    g_pSteamHttp->SendHTTPRequest(hReq, &hCall);

    new TrackedRequest(hReq, hCall, pszUrl, pszText, callbackCompleted, callbackError);
}

void HTTPManager::GenerateRequestOverride(EHTTPMethod method, const char *pszUrl, uint8 *data, int size, const char *contentType, std::function<void(HTTPRequestHandle, json)> callbackCompleted, std::function<void(HTTPRequestHandle, EHTTPStatusCode, json)> callbackError, std::vector<HTTPHeader> *headers) {
    if (!g_pSteamHttp) {
        META_LOG(&g_Plugin, "g_pExceptionHandlerSteam HTTP is not available.\n");
        return;
    }

    auto hReq = g_pSteamHttp->CreateHTTPRequest(method, pszUrl);

    if (data && size > 0 && !g_pSteamHttp->SetHTTPRequestRawPostBody(hReq, contentType, data, size)) {
        META_LOG(&g_Plugin, "g_pExceptionHandlerFailed to set raw POST body for: %s\n", pszUrl);
        return;
    }

    if (headers) {
        for (const auto &header: *headers)
            g_pSteamHttp->SetHTTPRequestHeaderValue(hReq, header.GetName(), header.GetValue());
    }

    SteamAPICall_t hCall;
    g_pSteamHttp->SendHTTPRequest(hReq, &hCall);

    new TrackedRequest(hReq, hCall, pszUrl, "", callbackCompleted, callbackError);
}
