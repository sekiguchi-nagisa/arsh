#include "gtest/gtest.h"

#include <misc/buffer.hpp>
#include <misc/inlined_array.hpp>
#include <misc/ring_buffer.hpp>

using namespace arsh;

using IBuffer = FlexBuffer<unsigned int>;

TEST(BufferTest, case1) {
  IBuffer buffer;

  ASSERT_EQ(0u, buffer.size());
  ASSERT_EQ(0u, buffer.capacity());
  ASSERT_TRUE(buffer.empty());

  // append
  for (unsigned int i = 0; i < 8; i++) {
    buffer += i;
  }

  ASSERT_EQ(8, buffer.size());
  ASSERT_TRUE(buffer.size() <= buffer.capacity());

  // get
  for (unsigned int i = 0; i < 8; i++) {
    ASSERT_EQ(i, buffer[i]);
    ASSERT_EQ(i, buffer.at(i));
  }

  // set
  buffer[5] = 90;
  ASSERT_EQ(90u, buffer[5]);
  ASSERT_EQ(3u, ++buffer[2]);
  ASSERT_EQ(0u, buffer[0]++);
  ASSERT_EQ(1u, buffer[0]);

#ifndef __EMSCRIPTEN__
  // out of range
  std::string msg("size is: ");
  msg += std::to_string(buffer.size());
  msg += ", but index is: ";
  msg += std::to_string(buffer.size() + 2);
  ASSERT_EXIT(
      { buffer.at(buffer.size() + 2) = 999; }, ::testing::KilledBySignal(SIGABRT), msg.c_str());
#endif
}

TEST(BufferTest, case2) {
  IBuffer buffer;

  unsigned int size = 10;
  for (unsigned int i = 0; i < size; i++) {
    buffer += i; // expand buffer.
  }

  ASSERT_EQ(size, buffer.size());
  ASSERT_TRUE(buffer.size() <= buffer.capacity());
  auto cap = buffer.capacity();

  for (unsigned int i = 0; i < size; i++) {
    ASSERT_EQ(i, buffer[i]);
  }

  buffer.clear();
  ASSERT_EQ(0u, buffer.size());
  ASSERT_EQ(cap, buffer.capacity());

  // remove
  free(buffer.take());
  ASSERT_EQ(0u, buffer.size());
  ASSERT_EQ(0u, buffer.capacity());
  ASSERT_TRUE(buffer.data() == nullptr);

  unsigned int v[] = {10, 20, 30};
  // reuse
  buffer += v;
  ASSERT_EQ(3u, buffer.size());
  ASSERT_TRUE(buffer.size() <= buffer.capacity());
  ASSERT_EQ(v[0], buffer[0]);
  ASSERT_EQ(v[1], buffer[1]);
  ASSERT_EQ(v[2], buffer[2]);
}

TEST(BufferTest, case3) {
  ByteBuffer buffer;
  const char *s = "hello world!!";
  unsigned int len = strlen(s);
  buffer.append(s, strlen(s));
  unsigned int cap = buffer.capacity();

  for (unsigned int i = 0; i < len; i++) {
    ASSERT_EQ(s[i], buffer[i]);
  }

  ByteBuffer buffer2(std::move(buffer));

  // after move
  ASSERT_EQ(0u, buffer.size());
  ASSERT_EQ(0u, buffer.capacity());
  ASSERT_TRUE(buffer.data() == nullptr);

  ASSERT_EQ(len, buffer2.size());
  ASSERT_EQ(cap, buffer2.capacity());
  ASSERT_TRUE(memcmp(s, buffer2.data(), buffer2.size()) == 0);

  buffer2 += buffer;
  // do nothing
  ASSERT_EQ(len, buffer2.size());
  ASSERT_EQ(cap, buffer2.capacity());

  ByteBuffer buffer3;
  buffer3 += buffer2;
  buffer3 += ' ';
  buffer3 += buffer2;
  buffer3 += '\0';

  ASSERT_TRUE(memcmp(s, buffer2.data(), buffer2.size()) == 0);
  ASSERT_STREQ("hello world!! hello world!!", buffer3.data());
}

