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
    VMInspector() : breakOp(OpCode::NOP), handler(), called(true) {}

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
        ASSERT_EQ(kind, actualKind);
        ASSERT_TRUE(this->inspector.getCalled());
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
        ASSERT_TRUE(gvarName != nullptr);

        auto *handle = this->state->symbolTable.lookupHandle(gvarName);
        ASSERT_TRUE(handle != nullptr);

        auto &v = this->state->getGlobal(handle->getIndex());
        ASSERT_TRUE(v.isObject());

        ASSERT_EQ(refCount, v->getRefcount());
    }
};

TEST_F(VMTest, base) {
    ASSERT_NO_FATAL_FAILURE(this->eval("12", DS_ERROR_KIND_SUCCESS, OpCode::POP, [&]{
        ASSERT_EQ(12, typeAs<Int_Object>(this->state->peek())->getValue());
    }));
}

TEST_F(VMTest, deinit1) {
    ASSERT_NO_FATAL_FAILURE(this->eval("var a = new [Int]()"));
    ASSERT_NO_FATAL_FAILURE(RefCount("a", 1));

    ASSERT_NO_FATAL_FAILURE(this->eval("{ var b = $a}"));
    ASSERT_NO_FATAL_FAILURE(RefCount("a", 1));

    ASSERT_NO_FATAL_FAILURE(this->eval("{ var b = $a; if $true { var c = $a }; $RANDOM; }", DS_ERROR_KIND_SUCCESS, OpCode::RAND, [&]{
        ASSERT_NO_FATAL_FAILURE(RefCount("a", 2));
    }));
}

TEST_F(VMTest, deinit2) {
    ASSERT_NO_FATAL_FAILURE(this->eval("{ var b = $@; throw 34; }", DS_ERROR_KIND_RUNTIME_ERROR));
    ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));

    ASSERT_NO_FATAL_FAILURE(this->eval("{ var a = $@; { var b = $@; var c = $b; throw 34; }}", DS_ERROR_KIND_RUNTIME_ERROR));
    ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));
}

TEST_F(VMTest, deinit3) {
    ASSERT_NO_FATAL_FAILURE(this->eval("var i = 0; while $i < 2 { var b = $@; $i++ }"));
    ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));

    ASSERT_NO_FATAL_FAILURE(this->eval("while $true { var b = $@; break; }"));
    ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));

    ASSERT_NO_FATAL_FAILURE(this->eval("for(var i = $@; $true;) { var b = $i; break; }"));
    ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));

    ASSERT_NO_FATAL_FAILURE(this->eval("for(var i = 0; $i < 3; $i++) { var b = $@; continue; }"));
    ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));
}

TEST_F(VMTest, deinit4) {
    ASSERT_NO_FATAL_FAILURE(this->eval("function f($a : [String]) { $RANDOM; var b = $a; }; $f($@)",
                       DS_ERROR_KIND_SUCCESS, OpCode::RAND, [&] {
                ASSERT_NO_FATAL_FAILURE(RefCount("@", 2));
            }));
}

TEST_F(VMTest, deinit5) {
    ASSERT_NO_FATAL_FAILURE(this->eval("function f($a : [String]) { var b = $a; { var c = $b; $RANDOM; }; var c = $b; }; $f($@)",
                       DS_ERROR_KIND_SUCCESS, OpCode::RAND, [&]{
                ASSERT_NO_FATAL_FAILURE(RefCount("@", 4));
            }));
}

TEST_F(VMTest, deinit6) {
    ASSERT_NO_FATAL_FAILURE(this->eval("function f($a : [String]) { var b = $a; { var c = $b }; $RANDOM; var c = $b; }; $f($@)",
                       DS_ERROR_KIND_SUCCESS, OpCode::RAND, [&] {
                ASSERT_NO_FATAL_FAILURE(RefCount("@", 3));
            }));
}

