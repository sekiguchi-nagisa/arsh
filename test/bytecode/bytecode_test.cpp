#include <gtest/gtest.h>

#include <core/codegen.h>

using Label = ydsh::Label;
using ByteCodeWriter = ydsh::ByteCodeWriter<true>;

TEST(writer, api1) {
    ByteCodeWriter writer;

    writer.codeBuffer += 33;
    writer.codeBuffer += 33;

    // 8 bit
    writer.write8(0, 55);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(55u, writer.codeBuffer[0]));

    writer.write8(1, 90);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(90u, writer.codeBuffer[1]));

    // 16 bit
    writer.codeBuffer += 0;
    writer.codeBuffer += 0;

    writer.write16(0, 8);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.codeBuffer[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(8u, writer.codeBuffer[1]));

    writer.write16(2, 6000);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x17, writer.codeBuffer[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x70, writer.codeBuffer[3]));

    writer.write16(1, 6000);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.codeBuffer[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x17, writer.codeBuffer[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x70, writer.codeBuffer[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x70, writer.codeBuffer[3]));

    // 32 bit
    writer.codeBuffer += 45;

    writer.write32(0, 0x12345678);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x12, writer.codeBuffer[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x34, writer.codeBuffer[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x56, writer.codeBuffer[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x78, writer.codeBuffer[3]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(45u, writer.codeBuffer[4]));

    writer.write32(1, 0x12345678);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x12, writer.codeBuffer[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x12, writer.codeBuffer[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x34, writer.codeBuffer[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x56, writer.codeBuffer[3]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x78, writer.codeBuffer[4]));
}

TEST(writer, api2) {
    ByteCodeWriter writer;

    for(unsigned int i = 0; i < 4; i++) {
        writer.codeBuffer += 0;
    }

    // 8 bit
    writer.write(0, 25);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(25u, writer.codeBuffer[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.codeBuffer[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.codeBuffer[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.codeBuffer[3]));

    writer.write(0, 255);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(255u, writer.codeBuffer[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.codeBuffer[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.codeBuffer[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.codeBuffer[3]));

    // 16 bit
    for(unsigned int i = 0; i < 4; i++) {
        writer.codeBuffer[i] = 4;
    }

    writer.write(0, 256);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x1, writer.codeBuffer[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.codeBuffer[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, writer.codeBuffer[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, writer.codeBuffer[3]));

    writer.write(0, 0xFFFF);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, writer.codeBuffer[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, writer.codeBuffer[3]));

    // 32 bit
    writer.write(0, 0x10000);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.codeBuffer[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x01, writer.codeBuffer[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.codeBuffer[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.codeBuffer[3]));

    writer.write(0, 0xFFFFFFFF);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[3]));

    // 64 bit
    for(unsigned int i = 0; i < 4; i++) {
        writer.codeBuffer += 45;
    }

    writer.write(0, -1);
    for(unsigned int i = 0; i < 8; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[i]));
    }

    writer.write(0, 0xFF12345678901234);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x12, writer.codeBuffer[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x34, writer.codeBuffer[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x56, writer.codeBuffer[3]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x78, writer.codeBuffer[4]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x90, writer.codeBuffer[5]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x12, writer.codeBuffer[6]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x34, writer.codeBuffer[7]));
}

TEST(writer, api3) {
    // 8 bit
    {
        ByteCodeWriter writer;
        writer.append(255);
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(255u, writer.codeBuffer[0]));
    }

    // 16 bit
    {
        ByteCodeWriter writer;
        writer.append(256);
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x1, writer.codeBuffer[0]));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, writer.codeBuffer[1]));

        writer.append(0xFFFF);
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[2]));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[3]));
    }

    // 32 bit
    {
        ByteCodeWriter writer;
        writer.append(0x10000);
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.codeBuffer[0]));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x01, writer.codeBuffer[1]));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.codeBuffer[2]));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.codeBuffer[3]));

        writer.append(0xFFFFFFFF);
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[4]));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[5]));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[6]));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[7]));
    }

    // 64 bit
    {
        ByteCodeWriter writer;
        writer.append(-1);
        for(unsigned int i = 0; i < 8; i++) {
            ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[i]));
        }

        writer.append(0xFF12345678901234);
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0xFF, writer.codeBuffer[8]));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x12, writer.codeBuffer[9]));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x34, writer.codeBuffer[10]));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x56, writer.codeBuffer[11]));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x78, writer.codeBuffer[12]));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x90, writer.codeBuffer[13]));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x12, writer.codeBuffer[14]));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x34, writer.codeBuffer[15]));
    }
}

