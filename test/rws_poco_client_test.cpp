#include <gtest/gtest.h>
#include <Poco/Net/HTTPCookie.h>

namespace abb ::rws
{
// Sanity check: prove the new translation unit links against
// gtest + Poco::Net before we start testing real helpers.
TEST(POCOClientCookieTest, sanity_PocoHTTPCookieIsConstructible)
{
  Poco::Net::HTTPCookie cookie("name", "value");
  EXPECT_EQ(cookie.getName(), "name");
  EXPECT_EQ(cookie.getValue(), "value");
}
}  // namespace abb ::rws
