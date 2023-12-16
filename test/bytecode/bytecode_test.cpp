#include "gtest/gtest.h"

#include <misc/emitter.hpp>

using namespace arsh;
using ByteCodeWriter = arsh::CodeEmitter<true>;

TEST(writer, api1) {
  ByteCodeWriter writer;

  writer.codeBuffer += 33;
  writer.codeBuffer += 33;

  // 8 bit
  writer.emit8(0, 55);
  ASSERT_EQ(55u, writer.codeBuffer[0]);

  writer.emit8(1, 90);
  ASSERT_EQ(90u, writer.codeBuffer[1]);

  // 16 bit
  writer.codeBuffer += 0;
  writer.codeBuffer += 0;

  writer.emit16(0, 8);
  ASSERT_EQ(0u, writer.codeBuffer[0]);
  ASSERT_EQ(8u, writer.codeBuffer[1]);

  writer.emit16(2, 6000);
  ASSERT_EQ(0x17, writer.codeBuffer[2]);
  ASSERT_EQ(0x70, writer.codeBuffer[3]);

  writer.emit16(1, 6000);
  ASSERT_EQ(0x00, writer.codeBuffer[0]);
  ASSERT_EQ(0x17, writer.codeBuffer[1]);
  ASSERT_EQ(0x70, writer.codeBuffer[2]);
  ASSERT_EQ(0x70, writer.codeBuffer[3]);

  // 32 bit
  writer.codeBuffer += 45;

  writer.emit32(0, 0x12345678);
  ASSERT_EQ(0x12, writer.codeBuffer[0]);
  ASSERT_EQ(0x34, writer.codeBuffer[1]);
  ASSERT_EQ(0x56, writer.codeBuffer[2]);
  ASSERT_EQ(0x78, writer.codeBuffer[3]);
  ASSERT_EQ(45u, writer.codeBuffer[4]);

  writer.emit32(1, 0x12345678);
  ASSERT_EQ(0x12, writer.codeBuffer[0]);
  ASSERT_EQ(0x12, writer.codeBuffer[1]);
  ASSERT_EQ(0x34, writer.codeBuffer[2]);
  ASSERT_EQ(0x56, writer.codeBuffer[3]);
  ASSERT_EQ(0x78, writer.codeBuffer[4]);
}

TEST(writer, api2) {
  ByteCodeWriter writer;

  for (unsigned int i = 0; i < 4; i++) {
    writer.codeBuffer += 0;
  }

  // 8 bit
  writer.emit(0, 25);
  ASSERT_EQ(25u, writer.codeBuffer[0]);
  ASSERT_EQ(0u, writer.codeBuffer[1]);
  ASSERT_EQ(0u, writer.codeBuffer[2]);
  ASSERT_EQ(0u, writer.codeBuffer[3]);

  writer.emit(0, 255);
  ASSERT_EQ(255u, writer.codeBuffer[0]);
  ASSERT_EQ(0u, writer.codeBuffer[1]);
  ASSERT_EQ(0u, writer.codeBuffer[2]);
  ASSERT_EQ(0u, writer.codeBuffer[3]);

  // 16 bit
  for (unsigned int i = 0; i < 4; i++) {
    writer.codeBuffer[i] = 4;
  }

  writer.emit(0, 256);
  ASSERT_EQ(0x1, writer.codeBuffer[0]);
  ASSERT_EQ(0u, writer.codeBuffer[1]);
  ASSERT_EQ(4u, writer.codeBuffer[2]);
  ASSERT_EQ(4u, writer.codeBuffer[3]);

  writer.emit(0, 0xFFFF);
  ASSERT_EQ(0xFF, writer.codeBuffer[0]);
  ASSERT_EQ(0xFF, writer.codeBuffer[1]);
  ASSERT_EQ(4u, writer.codeBuffer[2]);
  ASSERT_EQ(4u, writer.codeBuffer[3]);

  // 32 bit
  writer.emit(0, 0x10000);
  ASSERT_EQ(0x00, writer.codeBuffer[0]);
  ASSERT_EQ(0x01, writer.codeBuffer[1]);
  ASSERT_EQ(0x00, writer.codeBuffer[2]);
  ASSERT_EQ(0x00, writer.codeBuffer[3]);

  writer.emit(0, 0xFFFFFFFF);
  ASSERT_EQ(0xFF, writer.codeBuffer[0]);
  ASSERT_EQ(0xFF, writer.codeBuffer[1]);
  ASSERT_EQ(0xFF, writer.codeBuffer[2]);
  ASSERT_EQ(0xFF, writer.codeBuffer[3]);

  // 64 bit
  for (unsigned int i = 0; i < 4; i++) {
    writer.codeBuffer += 45;
  }

  writer.emit(0, -1);
  for (unsigned int i = 0; i < 8; i++) {
    ASSERT_EQ(0xFF, writer.codeBuffer[i]);
  }

  writer.emit(0, 0xFF12345678901234);
  ASSERT_EQ(0xFF, writer.codeBuffer[0]);
  ASSERT_EQ(0x12, writer.codeBuffer[1]);
  ASSERT_EQ(0x34, writer.codeBuffer[2]);
  ASSERT_EQ(0x56, writer.codeBuffer[3]);
  ASSERT_EQ(0x78, writer.codeBuffer[4]);
  ASSERT_EQ(0x90, writer.codeBuffer[5]);
  ASSERT_EQ(0x12, writer.codeBuffer[6]);
  ASSERT_EQ(0x34, writer.codeBuffer[7]);
}