TEST(writer, read) {
    ByteCodeWriter writer;

    for(unsigned int i = 0; i < 4; i++) {
        writer.codeBuffer += 0;
    }

    // 8 bit
    {
        const unsigned char v = 68;
        writer.write(0, v);
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(v, ydsh::read8(writer.codeBuffer.get(), 0)));
    }

    // 16 bit
    {
        const unsigned short v = 2784;
        writer.write(1, v);
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(v, ydsh::read16(writer.codeBuffer.get(), 1)));
    }

    // 32 bit
    {
        const unsigned int v = 0x12345678;
        writer.write(0, v);
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(v, ydsh::read32(writer.codeBuffer.get(), 0)));
    }

    // 64 bit
    for(unsigned int i = 0; i < 4; i++) {
        writer.codeBuffer += 34;
    }
    {
        const unsigned long v = static_cast<unsigned long>(-456789);
        writer.write(0, v);
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(v, ydsh::read64(writer.codeBuffer.get(), 0)));
    }
}

TEST(writer, label1) {
    ByteCodeWriter writer;

    writer.codeBuffer.reserve(static_cast<unsigned short>(-1) + 40000);
    for(unsigned int i = 0; i < writer.codeBuffer.capacity(); i++) {
        writer.codeBuffer += 0;
    }

    // forward
    auto label = ydsh::makeIntrusive<Label>();
    writer.write(0, 25u);
    writer.writeLabel(1, label, 0, ByteCodeWriter::LabelTarget::_8);
    writer.write(2, 26u);
    writer.write(3, 27u);
    writer.markLabel(3, label);

    writer.finalize();
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, label->getIndex()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(25u, writer.codeBuffer[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, writer.codeBuffer[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(26u, writer.codeBuffer[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(27u, writer.codeBuffer[3]));

    // backward
    label = ydsh::makeIntrusive<Label>();
    writer.write(0, 77u);
    writer.write(1, 78u);
    writer.markLabel(1, label);
    writer.write(2, 79u);
    writer.writeLabel(3, label, 0, ByteCodeWriter::LabelTarget::_8);
    writer.write(4, 80u);

    writer.finalize();
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, label->getIndex()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(77u, writer.codeBuffer[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(78u, writer.codeBuffer[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(79u, writer.codeBuffer[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, writer.codeBuffer[3]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(80u, writer.codeBuffer[4]));

    // multiple
    label = ydsh::makeIntrusive<Label>();
    for(unsigned int i = 0; i < 10; i++) {
        writer.write(i, 88u);
    }

    writer.markLabel(4, label);
    for(unsigned int i = 0; i < 10; i++) {
        if(i % 2 != 0) {
            writer.writeLabel(i, label, 0, ByteCodeWriter::LabelTarget::_8);
        }
    }

    writer.finalize();
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, label->getIndex()));
    for(unsigned int i = 0; i < 5; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(88u, writer.codeBuffer[i * 2]));
    }
    for(unsigned int i = 0; i < 5; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, writer.codeBuffer[i * 2 + 1]));
    }
}

TEST(writer, label2) {
    ByteCodeWriter writer;

    writer.codeBuffer.assign(8, 0);

    auto label = ydsh::makeIntrusive<Label>();
    writer.writeLabel(3, label, 1, ByteCodeWriter::LabelTarget::_8);
    writer.markLabel(6, label);

    writer.finalize();
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.codeBuffer[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.codeBuffer[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.codeBuffer[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x05, writer.codeBuffer[3]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.codeBuffer[4]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.codeBuffer[5]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.codeBuffer[6]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x00, writer.codeBuffer[7]));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