using StrBuffer = FlexBuffer<const char *, unsigned char>;

#ifndef __EMSCRIPTEN__
TEST(BufferTest, case4) {
  StrBuffer buffer;

  const char *v[] = {
      "AAA", "BBB", "CCC", "DDD", "EEE", "FFF", "GGG", "HHH", "III", "JJJ",
  };

  ASSERT_EXIT(
      {
        for (unsigned int i = 0; i < 30; i++) {
          buffer.append(v, std::size(v));
        }
      },
      ::testing::KilledBySignal(SIGABRT), "reach max size\n");
}
#endif

TEST(BufferTest, case5) {
  const StrBuffer::size_type size = 254;
  StrBuffer buffer(size);

  ASSERT_EQ(StrBuffer::MAX_SIZE - 1, buffer.capacity());

  const char *v[size];
  buffer.append(v, size);
}

TEST(BufferTest, case6) {
  IBuffer buffer;

  buffer += 1;
  buffer += 2;
  buffer += 3;

  // const iterator
  auto &r = static_cast<const IBuffer &>(buffer);
  unsigned int count = 1;
  for (auto &e : r) {
    ASSERT_EQ(count, e);
    count++;
  }
  ASSERT_EQ(3u, r.end() - r.begin());

  // iterator
  for (auto &e : buffer) {
    e++;
  }
  ASSERT_EQ(3u, buffer.end() - buffer.begin());

  count = 2;
  for (auto &e : r) {
    ASSERT_EQ(count, e);
    count++;
  }

  for (unsigned int i = 0; i < 3; i++) { // test const at
    ASSERT_EQ(i + 2, r.at(i));
  }

  // test front, back
  ASSERT_EQ(2u, buffer.front());
  ASSERT_EQ(2u, r.front());
  ASSERT_EQ(4u, buffer.back());
  ASSERT_EQ(4u, r.back());

  // test pop_back
  ASSERT_EQ(3u, buffer.size());
  buffer.pop_back();
  ASSERT_EQ(2u, buffer.size());
}

#ifndef __EMSCRIPTEN__
TEST(BufferTest, case7) { // test append own
  IBuffer buffer;
  buffer += 23;
  buffer += 43;

  ASSERT_NO_FATAL_FAILURE(
      ASSERT_EXIT(buffer += buffer, ::testing::KilledBySignal(SIGABRT), "appending own buffer\n"));

  buffer += std::move(buffer);
  ASSERT_EQ(2u, buffer.size());
  ASSERT_EQ(23u, buffer[0]);
  ASSERT_EQ(43u, buffer[1]);
}
#endif

TEST(BufferTest, case8) {
  IBuffer buffer;
  for (unsigned int i = 0; i < 5; i++) {
    buffer += i;
  }

  auto iter = buffer.erase(buffer.begin() + 2);
  ASSERT_EQ(4u, buffer.size());
  ASSERT_EQ(3u, *iter);
  ASSERT_EQ(2u, iter - buffer.begin());

  buffer += 5;
  ASSERT_EQ(5u, buffer.size());

  unsigned int r[] = {0, 1, 3, 4, 5};

  unsigned int i = 0;
  for (auto &e : buffer) {
    ASSERT_EQ(r[i], e);
    i++;
  }
}

TEST(BufferTest, case9) {
  IBuffer buffer;
  for (unsigned int i = 0; i < 10; i++) {
    buffer += i;
  }

  auto iter = buffer.erase(buffer.begin() + 2, buffer.begin() + 5);
  ASSERT_EQ(7u, buffer.size());
  ASSERT_EQ(5u, *iter);
  ASSERT_EQ(2u, iter - buffer.begin());

  buffer += 10;
  ASSERT_EQ(8u, buffer.size());

  unsigned int r[] = {
      0, 1, 5, 6, 7, 8, 9, 10,
  };

  unsigned int i = 0;
  for (auto &e : buffer) {
    ASSERT_EQ(r[i], e);
    i++;
  }
}

