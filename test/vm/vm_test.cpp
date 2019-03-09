#include "gtest/gtest.h"

#include <functional>

#include <vm.h>
#include <ydsh/ydsh.h>

#include "../test_common.h"

using BreakPointHandler = std::function<void()>;

class VMInspector : public VMHook {
private:
    OpCode breakOp;
    BreakPointHandler handler;
    bool called;

public:
    VMInspector() : breakOp(OpCode::HALT), handler(), called(true) {}

    void setHandler(OpCode op, BreakPointHandler &&handler) {
        this->breakOp = op;
        this->handler = std::move(handler);
        if(this->handler) {
            this->called = false;
        }
    }

    bool getCalled() const {
        return this->called;
    }

    void vmFetchHook(DSState &, OpCode op) override {
        if(this->breakOp == op) {
            if(this->handler) {
                this->handler();
                this->called = true;
            }
        }
    }

    void vmThrowHook(DSState &) override {}
};

class VMTest : public ::testing::Test {
protected:
    DSState *state;
    VMInspector inspector;

public:
    VMTest() : state(nullptr), inspector() {}
    ~VMTest() override = default;

    void SetUp() override {
        this->state = DSState_create();
        this->state->setVMHook(&this->inspector);
    }

    void TearDown() override {
        DSState_delete(&this->state);
    }

private:
    void setBreakPointHandler(OpCode breakOp, BreakPointHandler &&handler) {
        this->inspector.setHandler(breakOp, std::move(handler));
    }

protected:
    void eval(const char *code, DSErrorKind kind = DS_ERROR_KIND_SUCCESS) {
        DSError e;
        DSState_eval(this->state, "(dummy)", code, strlen(code), &e);
        auto actualKind = e.kind;
        DSError_release(&e);
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(kind, actualKind));
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->inspector.getCalled()));
    }

    void eval(const char *code, DSErrorKind kind, OpCode breakOp, BreakPointHandler &&handler) {
        this->setBreakPointHandler(breakOp, std::move(handler));
        this->eval(code, kind);
    }

    DSValue getValue(const char *name) const {
        auto handle = this->state->symbolTable.lookupHandle(name);
        if(handle == nullptr) {
            return nullptr;
        }
        return this->state->getGlobal(handle->getIndex());
    }

    void RefCount(const char *gvarName, unsigned int refCount) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(gvarName != nullptr));

        auto *handle = this->state->symbolTable.lookupHandle(gvarName);
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(handle != nullptr));

        auto &v = this->state->getGlobal(handle->getIndex());
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(v.isObject()));

        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(refCount, v->getRefcount()));
    }
};

TEST_F(VMTest, base) {
    ASSERT_(this->eval("12", DS_ERROR_KIND_SUCCESS, OpCode::POP, [&]{
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(12, typeAs<Int_Object>(this->state->peek())->getValue()));
    }));
}

TEST_F(VMTest, deinit1) {
    ASSERT_(this->eval("var a = new [Int]()"));
    ASSERT_(RefCount("a", 1));

    ASSERT_(this->eval("{ var b = $a}"));
    ASSERT_(RefCount("a", 1));

    ASSERT_(this->eval("{ var b = $a; if $true { var c = $a }; $RANDOM; }", DS_ERROR_KIND_SUCCESS, OpCode::RAND, [&]{
        ASSERT_(RefCount("a", 2));
    }));
}

TEST_F(VMTest, deinit2) {
    ASSERT_(this->eval("{ var b = $@; throw 34; }", DS_ERROR_KIND_RUNTIME_ERROR));
    ASSERT_(RefCount("@", 1));

    ASSERT_(this->eval("{ var a = $@; { var b = $@; var c = $b; throw 34; }}", DS_ERROR_KIND_RUNTIME_ERROR));
    ASSERT_(RefCount("@", 1));
}

TEST_F(VMTest, deinit3) {
    ASSERT_(this->eval("var i = 0; while $i < 2 { var b = $@; $i++ }"));
    ASSERT_(RefCount("@", 1));

    ASSERT_(this->eval("while $true { var b = $@; break; }"));
    ASSERT_(RefCount("@", 1));

    ASSERT_(this->eval("for(var i = $@; $true;) { var b = $i; break; }"));
    ASSERT_(RefCount("@", 1));

    ASSERT_(this->eval("for(var i = 0; $i < 3; $i++) { var b = $@; continue; }"));
    ASSERT_(RefCount("@", 1));
}

TEST_F(VMTest, deinit4) {
    ASSERT_(this->eval("function f($a : [String]) { $RANDOM; var b = $a; }; $f($@)",
                       DS_ERROR_KIND_SUCCESS, OpCode::RAND, [&] {
                ASSERT_(RefCount("@", 2));
            }));
}

