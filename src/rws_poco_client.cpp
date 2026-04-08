/***********************************************************************************************************************
 *
 * Copyright (c) 2015, ABB Schweiz AG
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with
 * or without modification, are permitted provided that
 * the following conditions are met:
 *
 *    * Redistributions of source code must retain the
 *      above copyright notice, this list of conditions
 *      and the following disclaimer.
 *    * Redistributions in binary form must reproduce the
 *      above copyright notice, this list of conditions
 *      and the following disclaimer in the documentation
 *      and/or other materials provided with the
 *      distribution.
 *    * Neither the name of ABB nor the names of its
 *      contributors may be used to endorse or promote
 *      products derived from this software without
 *      specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ***********************************************************************************************************************
 */

#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

#include <Poco/DateTime.h>
#include <Poco/DateTimeParser.h>
#include <Poco/Timestamp.h>
#include <Poco/String.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/NetException.h>
#include <Poco/StreamCopier.h>

#include <abb_librws/rws_poco_client.h>
#include <abb_librws/rws_error.h>

using namespace Poco;
using namespace Poco::Net;

namespace abb
{
namespace rws
{

namespace
{
// Trim ASCII whitespace from both ends, in place.
void trimInPlace(std::string& s)
{
  auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!s.empty() && is_space(static_cast<unsigned char>(s.front())))
  {
    s.erase(s.begin());
  }
  while (!s.empty() && is_space(static_cast<unsigned char>(s.back())))
  {
    s.pop_back();
  }
}

// Check case-insensitively whether `attrs` contains "max-age=0" as a
// standalone token (i.e., the character after the '0' is not a digit).
bool containsMaxAgeZero(const std::string& lower_attrs)
{
  const std::string needle = "max-age=0";
  size_t pos = 0;
  while ((pos = lower_attrs.find(needle, pos)) != std::string::npos)
  {
    const size_t after = pos + needle.size();
    if (after == lower_attrs.size() ||
        !std::isdigit(static_cast<unsigned char>(lower_attrs[after])))
    {
      return true;
    }
    pos = after;
  }
  return false;
}

// Extract the Expires attribute value (original case) from a raw
// Set-Cookie attributes string. Returns empty if not present.
std::string extractExpiresValue(const std::string& attrs, const std::string& lower_attrs)
{
  const std::string needle = "expires=";
  const size_t pos = lower_attrs.find(needle);
  if (pos == std::string::npos)
  {
    return {};
  }
  const size_t value_start = pos + needle.size();
  size_t value_end = attrs.find(';', value_start);
  if (value_end == std::string::npos)
  {
    value_end = attrs.size();
  }
  std::string value = attrs.substr(value_start, value_end - value_start);
  trimInPlace(value);
  return value;
}
}  // namespace

bool POCOClient::isSetCookieDeletion(const std::string& raw_set_cookie)
{
  // Split the header into "name=value" head and attributes tail.
  const size_t first_semi = raw_set_cookie.find(';');
  const std::string head = (first_semi == std::string::npos)
                               ? raw_set_cookie
                               : raw_set_cookie.substr(0, first_semi);

  const size_t eq = head.find('=');
  if (eq == std::string::npos)
  {
    return false;  // malformed: let applySetCookieToJar ignore it
  }

  // Empty-value deletion.
  std::string value = head.substr(eq + 1);
  trimInPlace(value);
  if (value.empty())
  {
    return true;
  }

  if (first_semi == std::string::npos)
  {
    return false;
  }

  // Lowercase the attribute portion for case-insensitive scanning.
  std::string attrs = raw_set_cookie.substr(first_semi + 1);
  std::string lower_attrs;
  lower_attrs.reserve(attrs.size());
  for (char c : attrs)
  {
    lower_attrs.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }

  // Max-Age=0 deletion.
  if (containsMaxAgeZero(lower_attrs))
  {
    return true;
  }

  // Past Expires deletion.
  const std::string expires_value = extractExpiresValue(attrs, lower_attrs);
  if (!expires_value.empty())
  {
    try
    {
      int tzd = 0;
      Poco::DateTime dt = Poco::DateTimeParser::parse(expires_value, tzd);
      dt.makeUTC(tzd);
      if (dt.timestamp() < Poco::Timestamp())
      {
        return true;
      }
    }
    catch (const Poco::Exception&)
    {
      // Unparseable date — be conservative and do not treat as deletion.
    }
  }

  return false;
}

void POCOClient::applySetCookieToJar(Poco::Net::NameValueCollection& jar,
                                     const std::string& raw_set_cookie)
{
  // Extract name=value (the head of the header before the first ';').
  const size_t first_semi = raw_set_cookie.find(';');
  const std::string head = (first_semi == std::string::npos)
                               ? raw_set_cookie
                               : raw_set_cookie.substr(0, first_semi);

  const size_t eq = head.find('=');
  if (eq == std::string::npos)
  {
    return;  // malformed, silently ignore
  }

  std::string name = head.substr(0, eq);
  std::string value = head.substr(eq + 1);
  trimInPlace(name);
  trimInPlace(value);
  if (name.empty())
  {
    return;
  }

  if (isSetCookieDeletion(raw_set_cookie))
  {
    jar.erase(name);
    return;
  }

  // set() is insert-or-update on the first occurrence. Never use add()
  // because NameValueCollection allows duplicate keys and add() would
  // create stale shadow entries.
  jar.set(name, value);
}

/***********************************************************************************************************************
 * Class definitions: POCOClient
 */

POCOClient::POCOClient(Poco::Net::HTTPClientSession& session, const std::string& username, const std::string& password)
  : http_client_session_{ session }, http_credentials_{ username, password }
{
  http_client_session_.setKeepAlive(true);
}

POCOClient::~POCOClient()
{
}

/************************************************************
 * Primary methods
 */

