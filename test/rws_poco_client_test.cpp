#include <gtest/gtest.h>
#include <Poco/Net/HTTPCookie.h>

#include <abb_librws/rws_poco_client.h>

namespace abb ::rws
{

// ---------------------------------------------------------------------------
// isCookieDeletion: detect server-sent "delete this cookie" responses.
//
// Per RFC 6265 the server signals deletion by either:
//   * sending an empty cookie value, or
//   * setting Max-Age=0, or
//   * setting Expires to a past timestamp.
// ABB Omnicore uses the first two when invalidating an idle session, e.g.:
//   Set-Cookie: -http-session-=; expires=...23:16:56 GMT
//   Set-Cookie: ABBCX=deleted; Expires=Thu, 01-Jan-1970 00:00:01 GMT; ...
// ---------------------------------------------------------------------------

TEST(POCOClientCookieTest, isCookieDeletion_emptyValueIsDeletion)
{
  Poco::Net::HTTPCookie cookie("-http-session-", "");
  EXPECT_TRUE(POCOClient::isCookieDeletion(cookie));
}

TEST(POCOClientCookieTest, isCookieDeletion_maxAgeZeroIsDeletion)
{
  Poco::Net::HTTPCookie cookie("ABBCX", "deleted");
  cookie.setMaxAge(0);
  EXPECT_TRUE(POCOClient::isCookieDeletion(cookie));
}

TEST(POCOClientCookieTest, isCookieDeletion_normalValueIsNotDeletion)
{
  Poco::Net::HTTPCookie cookie("-http-session-", "abc123");
  EXPECT_FALSE(POCOClient::isCookieDeletion(cookie));
}

TEST(POCOClientCookieTest, isCookieDeletion_normalValueWithMaxAgeUnsetIsNotDeletion)
{
  // Default Max-Age is -1 (session cookie, no explicit expiry) — must NOT
  // be treated as deletion.
  Poco::Net::HTTPCookie cookie("ABBCX", "live");
  EXPECT_EQ(cookie.getMaxAge(), -1);
  EXPECT_FALSE(POCOClient::isCookieDeletion(cookie));
}

TEST(POCOClientCookieTest, isCookieDeletion_positiveMaxAgeIsNotDeletion)
{
  Poco::Net::HTTPCookie cookie("ABBCX", "live");
  cookie.setMaxAge(3600);
  EXPECT_FALSE(POCOClient::isCookieDeletion(cookie));
}

}  // namespace abb ::rws
