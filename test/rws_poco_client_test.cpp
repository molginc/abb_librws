#include <gtest/gtest.h>
#include <Poco/Net/HTTPCookie.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/NameValueCollection.h>

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

// ---------------------------------------------------------------------------
// Round-trip parse tests: take the EXACT Set-Cookie strings from the
// RMF-315 failure log, parse them via Poco's HTTPResponse::getCookies()
// (the same code path makeHTTPRequest uses), and assert that
// isCookieDeletion correctly identifies each as a deletion.
//
// These tests anchor our assumption about Poco's parsing behavior so it
// can never silently regress.
// ---------------------------------------------------------------------------

TEST(POCOClientCookieTest, isCookieDeletion_RMF315_emptyValueWithPastExpires)
{
  // -http-session-=; path=/; domain=192.168.11.12; expires=Tue, 07 Apr 2026 23:16:56 GMT
  // (Empty value, past Expires.)
  Poco::Net::HTTPResponse response;
  response.add("Set-Cookie",
               "-http-session-=; path=/; domain=192.168.11.12; "
               "expires=Tue, 07 Apr 2026 23:16:56 GMT");

  std::vector<Poco::Net::HTTPCookie> cookies;
  response.getCookies(cookies);
  ASSERT_EQ(cookies.size(), 1u);

  // Pin Poco's behavior independent of isCookieDeletion: a past Expires
  // must be normalized into a large NEGATIVE Max-Age (NOT -1, which is
  // the session-cookie sentinel for "no Max-Age set"). If a Poco upgrade
  // ever changes this, isCookieDeletion's getMaxAge() < -1 branch becomes
  // ineffective and we need to know.
  EXPECT_LT(cookies.front().getMaxAge(), -1)
      << "Poco normalization changed: past Expires no longer maps to Max-Age < -1";

  EXPECT_TRUE(POCOClient::isCookieDeletion(cookies.front()))
      << "RMF-315: empty-value cookie must be detected as deletion";
}

TEST(POCOClientCookieTest, isCookieDeletion_RMF315_nonEmptyValueWithEpochExpires)
{
  // ABBCX=deleted; Expires=Thu, 01-Jan-1970 00:00:01 GMT; Path=/; Domain=192.168.11.12; HttpOnly
  // (Non-empty value 'deleted', Expires set to Unix epoch.)
  // This is the load-bearing case: if Poco does NOT normalize past
  // Expires into Max-Age <= 0, our helper would silently miss this
  // cookie and RMF-315 would persist.
  Poco::Net::HTTPResponse response;
  response.add("Set-Cookie",
               "ABBCX=deleted; Expires=Thu, 01-Jan-1970 00:00:01 GMT; "
               "Path=/; Domain=192.168.11.12; HttpOnly");

  std::vector<Poco::Net::HTTPCookie> cookies;
  response.getCookies(cookies);
  ASSERT_EQ(cookies.size(), 1u);
  EXPECT_TRUE(POCOClient::isCookieDeletion(cookies.front()))
      << "RMF-315: non-empty value with epoch Expires must be detected as deletion";
}

// ---------------------------------------------------------------------------
// applyCookieToJar: merge a single inbound cookie into the in-memory jar.
//
// On a deletion response we MUST remove the entry rather than overwriting
// it with an empty value, otherwise the next request resends the dead
// cookie and the controller stays at 401.
// ---------------------------------------------------------------------------

TEST(POCOClientCookieTest, applyCookieToJar_addsNewCookie)
{
  Poco::Net::NameValueCollection jar;
  Poco::Net::HTTPCookie cookie("ABBCX", "abc123");

  POCOClient::applyCookieToJar(jar, cookie);

  ASSERT_EQ(jar.size(), 1u);
  EXPECT_EQ(jar.get("ABBCX"), "abc123");
}

TEST(POCOClientCookieTest, applyCookieToJar_updatesExistingCookie)
{
  Poco::Net::NameValueCollection jar;
  jar.add("ABBCX", "old_value");

  Poco::Net::HTTPCookie cookie("ABBCX", "new_value");
  POCOClient::applyCookieToJar(jar, cookie);

  ASSERT_EQ(jar.size(), 1u);
  EXPECT_EQ(jar.get("ABBCX"), "new_value");
}

TEST(POCOClientCookieTest, applyCookieToJar_removesOnDeletion_emptyValue)
{
  Poco::Net::NameValueCollection jar;
  jar.add("-http-session-", "session_token");

  Poco::Net::HTTPCookie deletion("-http-session-", "");
  POCOClient::applyCookieToJar(jar, deletion);

  EXPECT_EQ(jar.size(), 0u);
  EXPECT_FALSE(jar.has("-http-session-"));
}

TEST(POCOClientCookieTest, applyCookieToJar_removesOnDeletion_maxAgeZero)
{
  Poco::Net::NameValueCollection jar;
  jar.add("ABBCX", "session_token");

  Poco::Net::HTTPCookie deletion("ABBCX", "deleted");
  deletion.setMaxAge(0);
  POCOClient::applyCookieToJar(jar, deletion);

  EXPECT_EQ(jar.size(), 0u);
  EXPECT_FALSE(jar.has("ABBCX"));
}

TEST(POCOClientCookieTest, applyCookieToJar_deletionOfMissingCookieIsNoop)
{
  Poco::Net::NameValueCollection jar;
  jar.add("other", "still_here");

  Poco::Net::HTTPCookie deletion("ABBCX", "");
  POCOClient::applyCookieToJar(jar, deletion);

  ASSERT_EQ(jar.size(), 1u);
  EXPECT_EQ(jar.get("other"), "still_here");
}

TEST(POCOClientCookieTest, applyCookieToJar_OmnicoreSessionInvalidationScenario)
{
  // Reproduces the exact RMF-315 failure log: jar holds the live cookies,
  // controller responds with deletions for both, jar must end up empty
  // so the next request can re-authenticate cleanly.
  Poco::Net::NameValueCollection jar;
  jar.add("-http-session-", "abc123");
  jar.add("ABBCX", "live_session");

  Poco::Net::HTTPCookie del_session("-http-session-", "");
  Poco::Net::HTTPCookie del_abbcx("ABBCX", "deleted");
  del_abbcx.setMaxAge(0);

  POCOClient::applyCookieToJar(jar, del_session);
  POCOClient::applyCookieToJar(jar, del_abbcx);

  EXPECT_EQ(jar.size(), 0u) << "RMF-315: poisoned jar would survive into the next request";
}

TEST(POCOClientCookieTest, applyCookieToJar_neverCreatesDuplicateKeys)
{
  // Poco::Net::NameValueCollection is multimap-style ("There can be
  // more than one name-value pair with the same name" — see
  // NameValueCollection.h). If applyCookieToJar ever used add()
  // instead of set() for an existing key, two entries would coexist
  // and the stale one would survive future updates. Pin set()
  // semantics so a future "optimization" can't reintroduce the bug.
  Poco::Net::NameValueCollection jar;

  Poco::Net::HTTPCookie first("ABBCX", "value_one");
  POCOClient::applyCookieToJar(jar, first);
  ASSERT_EQ(jar.size(), 1u);

  // Apply a second non-deletion cookie with the SAME name. Naive add()
  // would grow the jar to 2 entries; correct set() leaves it at 1.
  Poco::Net::HTTPCookie second("ABBCX", "value_two");
  POCOClient::applyCookieToJar(jar, second);

  EXPECT_EQ(jar.size(), 1u)
      << "applyCookieToJar must not create duplicate keys for the same cookie name";
  EXPECT_EQ(jar.get("ABBCX"), "value_two");
}

}  // namespace abb ::rws
