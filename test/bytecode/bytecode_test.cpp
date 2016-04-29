#include <gtest/gtest.h>

#include <misc/bytecode_writer.hpp>

using namespace ydsh::misc;

TEST(writer, api1) {
    ByteCodeWriter writer;

    writer.code += 33;
    writer.code += 33;

    // 8 bit
    writer.write8(0, 55);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(55u, writer.code[0]));

    writer.write8(1, 90);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(90u, writer.code[1]));

    // 16 bit
    writer.code += 0;
    writer.code += 0;

    writer.write16(0, 8);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.code[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(8u, writer.code[1]));

    writer.write16(2, 6000);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x17, writer.code[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x70, writer.code[3]));

    writer.write16(1, 6000);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.code[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x17, writer.code[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x70, writer.code[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x70, writer.code[3]));

    // 32 bit
    writer.code += 45;

    writer.write32(0, 0x12345678);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x12, writer.code[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x34, writer.code[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x56, writer.code[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x78, writer.code[3]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(45u, writer.code[4]));

    writer.write32(1, 0x12345678);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x12, writer.code[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x12, writer.code[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x34, writer.code[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x56, writer.code[3]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x78, writer.code[4]));
}

TEST(writer, api2) {
    ByteCodeWriter writer;

    for(unsigned int i = 0; i < 4; i++) {
        writer.code += 0;
    }

    // 8 bit
    writer.write(0, 25);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(25u, writer.code[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.code[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.code[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.code[3]));

    writer.write(0, 255);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(255u, writer.code[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.code[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.code[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.code[3]));

    // 16 bit
    for(unsigned int i = 0; i < 4; i++) {
        writer.code[i] = 4;
    }

    writer.write(0, 256);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x1, writer.code[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.code[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, writer.code[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, writer.code[3]));

    writer.write(0, 0xFFFF);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.code[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.code[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, writer.code[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, writer.code[3]));

    // 32 bit
    writer.write(0, 0x10000);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.code[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x01, writer.code[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.code[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.code[3]));

    writer.write(0, 0xFFFFFFFF);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.code[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.code[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.code[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.code[3]));
}

TEST(writer, label1) {
    ByteCodeWriter writer;

    writer.code.reserve(static_cast<unsigned short>(-1) + 40000);
    for(unsigned int i = 0; i < writer.code.capacity(); i++) {
        writer.code += 0;
    }

    // forward
    Label label;
    writer.write(0, 25u);
    writer.writeLabel(1, label);
    writer.write(2, 26u);
    writer.write(3, 27u);
    writer.markLabel(3, label);

    writer.finalize();
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, label.getIndex()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(25u, writer.code[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, writer.code[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(26u, writer.code[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(27u, writer.code[3]));

    // backward
    label = Label();
    writer.write(0, 77u);
    writer.write(1, 78u);
    writer.markLabel(1, label);
    writer.write(2, 79u);
    writer.writeLabel(3, label);
    writer.write(4, 80u);

    writer.finalize();
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, label.getIndex()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(77u, writer.code[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(78u, writer.code[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(79u, writer.code[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, writer.code[3]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(80u, writer.code[4]));

    // multiple
    label = Label();
    for(unsigned int i = 0; i < 10; i++) {
        writer.write(i, 88u);
    }

    writer.markLabel(4, label);
    for(unsigned int i = 0; i < 10; i++) {
        if(i % 2 != 0) {
            writer.writeLabel(i, label);
        }
    }

    writer.finalize();
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, label.getIndex()));
    for(unsigned int i = 0; i < 5; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(88u, writer.code[i * 2]));
    }
    for(unsigned int i = 0; i < 5; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, writer.code[i * 2 + 1]));
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