TEST_F(VMTest, deinit5) {
    ASSERT_(this->eval("function f($a : [String]) { var b = $a; { var c = $b; $RANDOM; }; var c = $b; }; $f($@)",
                       DS_ERROR_KIND_SUCCESS, OpCode::RAND, [&]{
                ASSERT_(RefCount("@", 4));
            }));
}

TEST_F(VMTest, deinit6) {
    ASSERT_(this->eval("function f($a : [String]) { var b = $a; { var c = $b }; $RANDOM; var c = $b; }; $f($@)",
                       DS_ERROR_KIND_SUCCESS, OpCode::RAND, [&] {
                ASSERT_(RefCount("@", 3));
            }));
}

TEST_F(VMTest, deinit7) {
    ASSERT_(this->eval("try { var a = $@; 34 / 0; var b = $a; } catch($e) {}"));
    ASSERT_(RefCount("@", 1));

    ASSERT_(this->eval("try { while $true { var a = $@; break; } } finally {  $RANDOM; }",
                       DS_ERROR_KIND_SUCCESS, OpCode::RAND, [&]{
                ASSERT_(RefCount("@", 1));
            }));
}

TEST_F(VMTest, deinit8) {
    ASSERT_(this->eval("try { var a = $@; 34 / 0 } catch $e { $RANDOM; }", DS_ERROR_KIND_SUCCESS, OpCode::RAND, [&]{
        ASSERT_(RefCount("@", 1));
    }));
}

TEST_F(VMTest, deinit9) {
    ASSERT_(this->eval("try { var a = $@; 34 / 0 } catch $e { var b = $@; throw 34; } finally {  $RANDOM; }",
                    DS_ERROR_KIND_RUNTIME_ERROR, OpCode::RAND, [&]{
                ASSERT_(RefCount("@", 1));
            }));
}

TEST_F(VMTest, deinit10) {
    ASSERT_(this->eval("try { var a = $@; var b = $a; 34 / 0 } catch $e : Int { var b = $@; var c = $b; var d = $c; } finally {  $RANDOM; }",
                       DS_ERROR_KIND_RUNTIME_ERROR, OpCode::RAND, [&]{
                ASSERT_(RefCount("@", 1));
            }));
}

TEST_F(VMTest, sig1) {
    ASSERT_(this->eval("function f($s : Signal) {}"));
    auto func = this->getValue("f");
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(func != nullptr));

    SignalVector v;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, v.getData().size()));

    // not found
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(v.lookup(SIGQUIT) == nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(v.lookup(SIGINT) == nullptr));

    // register
    v.insertOrUpdate(3, func);
    v.insertOrUpdate(1, func);
    v.insertOrUpdate(4, func);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, v.getData().size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, v.getData()[0].first));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3, v.getData()[1].first));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4, v.getData()[2].first));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(func, v.lookup(1)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(func, v.lookup(3)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(func, v.lookup(4)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(v.lookup(2) == nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(v.lookup(5) == nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(v.lookup(-3) == nullptr));

    // update
    auto func1 = this->getValue("SIG_DFL");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(func, v.lookup(3)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(func, func1));
    v.insertOrUpdate(3, func1);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(func1, v.lookup(3)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, v.getData().size()));

    // remove
    v.insertOrUpdate(4, nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, v.getData().size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DSValue(), v.lookup(4)));

    // do nothing
    v.insertOrUpdate(5, nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, v.getData().size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DSValue(), v.lookup(5)));
}

TEST_F(VMTest, abort) {
    ASSERT_(this->eval("var a = 45 / 0;", DS_ERROR_KIND_RUNTIME_ERROR));
    ASSERT_(this->eval("$a;", DS_ERROR_KIND_TYPE_ERROR));
}

static Job newJob() {
    return JobImpl::create(Proc());
}

static JobTable::ConstEntryIter getBeginIter(const JobTable &table) {
    return table.beginJob();
}

static JobTable::ConstEntryIter getEndIter(const JobTable &table) {
    return table.endJob();
}