 POCOResult POCOClient::httpAuthenticate(const std::string& uri)
{
      // The response and the request.
      HTTPResponse response;
      std::string response_content;
      std::string content;

      HTTPRequest request(HTTPRequest::HTTP_GET, uri, HTTPRequest::HTTP_1_1);
      request.setCredentials("Basic", "RGVmYXVsdCBVc2VyOnJvYm90aWNz");
      request.add("accept", "application/xhtml+xml;v=2.0");

      authenticate(request, response, content, response_content);
      return POCOResult{ response.getStatus(), response.getReason(), response, response_content };
}


POCOResult POCOClient::httpGet(const std::string& uri)
{
  return makeHTTPRequest(HTTPRequest::HTTP_GET, uri);
}

POCOResult POCOClient::httpPost(const std::string& uri, const std::string& content, const std::string& content_type)
{
  return makeHTTPRequest(HTTPRequest::HTTP_POST, uri, content, content_type);
}

POCOResult POCOClient::httpPut(const std::string& uri, const std::string& content, const std::string& content_type)
{
  return makeHTTPRequest(HTTPRequest::HTTP_PUT, uri, content, content_type);
}

POCOResult POCOClient::httpDelete(const std::string& uri)
{
  return makeHTTPRequest(HTTPRequest::HTTP_DELETE, uri);
}

POCOResult POCOClient::makeHTTPRequest(const std::string& method, const std::string& uri, const std::string& content,
                     const std::string& content_type)
{
  // The response and the request.
  HTTPResponse response;
  std::string response_content;

  HTTPRequest request(method, uri, HTTPRequest::HTTP_1_1);
  request.add("accept", "application/xhtml+xml;v=2.0");
  request.setCookies(cookies_);
  request.setContentLength(content.length());

  if (!content_type.empty())
  {
    request.setContentType(content_type);
  }
  else if (method == HTTPRequest::HTTP_POST || !content.empty())
  {
    request.setContentType("application/x-www-form-urlencoded");
  }

  // Attempt the communication.
  try
  { 
    sendAndReceive(request, response, content, response_content);

    // Merge inbound Set-Cookie headers into the jar using our own raw-string
    // parser. Bypasses Poco 1.9.2's HTTPCookie(NameValueCollection) which
    // mis-handles SameSite=None by assigning it as the cookie name — that
    // bug silently dropped the real session cookies and caused the
    // controller to create a new session on every request (RMF-315).
    for (const auto& header : response)
    {
      if (Poco::icompare(header.first, HTTPResponse::SET_COOKIE) == 0)
      {
        applySetCookieToJar(cookies_, header.second);
      }
    }

    // Check if there was a server error, if so, make another attempt with a clean sheet.
    if (response.getStatus() >= HTTPResponse::HTTP_INTERNAL_SERVER_ERROR)
    {
      http_client_session_.reset();
      request.erase(HTTPRequest::COOKIE);
      sendAndReceive(request, response, content, response_content);

      // Merge any cookies from the retry response too.
      for (const auto& header : response)
      {
        if (Poco::icompare(header.first, HTTPResponse::SET_COOKIE) == 0)
        {
          applySetCookieToJar(cookies_, header.second);
        }
      }
    }

    // RMF-315: on a session-invalidation 401, let the existing cookie
    // jar do its work. The cookie merge above (applyCookieToJar) has
    // already removed any server-sent deletion cookies, so re-calling
    // authenticate() either (a) reuses the existing session if it was
    // still live server-side, or (b) creates exactly ONE new session
    // via Basic auth if it wasn't. We MUST NOT cookies_.clear() or
    // http_client_session_.reset() here — both force a brand-new
    // controller-side session per 401, and RobotWare 7.7+ caps the
    // total session pool at 70 per controller. On a cold-booted
    // controller (which returns 401 for several seconds during boot)
    // the retry loop in establishRWSConnection would otherwise burn
    // the entire pool before the station comes up.
    //
    // request.erase(COOKIE) is retained because the first sendAndReceive
    // above already attached the stale Cookie header to the in-flight
    // request object; scrub it so authenticate()'s sendAndReceive
    // doesn't double-attach.
    if (response.getStatus() == HTTPResponse::HTTP_UNAUTHORIZED)
    {
      request.erase(HTTPRequest::COOKIE);
      authenticate(request, response, content, response_content);
    }

    return POCOResult{ response.getStatus(), response.getReason(), response, response_content };
  }
  catch (CommunicationError const&)
  {
    // If an error occurred, clear the cookies and reset the session.
    cookies_.clear();
    http_client_session_.reset();

    throw;
  }
}

Poco::Net::WebSocket POCOClient::webSocketConnect(const std::string& uri, const std::string& protocol,
                                                  Poco::Net::HTTPClientSession&& session)
{
  // The response and the request.
  HTTPResponse response;
  HTTPRequest request(HTTPRequest::HTTP_GET, uri, HTTPRequest::HTTP_1_1);
  request.set("Sec-WebSocket-Protocol", protocol);
  request.setCookies(cookies_);

  // Attempt the communication.
  try
  {
    Poco::Net::WebSocket websocket{ session, request, response };

    if (response.getStatus() != HTTPResponse::HTTP_SWITCHING_PROTOCOLS)
      BOOST_THROW_EXCEPTION(ProtocolError{ "webSocketConnect() failed" } << HttpStatusErrorInfo{ response.getStatus() }
                                                                         << HttpReasonErrorInfo{ response.getReason() }
                                                                         << HttpMethodErrorInfo{ request.getMethod() }
                                                                         << UriErrorInfo{ uri });

    return websocket;
  }
  catch (Poco::Exception const& e)
  {
    BOOST_THROW_EXCEPTION(CommunicationError{ "webSocketConnect() failed: " + e.displayText() }
                          << HttpStatusErrorInfo{ response.getStatus() } << HttpReasonErrorInfo{ response.getReason() }
                          << HttpMethodErrorInfo{ request.getMethod() } << UriErrorInfo{ uri }
                          << boost::errinfo_nested_exception{ boost::current_exception() });
  }
}

/************************************************************
 * Auxiliary methods
 */

void POCOClient::sendAndReceive(HTTPRequest& request, HTTPResponse& response, const std::string& request_content,
                                std::string& response_content)
{
  HTTPInfo log_entry;

  // Add request info to the log entry.
  log_entry.addHTTPRequestInfo(request, request_content);

  // Add cookies to the request.
  if (cookies_.size() > 0)
  {
    request.setCookies(cookies_);
  }

  // Contact the server.
  try
  {
    std::ostream& request_content_stream = http_client_session_.sendRequest(request);
    request_content_stream << request_content;
  }
  catch (Poco::Exception const& e)
  {
    BOOST_THROW_EXCEPTION(CommunicationError{ "HTTP send error: " + e.displayText() }
                          << HttpMethodErrorInfo{ request.getMethod() } << UriErrorInfo{ request.getURI() }
                          << HttpRequestContentErrorInfo{ request_content }
                          << boost::errinfo_nested_exception{ boost::current_exception() });
  }

  try
  {
    std::istream& response_content_stream = http_client_session_.receiveResponse(response);

    response_content.clear();
    StreamCopier::copyToString(response_content_stream, response_content);
  }
  catch (Poco::Exception const& e)
  {
    BOOST_THROW_EXCEPTION(CommunicationError{ "HTTP receive error: " + e.displayText() }
                          << HttpMethodErrorInfo{ request.getMethod() } << UriErrorInfo{ request.getURI() }
                          << HttpRequestContentErrorInfo{ request_content }
                          << boost::errinfo_nested_exception{ boost::current_exception() });
  }

  // Add response info to the log entry.
  log_entry.addHTTPResponseInfo(response, response_content);

  // Add entry to the log.
  if (log_.size() >= LOG_SIZE)
  {
    log_.pop_back();
  }

  log_.push_front(log_entry);
}

void POCOClient::authenticate(HTTPRequest& request, HTTPResponse& response, const std::string& request_content,
                              std::string& response_content)
{
  // Authenticate with the provided credentials.
  http_credentials_.authenticate(request, response);

  // Contact the server, and extract and store the received cookies.
  sendAndReceive(request, response, request_content, response_content);

  // Repopulate the cookie jar from the auth response using the same
  // raw-header parser as makeHTTPRequest. Do NOT use Poco's getCookies()
  // here — it mis-handles SameSite=None in Poco 1.9.2 and the real
  // session cookies would be silently dropped.
  for (const auto& header : response)
  {
    if (Poco::icompare(header.first, HTTPResponse::SET_COOKIE) == 0)
    {
      applySetCookieToJar(cookies_, header.second);
    }
  }
}


std::string POCOClient::getLogText(bool verbose) const
{
  if (log_.size() == 0)
  {
    return "";
  }

  std::stringstream ss;

  for (size_t i = 0; i < log_.size(); ++i)
  {
    std::stringstream temp;
    temp << i + 1 << ". ";
    ss << temp.str() << log_[i].toString(verbose, temp.str().size()) << std::endl;
  }

  return ss.str();
}

std::string POCOClient::getLogTextLatestEvent(bool verbose) const
{
  return (log_.size() == 0 ? "" : log_[0].toString(verbose, 0));
}

void POCOClient::HTTPInfo::addHTTPRequestInfo(const Poco::Net::HTTPRequest& request, const std::string& request_content)
{
  this->request = { request.getMethod(), request.getURI(), request_content };
}

void POCOClient::HTTPInfo::addHTTPResponseInfo(const Poco::Net::HTTPResponse& response,
                                               const std::string& response_content)
{
  std::string header_info;

  for (HTTPResponse::ConstIterator i = response.begin(); i != response.end(); ++i)
  {
    header_info += i->first + "=" + i->second + "\n";
  }

  this->response = HTTPInfo::ResponseInfo{ response.getStatus(), header_info, response_content };
}

std::string POCOClient::HTTPInfo::toString(bool verbose, size_t indent) const
{
  std::stringstream ss;

  std::string seperator = (indent == 0 ? " | " : "\n" + std::string(indent, ' '));

  if (request)
  {
    ss << seperator << "HTTP Request: " << request->method << " " << request->uri;
  }

  if (response)
  {
    ss << seperator << "HTTP Response: " << response->status << " - "
       << HTTPResponse::getReasonForStatus(response->status);

    if (verbose)
    {
      ss << seperator << "HTTP Response Content: " << response->content;
    }
  }

  return ss.str();
}

}  // end namespace rws
}  // end namespace abb
