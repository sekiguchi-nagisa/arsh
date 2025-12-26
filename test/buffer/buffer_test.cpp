#include "gtest/gtest.h"

#include <misc/bitset.hpp>
#include <misc/buffer.hpp>
#include <misc/inlined_array.hpp>
#include <misc/inlined_stack.hpp>
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

TEST(BufferTest, case7) { // test append own
  IBuffer buffer;
  buffer += 23;
  buffer += 43;
  ASSERT_EQ(2u, buffer.size());
  ASSERT_EQ(23u, buffer[0]);
  ASSERT_EQ(43u, buffer[1]);

  // append own
  buffer += std::move(buffer); // do nothing
  ASSERT_EQ(2u, buffer.size());
  ASSERT_EQ(23u, buffer[0]);
  ASSERT_EQ(43u, buffer[1]);

  buffer += buffer;
  ASSERT_EQ(2u, buffer.size());
  ASSERT_EQ(23u, buffer[0]);
  ASSERT_EQ(43u, buffer[1]);

  buffer.append(buffer.data(), buffer.size());
  ASSERT_EQ(2u, buffer.size());
  ASSERT_EQ(23u, buffer[0]);
  ASSERT_EQ(43u, buffer[1]);
}

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
  ASSERT_TRUE(buffer.full());
  ASSERT_EQ("hello", *buffer.back());
  ASSERT_EQ("hello", *buffer.front());

  buffer.push_back(std::make_unique<std::string>("AAA"));
  ASSERT_EQ(1, buffer.size());
  ASSERT_TRUE(buffer.full());
  ASSERT_EQ("AAA", *buffer.back());
  ASSERT_EQ("AAA", *buffer.front());
  buffer.pop_front();
  ASSERT_TRUE(buffer.empty());

  buffer.push_back(std::make_unique<std::string>("BBB"));
  ASSERT_EQ(1, buffer.size());
  ASSERT_TRUE(buffer.full());
  ASSERT_EQ("BBB", *buffer.back());
  ASSERT_EQ("BBB", *buffer.front());
  buffer.pop_back();
  ASSERT_TRUE(buffer.empty());
  ASSERT_FALSE(buffer.full());
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
  ASSERT_EQ(3, buffer.capacity());
  ASSERT_TRUE(buffer.full());
  ASSERT_EQ("99", *buffer.back());
  ASSERT_EQ("97", *buffer.front());

  ASSERT_EQ("97", *buffer[0]);
  ASSERT_EQ("98", *buffer[1]);
  ASSERT_EQ("99", *buffer[2]);

  buffer.pop_front();
  ASSERT_FALSE(buffer.full());
  ASSERT_EQ(2, buffer.size());
  ASSERT_EQ(3, buffer.capacity());
  ASSERT_EQ("98", *buffer[0]);
  ASSERT_EQ("99", *buffer[1]);

  buffer.pop_back();
  ASSERT_FALSE(buffer.full());
  ASSERT_EQ(1, buffer.size());
  ASSERT_EQ(3, buffer.capacity());
  ASSERT_EQ("98", *buffer[0]);
}