TEST_F(VMTest, deinit7) {
    ASSERT_NO_FATAL_FAILURE(this->eval("try { var a = $@; 34 / 0; var b = $a; } catch($e) {}"));
    ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));

    ASSERT_NO_FATAL_FAILURE(this->eval("try { while $true { var a = $@; break; } } finally {  $RANDOM; }",
                       DS_ERROR_KIND_SUCCESS, OpCode::RAND, [&]{
                ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));
            }));
}

TEST_F(VMTest, deinit8) {
    ASSERT_NO_FATAL_FAILURE(this->eval("try { var a = $@; 34 / 0 } catch $e { $RANDOM; }", DS_ERROR_KIND_SUCCESS, OpCode::RAND, [&]{
        ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));
    }));
}

TEST_F(VMTest, deinit9) {
    ASSERT_NO_FATAL_FAILURE(this->eval("try { var a = $@; 34 / 0 } catch $e { var b = $@; throw 34; } finally {  $RANDOM; }",
                    DS_ERROR_KIND_RUNTIME_ERROR, OpCode::RAND, [&]{
                ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));
            }));
}

TEST_F(VMTest, deinit10) {
    ASSERT_NO_FATAL_FAILURE(this->eval("try { var a = $@; var b = $a; 34 / 0 } catch $e : Int { var b = $@; var c = $b; var d = $c; } finally {  $RANDOM; }",
                       DS_ERROR_KIND_RUNTIME_ERROR, OpCode::RAND, [&]{
                ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));
            }));
}

TEST_F(VMTest, sig1) {
    ASSERT_NO_FATAL_FAILURE(this->eval("function f($s : Signal) {}"));
    auto func = this->getValue("f");
    ASSERT_TRUE(func != nullptr);

    SignalVector v;
    ASSERT_EQ(0u, v.getData().size());

    // not found
    ASSERT_TRUE(v.lookup(SIGQUIT) == nullptr);
    ASSERT_TRUE(v.lookup(SIGINT) == nullptr);

    // register
    v.insertOrUpdate(3, func);
    v.insertOrUpdate(1, func);
    v.insertOrUpdate(4, func);
    ASSERT_EQ(3u, v.getData().size());
    ASSERT_EQ(1, v.getData()[0].first);
    ASSERT_EQ(3, v.getData()[1].first);
    ASSERT_EQ(4, v.getData()[2].first);
    ASSERT_EQ(func, v.lookup(1));
    ASSERT_EQ(func, v.lookup(3));
    ASSERT_EQ(func, v.lookup(4));
    ASSERT_TRUE(v.lookup(2) == nullptr);
    ASSERT_TRUE(v.lookup(5) == nullptr);
    ASSERT_TRUE(v.lookup(-3) == nullptr);

    // update
    auto func1 = this->getValue("SIG_DFL");
    ASSERT_EQ(func, v.lookup(3));
    ASSERT_NE(func, func1);
    v.insertOrUpdate(3, func1);
    ASSERT_EQ(func1, v.lookup(3));
    ASSERT_EQ(3u, v.getData().size());

    // remove
    v.insertOrUpdate(4, nullptr);
    ASSERT_EQ(2u, v.getData().size());
    ASSERT_EQ(DSValue(), v.lookup(4));

    // do nothing
    v.insertOrUpdate(5, nullptr);
    ASSERT_EQ(2u, v.getData().size());
    ASSERT_EQ(DSValue(), v.lookup(5));
}

TEST_F(VMTest, abort) {
    ASSERT_NO_FATAL_FAILURE(this->eval("var a = 45 / 0;", DS_ERROR_KIND_RUNTIME_ERROR));
    ASSERT_NO_FATAL_FAILURE(this->eval("$a;", DS_ERROR_KIND_TYPE_ERROR));
}

static JobTable::ConstEntryIter getBeginIter(const JobTable &table) {
    return table.beginJob();
}

static JobTable::ConstEntryIter getEndIter(const JobTable &table) {
    return table.endJob();
}

