#include "gtest/gtest.h"

#include "uri.h"

using namespace ydsh;
using namespace uri;

TEST(URITest, base1) {
    std::string str = "https://tools.ietf.org/html/rfc3986";
    auto uri = URI::fromString(str);
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(uri));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("https", uri.getScheme()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("tools.ietf.org", uri.getAuthority().getHost()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", uri.getAuthority().getUserInfo()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", uri.getAuthority().getPort()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("/html/rfc3986", uri.getPath()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", uri.getQuery()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", uri.getFragment()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(str, uri.toString()));
}

TEST(URITest, base2) {
    std::string str = "http://user:pass@127.0.0.1:8000/path/data?key=val&key2=val2#frag1";
    auto uri = URI::fromString(str);
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(uri));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("http", uri.getScheme()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("127.0.0.1", uri.getAuthority().getHost()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("user:pass", uri.getAuthority().getUserInfo()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("8000", uri.getAuthority().getPort()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("/path/data", uri.getPath()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("key=val&key2=val2", uri.getQuery()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("frag1", uri.getFragment()));
//    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(str, URI::fromString(uri.toString()).toString()));
}

TEST(URITest, base3) {
    std::string str = "ssh://[2001:db8:85a3::8a2e:370:7334]/";
    auto uri = URI::fromString(str);
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(uri));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("ssh", uri.getScheme()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("[2001:db8:85a3::8a2e:370:7334]", uri.getAuthority().getHost()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", uri.getAuthority().getUserInfo()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", uri.getAuthority().getPort()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("/", uri.getPath()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", uri.getQuery()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", uri.getFragment()));
//    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(str, URI::fromString(uri.toString()).toString()));
}

TEST(URITest, base4) {
    std::string str = "rsync://rsync.kernel.org/pub/";
    auto uri = URI::fromString(str);
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(uri));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("rsync", uri.getScheme()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("rsync.kernel.org", uri.getAuthority().getHost()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", uri.getAuthority().getUserInfo()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", uri.getAuthority().getPort()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("/pub/", uri.getPath()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", uri.getQuery()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", uri.getFragment()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(str, uri.toString()));
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}