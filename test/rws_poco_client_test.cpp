#include <gtest/gtest.h>
#include <Poco/Net/HTTPCookie.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/NameValueCollection.h>

#include <abb_librws/rws_poco_client.h>

namespace abb ::rws
{

// ---------------------------------------------------------------------------
// isSetCookieDeletion: detect server-sent "delete this cookie" responses
// from a RAW Set-Cookie header string. We bypass Poco's HTTPCookie
// constructor because Poco 1.9.2 mis-handles the SameSite attribute —
// it treats unknown attributes as the cookie's own name/value, silently
// dropping the real session cookie. See RMF-315 investigation notes.
//
// RFC 6265 §5.3: a Set-Cookie is a deletion when the value is empty,
// OR Max-Age=0, OR Expires is set to a past timestamp.
// ---------------------------------------------------------------------------

TEST(POCOClientCookieTest, isSetCookieDeletion_emptyValueIsDeletion)
{
  EXPECT_TRUE(POCOClient::isSetCookieDeletion(
      "-http-session-=; path=/; domain=192.168.11.11; expires=Tue, 07 Apr 2026 23:16:56 GMT"));
}

TEST(POCOClientCookieTest, isSetCookieDeletion_emptyValueNoAttrs)
{
  EXPECT_TRUE(POCOClient::isSetCookieDeletion("name="));
}

TEST(POCOClientCookieTest, isSetCookieDeletion_maxAgeZeroIsDeletion)
{
  EXPECT_TRUE(POCOClient::isSetCookieDeletion(
      "ABBCX=deleted; Max-Age=0; Path=/; Domain=192.168.11.11"));
}

TEST(POCOClientCookieTest, isSetCookieDeletion_maxAgeZeroCaseInsensitive)
{
  EXPECT_TRUE(POCOClient::isSetCookieDeletion(
      "ABBCX=deleted; MAX-AGE=0; Path=/"));
}

TEST(POCOClientCookieTest, isSetCookieDeletion_maxAgeZeroNotPrefixOfLargerNumber)
{
  // "Max-Age=04000" should NOT be treated as deletion even though it starts with '0'.
  EXPECT_FALSE(POCOClient::isSetCookieDeletion(
      "ABBCX=live; Max-Age=04000; Path=/"));
}

TEST(POCOClientCookieTest, isSetCookieDeletion_maxAgeNonZeroIsNotDeletion)
{
  EXPECT_FALSE(POCOClient::isSetCookieDeletion(
      "ABBCX=live; Max-Age=3600; Path=/"));
}

TEST(POCOClientCookieTest, isSetCookieDeletion_epochExpiresIsDeletion)
{
  // The exact ABBCX deletion cookie from the RMF-315 failure log.
  EXPECT_TRUE(POCOClient::isSetCookieDeletion(
      "ABBCX=deleted; Expires=Thu, 01-Jan-1970 00:00:01 GMT; Path=/; Domain=192.168.11.12; HttpOnly"));
}

TEST(POCOClientCookieTest, isSetCookieDeletion_pastExpiresIsDeletion)
{
  // An Expires clearly in the past (regardless of format variant).
  EXPECT_TRUE(POCOClient::isSetCookieDeletion(
      "name=value; Expires=Mon, 01 Jan 2001 00:00:00 GMT"));
}

TEST(POCOClientCookieTest, isSetCookieDeletion_futureExpiresIsNotDeletion)
{
  // Far-future Expires — must NOT be classified as deletion.
  EXPECT_FALSE(POCOClient::isSetCookieDeletion(
      "name=value; Expires=Fri, 31 Dec 2099 23:59:59 GMT"));
}

TEST(POCOClientCookieTest, isSetCookieDeletion_malformedExpiresIsNotDeletion)
{
  // If Expires can't be parsed, don't treat as deletion (be conservative).
  EXPECT_FALSE(POCOClient::isSetCookieDeletion(
      "name=value; Expires=garbage data"));
}

TEST(POCOClientCookieTest, isSetCookieDeletion_realOmnicoreSessionIsNotDeletion)
{
  // The EXACT live session cookies captured from a working Omnicore via curl.
  // These have no Expires and no Max-Age attributes.
  EXPECT_FALSE(POCOClient::isSetCookieDeletion(
      "-http-session-=1336::http.session::14841e23a7eb0ddc854e1f4643a91898;"
      " path=/; domain=192.168.11.11; secure; httponly; SameSite=None"));
  EXPECT_FALSE(POCOClient::isSetCookieDeletion(
      "ABBCX=1238181176; path=/; domain=192.168.11.11;"
      " secure; httponly; SameSite=None"));
}

TEST(POCOClientCookieTest, isSetCookieDeletion_malformedNoEquals)
{
  EXPECT_FALSE(POCOClient::isSetCookieDeletion("malformed_no_equals"));
}

// ---------------------------------------------------------------------------
// applySetCookieToJar: merge a raw Set-Cookie header into the jar.
//
// On deletion → erase entry from jar.
// Otherwise → set name=value.
// Must NOT use Poco's HTTPCookie parser (broken for SameSite in 1.9.2).
// ---------------------------------------------------------------------------

TEST(POCOClientCookieTest, applySetCookieToJar_addsNewCookie)
{
  Poco::Net::NameValueCollection jar;
  POCOClient::applySetCookieToJar(jar,
      "-http-session-=abc123; path=/; domain=192.168.11.11; secure; httponly; SameSite=None");
  ASSERT_EQ(jar.size(), 1u);
  EXPECT_EQ(jar.get("-http-session-"), "abc123");
}

TEST(POCOClientCookieTest, applySetCookieToJar_updatesExistingCookie)
{
  Poco::Net::NameValueCollection jar;
  jar.add("ABBCX", "old_value");
  POCOClient::applySetCookieToJar(jar, "ABBCX=new_value; path=/");
  ASSERT_EQ(jar.size(), 1u);
  EXPECT_EQ(jar.get("ABBCX"), "new_value");
}

TEST(POCOClientCookieTest, applySetCookieToJar_erasesOnEmptyValueDeletion)
{
  Poco::Net::NameValueCollection jar;
  jar.add("-http-session-", "session_token");
  POCOClient::applySetCookieToJar(jar,
      "-http-session-=; path=/; domain=192.168.11.12; expires=Tue, 07 Apr 2026 23:16:56 GMT");
  EXPECT_EQ(jar.size(), 0u);
  EXPECT_FALSE(jar.has("-http-session-"));
}

TEST(POCOClientCookieTest, applySetCookieToJar_erasesOnMaxAgeZeroDeletion)
{
  Poco::Net::NameValueCollection jar;
  jar.add("ABBCX", "session_token");
  POCOClient::applySetCookieToJar(jar, "ABBCX=deleted; Max-Age=0; Path=/");
  EXPECT_EQ(jar.size(), 0u);
  EXPECT_FALSE(jar.has("ABBCX"));
}

TEST(POCOClientCookieTest, applySetCookieToJar_erasesOnEpochExpiresDeletion)
{
  Poco::Net::NameValueCollection jar;
  jar.add("ABBCX", "session_token");
  POCOClient::applySetCookieToJar(jar,
      "ABBCX=deleted; Expires=Thu, 01-Jan-1970 00:00:01 GMT; Path=/");
  EXPECT_EQ(jar.size(), 0u);
  EXPECT_FALSE(jar.has("ABBCX"));
}

TEST(POCOClientCookieTest, applySetCookieToJar_neverCreatesDuplicateKeys)
{
  // Poco::Net::NameValueCollection is multimap-style; set() updates only the
  // first occurrence. Apply the same name twice to verify we never use add().
  Poco::Net::NameValueCollection jar;
  POCOClient::applySetCookieToJar(jar, "ABBCX=first; path=/");
  POCOClient::applySetCookieToJar(jar, "ABBCX=second; path=/");
  ASSERT_EQ(jar.size(), 1u);
  EXPECT_EQ(jar.get("ABBCX"), "second");
}

TEST(POCOClientCookieTest, applySetCookieToJar_ignoresMalformedHeader)
{
  Poco::Net::NameValueCollection jar;
  POCOClient::applySetCookieToJar(jar, "this has no equals sign");
  EXPECT_EQ(jar.size(), 0u);
}

TEST(POCOClientCookieTest, applySetCookieToJar_realOmnicoreSessionCookies)
{
  // THE critical regression test: the EXACT Set-Cookie strings from a working
  // ABB Omnicore controller. Our branch was failing because Poco 1.9.2's
  // HTTPCookie(NameValueCollection) mis-handles SameSite=None, causing the
  // real session cookies to be silently replaced with SameSite=None. This
  // test proves our raw-string parser stores them correctly.
  Poco::Net::NameValueCollection jar;

  POCOClient::applySetCookieToJar(jar,
      "-http-session-=1336::http.session::14841e23a7eb0ddc854e1f4643a91898;"
      " path=/; domain=192.168.11.11; secure; httponly; SameSite=None");
  POCOClient::applySetCookieToJar(jar,
      "ABBCX=1238181176; path=/; domain=192.168.11.11;"
      " secure; httponly; SameSite=None");

  ASSERT_EQ(jar.size(), 2u);
  EXPECT_TRUE(jar.has("-http-session-"));
  EXPECT_TRUE(jar.has("ABBCX"));
  EXPECT_EQ(jar.get("-http-session-"),
            "1336::http.session::14841e23a7eb0ddc854e1f4643a91898");
  EXPECT_EQ(jar.get("ABBCX"), "1238181176");
}

TEST(POCOClientCookieTest, applySetCookieToJar_RMF315InvalidationScenario)
{
  // The exact RMF-315 failure pattern: server sends deletions for both
  // cookies, jar must end empty so the next request can re-authenticate.
  Poco::Net::NameValueCollection jar;
  jar.add("-http-session-", "live_session_id");
  jar.add("ABBCX", "live_abbcx_value");

  POCOClient::applySetCookieToJar(jar,
      "-http-session-=; path=/; domain=192.168.11.12; expires=Tue, 07 Apr 2026 23:16:56 GMT");
  POCOClient::applySetCookieToJar(jar,
      "ABBCX=deleted; Expires=Thu, 01-Jan-1970 00:00:01 GMT; Path=/; Domain=192.168.11.12; HttpOnly");

  EXPECT_EQ(jar.size(), 0u)
      << "RMF-315: jar should be empty so next request can re-authenticate cleanly";
}

}  // namespace abb ::rws