TEST(BufferTest, case10) {
  IBuffer buffer;
  for (unsigned int i = 0; i < 10; i++) {
    buffer += i;
  }

  auto iter = buffer.erase(buffer.begin(), buffer.begin() + 3);
  ASSERT_EQ(7u, buffer.size());
  ASSERT_EQ(3u, *iter);
  ASSERT_EQ(0u, iter - buffer.begin());

  buffer += 10;
  ASSERT_EQ(8u, buffer.size());

  unsigned int i = 0;
  for (auto &e : buffer) {
    ASSERT_EQ(i + 3, e);
    i++;
  }
}

TEST(BufferTest, case11) {
  IBuffer buffer;
  for (unsigned int i = 0; i < 10; i++) {
    buffer += i;
  }

  auto iter = buffer.erase(buffer.begin() + 3, buffer.end());
  ASSERT_EQ(3u, buffer.size());
  ASSERT_TRUE(iter == buffer.end());
  ASSERT_EQ(3u, iter - buffer.begin());

  buffer += 3;
  ASSERT_EQ(4u, buffer.size());

  unsigned int i = 0;
  for (auto &e : buffer) {
    ASSERT_EQ(i, e);
    i++;
  }

  buffer.erase(buffer.end(), buffer.end());
  ASSERT_EQ(4u, buffer.size());
  buffer.erase(buffer.begin() + 1, buffer.begin() + 1);
  ASSERT_EQ(4u, buffer.size());
}

TEST(BufferTest, case12) {
  IBuffer buffer;

  // insert first
  auto iter = buffer.insert(buffer.begin(), 1);
  ASSERT_EQ(1u, buffer.size());
  ASSERT_EQ(buffer.begin(), iter);
  ASSERT_EQ(1u, *iter);

  iter = buffer.insert(buffer.begin(), 0);
  ASSERT_EQ(2u, buffer.size());
  ASSERT_EQ(buffer.begin(), iter);
  ASSERT_EQ(0u, *iter);

  // insert last
  iter = buffer.insert(buffer.end(), 3);
  ASSERT_EQ(3u, buffer.size());
  ASSERT_EQ(buffer.end() - 1, iter);
  ASSERT_EQ(3u, *iter);

  // insert
  iter = buffer.insert(buffer.begin() + 2, 2);
  ASSERT_EQ(4u, buffer.size());
  ASSERT_EQ(buffer.begin() + 2, iter);
  ASSERT_EQ(2u, *iter);

  unsigned int count = 0;
  for (auto &e : buffer) {
    ASSERT_EQ(count, e);
    count++;
  }
}

TEST(BufferTest, case13) {
  IBuffer buffer;
  buffer += 45;

  // insert
  buffer.insert(buffer.end(), 8, 12345);
  ASSERT_EQ(9u, buffer.size());
  for (unsigned int i = 0; i < 8; i++) {
    ASSERT_EQ(12345u, buffer[i + 1]);
  }
}

TEST(BufferTest, case14) {
  // initializer list
  IBuffer buffer = {0, 2, 4};
  ASSERT_EQ(3u, buffer.size());
  ASSERT_EQ(0u, buffer[0]);
  ASSERT_EQ(2u, buffer[1]);
  ASSERT_EQ(4u, buffer[2]);
}

struct Dummy {
  unsigned int first;
  const char *second;
};

TEST(BufferTest, case15) {
  FlexBuffer<Dummy> buffer;

  buffer += Dummy{0, nullptr};
  buffer.push_back({0, nullptr});

  Dummy d;
  d.first = 1;
  d.second = "hello";
  buffer.push_back(d);

  ASSERT_EQ(3u, buffer.size());
  ASSERT_EQ(0u, buffer[0].first);
  ASSERT_EQ(nullptr, buffer[0].second);
  ASSERT_EQ(0u, buffer[1].first);
  ASSERT_EQ(nullptr, buffer[1].second);
  ASSERT_EQ(1u, buffer[2].first);
  ASSERT_STREQ("hello", buffer[2].second);
}

TEST(BufferTest, case16) {
  IBuffer b1 = {0, 1, 3};
  IBuffer b2;
  b2 += 0;
  b2 += 1;
  b2 += 3;

  ASSERT_TRUE(b1 == b2);
  ASSERT_FALSE(b1 != b2);

  b1[2] = 0;
  ASSERT_FALSE(b1 == b2);
  ASSERT_TRUE(b1 != b2);
}