TEST(writer, api3) {
  // 8 bit
  {
    ByteCodeWriter writer;
    writer.append(255);
    ASSERT_EQ(255u, writer.codeBuffer[0]);
  }

  // 16 bit
  {
    ByteCodeWriter writer;
    writer.append(256);
    ASSERT_EQ(0x1, writer.codeBuffer[0]);
    ASSERT_EQ(0u, writer.codeBuffer[1]);

    writer.append(0xFFFF);
    ASSERT_EQ(0xFF, writer.codeBuffer[2]);
    ASSERT_EQ(0xFF, writer.codeBuffer[3]);
  }

  // 32 bit
  {
    ByteCodeWriter writer;
    writer.append(0x10000);
    ASSERT_EQ(0x00, writer.codeBuffer[0]);
    ASSERT_EQ(0x01, writer.codeBuffer[1]);
    ASSERT_EQ(0x00, writer.codeBuffer[2]);
    ASSERT_EQ(0x00, writer.codeBuffer[3]);

    writer.append(0xFFFFFFFF);
    ASSERT_EQ(0xFF, writer.codeBuffer[4]);
    ASSERT_EQ(0xFF, writer.codeBuffer[5]);
    ASSERT_EQ(0xFF, writer.codeBuffer[6]);
    ASSERT_EQ(0xFF, writer.codeBuffer[7]);
  }

  // 64 bit
  {
    ByteCodeWriter writer;
    writer.append(-1);
    for (unsigned int i = 0; i < 8; i++) {
      ASSERT_EQ(0xFF, writer.codeBuffer[i]);
    }

    writer.append(0xFF12345678901234);
    ASSERT_EQ(0xFF, writer.codeBuffer[8]);
    ASSERT_EQ(0x12, writer.codeBuffer[9]);
    ASSERT_EQ(0x34, writer.codeBuffer[10]);
    ASSERT_EQ(0x56, writer.codeBuffer[11]);
    ASSERT_EQ(0x78, writer.codeBuffer[12]);
    ASSERT_EQ(0x90, writer.codeBuffer[13]);
    ASSERT_EQ(0x12, writer.codeBuffer[14]);
    ASSERT_EQ(0x34, writer.codeBuffer[15]);
  }
}

