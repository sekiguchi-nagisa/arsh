//
// Created by skgchxngsxyz-carbon on 16/12/25.
//

#ifndef YDSH_TEST_COMMON_HPP
#define YDSH_TEST_COMMON_HPP

#include <cstdlib>

// common utility for test

class TempFileFactory {
protected:
    std::string tmpFileName;

    TempFileFactory() = default;
    virtual ~TempFileFactory() = default;

    void createTemp() {
        const char *tmpdir = getenv("TMPDIR");
        if(tmpdir == nullptr) {
            tmpdir = "/tmp";
        }
        unsigned int size = 512;
        char name[size];
        snprintf(name, size, "%s/exec_test_tmpXXXXXX", tmpdir);

        int fd = mkstemp(name);
        close(fd);
        this->tmpFileName = name;
    }

    void deleteTemp() {
        remove(this->tmpFileName.c_str());
    }
};

#define ASSERT_(F) do { SCOPED_TRACE(""); F; } while(false)

#endif //YDSH_TEST_COMMON_HPP