struct JobTableTest : public VMTest {
    Job newJob() {
        return JobTable::create(
                this->state->symbolTable.get(TYPE::Job), Proc(),
                DSValue(this->state->emptyFDObj),
                DSValue(this->state->emptyFDObj));
    }
};

TEST_F(JobTableTest, attach) {
    JobTable jobTable;

    auto job1 = newJob();
    auto job2 = newJob();
    auto job3 = newJob();
    auto job4 = newJob();
    auto job5 = newJob();
    auto job6 = newJob();

    jobTable.attach(job1);
    ASSERT_EQ(1u, job1->getJobID());
    ASSERT_EQ(job1, jobTable.getLatestEntry());

    jobTable.attach(job2);
    ASSERT_EQ(2u, job2->getJobID());
    ASSERT_EQ(job2, jobTable.getLatestEntry());

    jobTable.attach(job3);
    ASSERT_EQ(3u, job3->getJobID());
    ASSERT_EQ(job3, jobTable.getLatestEntry());

    jobTable.attach(job4);
    ASSERT_EQ(4u, job4->getJobID());
    ASSERT_EQ(job4, jobTable.getLatestEntry());

    jobTable.attach(job5);
    ASSERT_EQ(5u, job5->getJobID());
    ASSERT_EQ(job5, jobTable.getLatestEntry());

    ASSERT_EQ(job2, jobTable.detach(2, true));
    ASSERT_EQ(job5, jobTable.getLatestEntry());

    ASSERT_EQ(job3, jobTable.detach(3, false));
    ASSERT_EQ(job5, jobTable.getLatestEntry());

    ASSERT_EQ(job5, jobTable.detach(5, true));
    ASSERT_EQ(job4, jobTable.getLatestEntry());

    // job entry layout
    auto begin = getBeginIter(jobTable);
    ASSERT_EQ(1u, (*begin)->getJobID());
    ++begin;
    ASSERT_EQ(4u, (*begin)->getJobID());
    ++begin;
    ASSERT_EQ(getEndIter(jobTable), begin);


    // re-attach
    jobTable.attach(job5);
    ASSERT_EQ(2u, job5->getJobID());
    ASSERT_EQ(job5, jobTable.getLatestEntry());

    begin = getBeginIter(jobTable);
    ASSERT_EQ(1u, (*begin)->getJobID());
    ++begin;
    ASSERT_EQ(2u, (*begin)->getJobID());
    ++begin;
    ASSERT_EQ(4u, (*begin)->getJobID());
    ++begin;
    ASSERT_EQ(getEndIter(jobTable), begin);

    // re-attach
    jobTable.attach(job2);
    ASSERT_EQ(3u, job2->getJobID());
    ASSERT_EQ(job2, jobTable.getLatestEntry());

    begin = getBeginIter(jobTable);
    ASSERT_EQ(1u, (*begin)->getJobID());
    ++begin;
    ASSERT_EQ(2u, (*begin)->getJobID());
    ++begin;
    ASSERT_EQ(3u, (*begin)->getJobID());
    ++begin;
    ASSERT_EQ(4u, (*begin)->getJobID());
    ++begin;
    ASSERT_EQ(getEndIter(jobTable), begin);

    // re-attach
    jobTable.attach(job6);
    ASSERT_EQ(5u, job6->getJobID());
    ASSERT_EQ(job6, jobTable.getLatestEntry());

    begin = getBeginIter(jobTable);
    ASSERT_EQ(1u, (*begin)->getJobID());
    ++begin;
    ASSERT_EQ(2u, (*begin)->getJobID());
    ++begin;
    ASSERT_EQ(3u, (*begin)->getJobID());
    ++begin;
    ASSERT_EQ(4u, (*begin)->getJobID());
    ++begin;
    ASSERT_EQ(5u, (*begin)->getJobID());
    ++begin;
    ASSERT_EQ(getEndIter(jobTable), begin);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}