TEST(writer, read) {
  ByteCodeWriter writer;

  for (unsigned int i = 0; i < 4; i++) {
    writer.codeBuffer += 0;
  }

  // 8 bit
  {
    const unsigned char v = 68;
    writer.emit(0, v);
    ASSERT_EQ(v, arsh::read8(writer.codeBuffer.data(), 0));
  }

  // 16 bit
  {
    const unsigned short v = 2784;
    writer.emit(1, v);
    ASSERT_EQ(v, arsh::read16(writer.codeBuffer.data(), 1));
  }

  // 32 bit
  {
    const unsigned int v = 0x12345678;
    writer.emit(0, v);
    ASSERT_EQ(v, arsh::read32(writer.codeBuffer.data(), 0));
  }

  // 64 bit
  for (unsigned int i = 0; i < 4; i++) {
    writer.codeBuffer += 34;
  }
  {
    const auto v = static_cast<uint64_t>(-456789);
    writer.emit(0, v);
    ASSERT_EQ(v, arsh::read64(writer.codeBuffer.data(), 0));
  }
}

TEST(writer, label1) {
  ByteCodeWriter writer;

  writer.codeBuffer.reserve(static_cast<unsigned short>(-1) + 40000);
  for (unsigned int i = 0; i < writer.codeBuffer.capacity(); i++) {
    writer.codeBuffer += 0;
  }

  // forward
  auto label = makeLabel();
  writer.emit(0, 25u);
  writer.writeLabel(1, label, 0, ByteCodeWriter::LabelTarget::_8);
  writer.emit(2, 26u);
  writer.emit(3, 27u);
  writer.markLabel(3, label);

  writer.finalize();
  ASSERT_EQ(3u, label->getIndex());
  ASSERT_EQ(25u, writer.codeBuffer[0]);
  ASSERT_EQ(3u, writer.codeBuffer[1]);
  ASSERT_EQ(26u, writer.codeBuffer[2]);
  ASSERT_EQ(27u, writer.codeBuffer[3]);

  // backward
  label = makeLabel();
  writer.emit(0, 77u);
  writer.emit(1, 78u);
  writer.markLabel(1, label);
  writer.emit(2, 79u);
  writer.writeLabel(3, label, 0, ByteCodeWriter::LabelTarget::_8);
  writer.emit(4, 80u);

  writer.finalize();
  ASSERT_EQ(1u, label->getIndex());
  ASSERT_EQ(77u, writer.codeBuffer[0]);
  ASSERT_EQ(78u, writer.codeBuffer[1]);
  ASSERT_EQ(79u, writer.codeBuffer[2]);
  ASSERT_EQ(1u, writer.codeBuffer[3]);
  ASSERT_EQ(80u, writer.codeBuffer[4]);

  // multiple
  label = makeLabel();
  for (unsigned int i = 0; i < 10; i++) {
    writer.emit(i, 88u);
  }

  writer.markLabel(4, label);
  for (unsigned int i = 0; i < 10; i++) {
    if (i % 2 != 0) {
      writer.writeLabel(i, label, 0, ByteCodeWriter::LabelTarget::_8);
    }
  }

  writer.finalize();
  ASSERT_EQ(4u, label->getIndex());
  for (unsigned int i = 0; i < 5; i++) {
    ASSERT_EQ(88u, writer.codeBuffer[i * 2]);
  }
  for (unsigned int i = 0; i < 5; i++) {
    ASSERT_EQ(4u, writer.codeBuffer[i * 2 + 1]);
  }
}

TEST(writer, label2) {
  ByteCodeWriter writer;

  writer.codeBuffer.insert(writer.codeBuffer.end(), 8, 0);

  auto label = makeLabel();
  writer.writeLabel(3, label, 1, ByteCodeWriter::LabelTarget::_8);
  writer.markLabel(6, label);

  writer.finalize();
  ASSERT_EQ(0x00, writer.codeBuffer[0]);
  ASSERT_EQ(0x00, writer.codeBuffer[1]);
  ASSERT_EQ(0x00, writer.codeBuffer[2]);
  ASSERT_EQ(0x05, writer.codeBuffer[3]);
  ASSERT_EQ(0x00, writer.codeBuffer[4]);
  ASSERT_EQ(0x00, writer.codeBuffer[5]);
  ASSERT_EQ(0x00, writer.codeBuffer[6]);
  ASSERT_EQ(0x00, writer.codeBuffer[7]);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