TEST(RingBuffer, capacity) {
  ASSERT_EQ(2, RingBuffer<unsigned int>::alignAllocSize(0));
  ASSERT_EQ(2, RingBuffer<unsigned int>::alignAllocSize(1));
  ASSERT_EQ(4, RingBuffer<unsigned int>::alignAllocSize(2));
  ASSERT_EQ(4, RingBuffer<unsigned int>::alignAllocSize(3));
  ASSERT_EQ(8, RingBuffer<unsigned int>::alignAllocSize(4));
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

TEST(InlinedStack, base) {
  {
    InlinedStack<AAA, 3> stack;
    ASSERT_EQ(0, stack.size());
    ASSERT_EQ(3, stack.capacity());
    ASSERT_TRUE(stack.isStackAlloc());

    stack.push({.name = "hey1", .value = 111});
    ASSERT_EQ(1, stack.size());
    ASSERT_EQ(3, stack.capacity());
    ASSERT_EQ(111, stack.back().value);
    ASSERT_STREQ("hey1", stack.back().name);

    stack.push({.name = "hey2", .value = 222});
    ASSERT_EQ(2, stack.size());
    ASSERT_EQ(3, stack.capacity());
    ASSERT_TRUE(stack.isStackAlloc());
    ASSERT_EQ(222, stack.back().value);
    ASSERT_STREQ("hey2", stack.back().name);

    stack.push({.name = "hey3", .value = 333});
    ASSERT_EQ(3, stack.size());
    ASSERT_EQ(3, stack.capacity());
    ASSERT_TRUE(stack.isStackAlloc());
    ASSERT_EQ(333, stack.back().value);
    ASSERT_STREQ("hey3", stack.back().name);

    stack.push({.name = "hey44", .value = 4444});
    ASSERT_EQ(4, stack.size());
    ASSERT_EQ(4, stack.capacity());
    ASSERT_FALSE(stack.isStackAlloc());
    ASSERT_EQ(4444, stack.back().value);
    ASSERT_STREQ("hey44", stack.back().name);

    stack.push({.name = "hey55", .value = 5});
    ASSERT_EQ(5, stack.size());
    ASSERT_EQ(6, stack.capacity());
    ASSERT_FALSE(stack.isStackAlloc());
    ASSERT_EQ(5, stack.back().value);
    ASSERT_STREQ("hey55", stack.back().name);

    stack.pop();
    ASSERT_EQ(4, stack.size());
    ASSERT_EQ(6, stack.capacity());
    ASSERT_EQ(4444, stack.back().value);
    ASSERT_STREQ("hey44", stack.back().name);

    stack.pop();
    ASSERT_EQ(3, stack.size());
    ASSERT_EQ(6, stack.capacity());
    ASSERT_EQ(333, stack.back().value);
    ASSERT_STREQ("hey3", stack.back().name);

    stack.pop();
    ASSERT_EQ(2, stack.size());
    ASSERT_EQ(6, stack.capacity());
    ASSERT_FALSE(stack.isStackAlloc());
    ASSERT_EQ(222, stack.back().value);
    ASSERT_STREQ("hey2", stack.back().name);
  }
}

TEST(InlinedStack, nonTrivial) {
  {
    InlinedStack<std::string, 3> stack;
    ASSERT_EQ(0, stack.size());
    ASSERT_EQ(3, stack.capacity());
    ASSERT_TRUE(stack.isStackAlloc());
    stack.push("0_qwertyuiopasdfghjklzxcvbnm");
    stack.push("1_qwertyuiopasdfghjklzxcvbnm");
    ASSERT_EQ(2, stack.size());
    ASSERT_EQ(3, stack.capacity());
    ASSERT_TRUE(stack.isStackAlloc());
    ASSERT_EQ("0_qwertyuiopasdfghjklzxcvbnm", stack.front());
    ASSERT_EQ("1_qwertyuiopasdfghjklzxcvbnm", stack.back());

    stack.pop();
    ASSERT_EQ(1, stack.size());
    stack.push("3_qwertyuiopasdfghjklzxcvbnm");
    ASSERT_EQ("3_qwertyuiopasdfghjklzxcvbnm", stack.back());
    ASSERT_EQ(2, stack.size());
    ASSERT_EQ(3, stack.capacity());
  }

  {
    InlinedStack<std::string, 3> stack;
    ASSERT_EQ(0, stack.size());
    ASSERT_EQ(3, stack.capacity());
    ASSERT_TRUE(stack.isStackAlloc());
    stack.push("0_qwertyuiopasdfghjklzxcvbnm");
    stack.push("1_qwertyuiopasdfghjklzxcvbnm");
    stack.push("2_qwertyuiopasdfghjklzxcvbnm");
    stack.push("3_qwertyuiopasdfghjklzxcvbnm");
    ASSERT_EQ(4, stack.size());
    ASSERT_EQ(4, stack.capacity());
    ASSERT_FALSE(stack.isStackAlloc());

    stack.push("4_qwertyuiopasdfghjklzxcvbnm");
    ASSERT_EQ(5, stack.size());
    ASSERT_EQ(6, stack.capacity());

    ASSERT_EQ("4_qwertyuiopasdfghjklzxcvbnm", stack.back());
    stack.pop();
    ASSERT_EQ("3_qwertyuiopasdfghjklzxcvbnm", stack.back());
    stack.push("5_qwertyuiopasdfghjklzxcvbnm");
    ASSERT_EQ("5_qwertyuiopasdfghjklzxcvbnm", stack.back());
  }
}

TEST(BitSetTest, base) {
  {
    BitSet set(0);
    ASSERT_EQ(0, set.size());
    ASSERT_EQ(0, set.count());
    set.set();
    ASSERT_EQ(0, set.count());
  }

  {
    BitSet set(1);
    ASSERT_EQ(1, set.size());
    set.set();
    ASSERT_TRUE(set.test(0));
    ASSERT_EQ(1, set.count());
    set.reset(0);
    ASSERT_FALSE(set.test(0));
  }

  {
    BitSet set(63);
    ASSERT_EQ(63, set.size());
    set.set();
    ASSERT_TRUE(set.test(0));
    ASSERT_EQ(63, set.count());
    set.reset(0).reset(3);
    ASSERT_FALSE(set.test(0));
    ASSERT_FALSE(set.test(3));
    ASSERT_EQ(61, set.count());
  }

  {
    BitSet set(64);
    ASSERT_EQ(64, set.size());
    set.set();
    ASSERT_TRUE(set.test(0));
    ASSERT_EQ(64, set.count());
    set.reset(0).reset(3);
    ASSERT_FALSE(set.test(0));
    ASSERT_FALSE(set.test(3));
    ASSERT_EQ(62, set.count());
  }

  {
    BitSet set(65);
    ASSERT_EQ(65, set.size());
    set.set();
    ASSERT_TRUE(set.test(0));
    ASSERT_EQ(65, set.count());
    set.reset(0).reset(3).reset(63).reset(64);
    ASSERT_FALSE(set.test(0));
    ASSERT_FALSE(set.test(3));
    ASSERT_FALSE(set.test(63));
    ASSERT_FALSE(set.test(64));
    ASSERT_EQ(61, set.count());
  }

  {
    BitSet set(100);
    ASSERT_EQ(100, set.size());
    set.set();
    ASSERT_TRUE(set.test(0));
    ASSERT_EQ(100, set.count());
    set.reset(0).reset(3).reset(64).reset(65).reset(81);
    ASSERT_FALSE(set.test(0));
    ASSERT_FALSE(set.test(3));
    ASSERT_FALSE(set.test(64));
    ASSERT_FALSE(set.test(65));
    ASSERT_FALSE(set.test(81));
    ASSERT_EQ(95, set.count());

    BitSet set2(set);
    ASSERT_EQ(100, set2.size());
    ASSERT_EQ(95, set2.count());
    set2.reset(63).reset(99);
    ASSERT_FALSE(set2.test(63));
    ASSERT_FALSE(set2.test(99));
    ASSERT_EQ(93, set2.count());
    ASSERT_EQ(95, set.count());
  }
}

TEST(BitSetTest, countZero) {
  {
    BitSet set(129);
    ASSERT_EQ(129, set.countTailZero());
    for (int i = 128; i > -1; i--) {
      set.set(i);
      ASSERT_EQ(i, set.countTailZero());
      set.reset(i);
    }
  }

  {
    BitSet set(129);
    set.set(17);
    ASSERT_EQ(17, set.countTailZero());
  }

  {
    BitSet set(63);
    ASSERT_EQ(63, set.countTailZero());
    for (int i = 62; i > -1; i--) {
      set.set(i);
      ASSERT_EQ(i, set.countTailZero());
      set.reset(i);
    }
  }

  {
    BitSet set(64);
    ASSERT_EQ(64, set.countTailZero());
    for (int i = 63; i > -1; i--) {
      set.set(i);
      ASSERT_EQ(i, set.countTailZero());
      set.reset(i);
    }
  }

  for (int i = 0; i < 132; i++) {
    BitSet set(i);
    ASSERT_EQ(i, set.countTailZero());
    for (int j = i - 1; j > -1; j--) {
      set.set(j);
      ASSERT_EQ(j, set.countTailZero());
      set.reset(j);
    }
  }
}

TEST(BitSetTest, iterate) {
  BitSet set(137);
  ASSERT_EQ(137, set.size());
  ASSERT_EQ(set.size(), set.nextSetBit(0));
  set.set(0);
  set.set(23);
  set.set(49);
  set.set(67);
  set.set(91);
  set.set(129);
  ASSERT_EQ(0, set.nextSetBit(0));
  ASSERT_EQ(23, set.nextSetBit(1));
  ASSERT_EQ(23, set.nextSetBit(23));
  ASSERT_EQ(49, set.nextSetBit(24));
  ASSERT_EQ(67, set.nextSetBit(50));
  ASSERT_EQ(91, set.nextSetBit(68));
  ASSERT_EQ(129, set.nextSetBit(92));
  ASSERT_EQ(137, set.nextSetBit(130)); // not set

  // iterate api
  std::vector<size_t> setIndexes;
  set.iterateSetBit([&setIndexes](size_t index) {
    setIndexes.push_back(index);
    return true;
  });
  ASSERT_EQ(6, setIndexes.size());
  ASSERT_EQ(0, setIndexes[0]);
  ASSERT_EQ(23, setIndexes[1]);
  ASSERT_EQ(49, setIndexes[2]);
  ASSERT_EQ(67, setIndexes[3]);
  ASSERT_EQ(91, setIndexes[4]);
  ASSERT_EQ(129, setIndexes[5]);

  ASSERT_EQ(0, set.getNthSetBitIndex(0));
  ASSERT_EQ(23, set.getNthSetBitIndex(1));
  ASSERT_EQ(49, set.getNthSetBitIndex(2));
  ASSERT_EQ(67, set.getNthSetBitIndex(3));
  ASSERT_EQ(91, set.getNthSetBitIndex(4));
  ASSERT_EQ(129, set.getNthSetBitIndex(5));
  set.set(136);
  ASSERT_EQ(136, set.getNthSetBitIndex(6));
  ASSERT_TRUE(set.test(136));
  ASSERT_EQ(136, set.getNthSetBitIndex(7));
  ASSERT_FALSE(set.test(137));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