TEST(BufferTest, case17) {
  ByteBuffer b("hello");
  ASSERT_EQ(6, b.size());
  ASSERT_EQ('h', b[0]);
  ASSERT_EQ('\0', b[5]);
}

TEST(RingBuffer, base1) {
  RingBuffer<std::unique_ptr<std::string>> buffer;
  ASSERT_TRUE(buffer.empty());
  ASSERT_EQ(0, buffer.size());
  ASSERT_EQ(1, buffer.capacity());
  buffer.push_back(std::make_unique<std::string>("hello"));
  ASSERT_EQ(1, buffer.size());
  ASSERT_EQ("hello", *buffer.back());
  ASSERT_EQ("hello", *buffer.front());

  buffer.push_back(std::make_unique<std::string>("AAA"));
  ASSERT_EQ(1, buffer.size());
  ASSERT_EQ("AAA", *buffer.back());
  ASSERT_EQ("AAA", *buffer.front());
  buffer.pop_front();
  ASSERT_TRUE(buffer.empty());

  buffer.push_back(std::make_unique<std::string>("BBB"));
  ASSERT_EQ("BBB", *buffer.back());
  ASSERT_EQ("BBB", *buffer.front());
  buffer.pop_back();
  ASSERT_TRUE(buffer.empty());
}

TEST(RingBuffer, base2) {
  RingBuffer<unsigned int> buffer;
  ASSERT_TRUE(buffer.empty());
  ASSERT_EQ(0, buffer.size());
  ASSERT_EQ(1, buffer.capacity());
  buffer.push_back(12);
  ASSERT_EQ(1, buffer.size());
  ASSERT_EQ(12, buffer.back());
  ASSERT_EQ(12, buffer.front());
}

TEST(RingBuffer, pop) {
  RingBuffer<std::unique_ptr<std::string>> buffer(2);
  ASSERT_EQ(3, buffer.capacity());
  for (unsigned int i = 0; i < 100; i++) {
    buffer.push_back(std::make_unique<std::string>(std::to_string(i)));
  }
  ASSERT_EQ(3, buffer.size());
  ASSERT_EQ("99", *buffer.back());
  ASSERT_EQ("97", *buffer.front());

  ASSERT_EQ("97", *buffer[0]);
  ASSERT_EQ("98", *buffer[1]);
  ASSERT_EQ("99", *buffer[2]);
}

TEST(InlinedArray, trivial) {
  {
    InlinedArray<int[2], 5> values(5);
    ASSERT_EQ(5, values.size());
    ASSERT_FALSE(values.isAllocated());
    values[0][0] = 99;
    values[0][1] = 3;
    ASSERT_EQ(99, values[0][0]);
    ASSERT_EQ(3, values[0][1]);
  }

  {
    InlinedArray<const char *, 3> values2(5);
    ASSERT_EQ(5, values2.size());
    ASSERT_TRUE(values2.isAllocated());
    values2[3] = "hello";
    values2[4] = "world";
    ASSERT_STREQ("hello", values2[3]);
    ASSERT_STREQ("world", values2[4]);
  }
}

struct AAA {
  const char *name{nullptr};
  unsigned int value{99};

  AAA() = default;
};

TEST(InlinedArray, trivialCopyable) {
  {
    InlinedArray<AAA, 7> values(5);
    ASSERT_EQ(5, values.size());
    ASSERT_FALSE(values.isAllocated());
    for (unsigned int i = 0; i < values.size(); i++) {
      ASSERT_EQ(99, values[0].value);
      ASSERT_FALSE(values[0].name);
    }
  }

  {
    InlinedArray<AAA, 2> values2(5);
    ASSERT_EQ(5, values2.size());
    ASSERT_TRUE(values2.isAllocated());
    for (unsigned int i = 0; i < values2.size(); i++) {
      ASSERT_EQ(99, values2[0].value);
      ASSERT_FALSE(values2[0].name);
    }
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
