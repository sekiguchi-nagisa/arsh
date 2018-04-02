#include "gtest/gtest.h"

#include <functional>

#include <vm.h>

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
    virtual ~VMTest() = default;

    virtual void SetUp() {
        this->state = DSState_create();
        this->state->setVMHook(&this->inspector);
    }

    virtual void TearDown() {
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
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(kind, e.kind));
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

        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(refCount, v.get()->getRefcount()));
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


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}