TEST(JobTable, attach) {
    JobTable jobTable;

    auto job1 = newJob();
    auto job2 = newJob();
    auto job3 = newJob();
    auto job4 = newJob();
    auto job5 = newJob();
    auto job6 = newJob();

    jobTable.attach(job1);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, job1->jobID()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(job1, jobTable.getLatestEntry()));

    jobTable.attach(job2);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, job2->jobID()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(job2, jobTable.getLatestEntry()));

    jobTable.attach(job3);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, job3->jobID()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(job3, jobTable.getLatestEntry()));

    jobTable.attach(job4);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, job4->jobID()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(job4, jobTable.getLatestEntry()));

    jobTable.attach(job5);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(5u, job5->jobID()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(job5, jobTable.getLatestEntry()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(job2, jobTable.detach(2, true)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(job5, jobTable.getLatestEntry()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(job3, jobTable.detach(3, false)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(job5, jobTable.getLatestEntry()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(job5, jobTable.detach(5, true)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(job4, jobTable.getLatestEntry()));

    // job entry layout
    auto begin = getBeginIter(jobTable);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, (*begin)->jobID()));
    ++begin;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, (*begin)->jobID()));
    ++begin;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(getEndIter(jobTable), begin));


    // re-attach
    jobTable.attach(job5);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, job5->jobID()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(job5, jobTable.getLatestEntry()));

    begin = getBeginIter(jobTable);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, (*begin)->jobID()));
    ++begin;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, (*begin)->jobID()));
    ++begin;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, (*begin)->jobID()));
    ++begin;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(getEndIter(jobTable), begin));

    // re-attach
    jobTable.attach(job2);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, job2->jobID()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(job2, jobTable.getLatestEntry()));

    begin = getBeginIter(jobTable);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, (*begin)->jobID()));
    ++begin;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, (*begin)->jobID()));
    ++begin;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, (*begin)->jobID()));
    ++begin;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, (*begin)->jobID()));
    ++begin;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(getEndIter(jobTable), begin));

    // re-attach
    jobTable.attach(job6);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(5u, job6->jobID()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(job6, jobTable.getLatestEntry()));

    begin = getBeginIter(jobTable);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, (*begin)->jobID()));
    ++begin;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, (*begin)->jobID()));
    ++begin;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, (*begin)->jobID()));
    ++begin;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, (*begin)->jobID()));
    ++begin;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(5u, (*begin)->jobID()));
    ++begin;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(getEndIter(jobTable), begin));
}

struct ShebangTest : public ::testing::Test, TempFileFactory {
    void SetUp() override {
        this->createTemp();
    }

    void TearDown() override {
        this->deleteTemp();
    }
};

TEST_F(ShebangTest, case1) {
    ShebangLine line;

    auto filePath = this->createTempFile("temp.sh", "hgoihjreoifhiurea");
    auto ret = line(filePath.c_str());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(ret, ShebangLine::INVALID));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line.getInterpPath(), nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line.getOptionalArg(), nullptr));
}

TEST_F(ShebangTest, case2) {
    ShebangLine line;

    auto filePath = this->createTempFile("temp.sh", "#!   ");
    auto ret = line(filePath.c_str());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(ret, ShebangLine::INVALID));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line.getInterpPath(), nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line.getOptionalArg(), nullptr));

    filePath = this->createTempFile("temp2.sh", "#!\n43");
    ret = line(filePath.c_str());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(ret, ShebangLine::INVALID));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line.getInterpPath(), nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line.getOptionalArg(), nullptr));
}

TEST_F(ShebangTest, case3) {
    ShebangLine line;

    auto filePath = this->createTempFile("temp.sh", "#!/bin/env\n234");
    auto ret = line(filePath.c_str());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(ret, ShebangLine::OK));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(line.getInterpPath(), "/bin/env"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line.getOptionalArg(), nullptr));

    filePath = this->createTempFile("temp2.sh", "#!   /bin/env   \n234");
    ret = line(filePath.c_str());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(ret, ShebangLine::OK));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(line.getInterpPath(), "/bin/env"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line.getOptionalArg(), nullptr));
}

TEST_F(ShebangTest, case4) {
    ShebangLine line;

    auto filePath = this->createTempFile("temp.sh", "#!/bin/env  -a -b\n234");
    auto ret = line(filePath.c_str());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(ret, ShebangLine::OK));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(line.getInterpPath(), "/bin/env"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(line.getOptionalArg(), "-a -b"));

    filePath = this->createTempFile("temp2.sh", "#!   /bin/env   -a  -b\n234");
    ret = line(filePath.c_str());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(ret, ShebangLine::OK));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(line.getInterpPath(), "/bin/env"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(line.getOptionalArg(), "-a  -b"));
}

TEST_F(ShebangTest, case5) {
    ShebangLine line;
    std::string filePath = this->getTempDirName();
    filePath += "/hofihreifr12343.dew";
    auto ret = line(filePath.c_str());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(ret, ShebangLine::NOT_OPEN));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line.getInterpPath(), nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line.getOptionalArg(), nullptr));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}