#include <gtest/gtest.h>

#include <vm.h>

#include "../test_common.hpp"

class VMBreakException {
private:
    const OpCode code;

public:
    VMBreakException(OpCode code) : code(code) {}

    OpCode getOpcode() const {
        return this->code;
    }
};

class VMInspector : public VMHook {
private:
    OpCode breakOp;

public:
    VMInspector() : breakOp(OpCode::HALT) {}

    void setBreakOp(OpCode breakOp) {
        this->breakOp = breakOp;
    }

    OpCode getBreakOp() const {
        return this->breakOp;
    }

    void vmFetchHook(DSState &, OpCode op) override {
        if(this->breakOp == op) {
            throw VMBreakException(op);
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

protected:
    void eval(const char *code) {
        try {
            DSState_eval(this->state, "(dummy)", code, nullptr);
        } catch(const VMBreakException &) {}
    }

    void setBreakPoint(OpCode op) {
        this->inspector.setBreakOp(op);
    }

    void resetBreakPoint() {
        this->setBreakPoint(OpCode::HALT);
    }

    DSValue getValue(const char *name) const {
        auto handle = this->state->symbolTable.lookupHandle(name);
        if(handle == nullptr || !handle->attr().has(FieldAttribute::FUNC_HANDLE)) {
            return nullptr;
        }
        return this->state->getGlobal(handle->getFieldIndex());
    }

    void RefCount(const char *gvarName, unsigned int refCount) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(gvarName != nullptr));

        auto *handle = this->state->symbolTable.lookupHandle(gvarName);
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(handle != nullptr));

        auto &v = this->state->getGlobal(handle->getFieldIndex());
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(v.isObject()));

        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(refCount, v.get()->getRefcount()));
    }
};

TEST_F(VMTest, base) {
    this->setBreakPoint(OpCode::POP);
    this->eval("12");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(12, typeAs<Int_Object>(this->state->peek())->getValue()));
}

TEST_F(VMTest, deinit1) {
    this->eval("var a = new [Int]()");
    ASSERT_(RefCount("a", 1));

    this->eval("{ var b = $a}");
    ASSERT_(RefCount("a", 1));

    this->setBreakPoint(OpCode::RAND);
    this->eval("{ var b = $a; if $true { var c = $a }; $RANDOM; }");
    ASSERT_(RefCount("a", 2));
}

TEST_F(VMTest, deinit2) {
    this->eval("{ var b = $@; throw 34; }");
    ASSERT_(RefCount("@", 1));

    this->eval("{ var a = $@; { var b = $@; var c = $b; throw 34; }}");
    ASSERT_(RefCount("@", 1));
}

TEST_F(VMTest, deinit3) {
    this->eval("var i = 0; while $i < 2 { var b = $@; $i++ }");
    ASSERT_(RefCount("@", 1));

    this->eval("while $true { var b = $@; break; }");
    ASSERT_(RefCount("@", 1));

    this->eval("for(var i = $@; $true;) { var b = $i; break; }");
    ASSERT_(RefCount("@", 1));

    this->eval("for(var i = 0; $i < 3; $i++) { var b = $@; continue; }");
    ASSERT_(RefCount("@", 1));
}

TEST_F(VMTest, deinit4) {
    this->setBreakPoint(OpCode::RAND);
    this->eval("function f($a : [String]) { $RANDOM; var b = $a; }; $f($@)");
    ASSERT_(RefCount("@", 2));
}

TEST_F(VMTest, deinit5) {
    this->setBreakPoint(OpCode::RAND);
    this->eval("function f($a : [String]) { var b = $a; { var c = $b } var c = $b; }; $f($@)");
    ASSERT_(RefCount("@", 1));
}

TEST_F(VMTest, deinit6) {
    this->setBreakPoint(OpCode::RAND);
    this->eval("function f($a : [String]) { var b = $a; { var c = $b } var c = $b; }; $f($@)");
    ASSERT_(RefCount("@", 1));
}

TEST_F(VMTest, deinit7) {
    this->eval("try { var a = $@; 34 / 0; var b = $a; } catch($e) {}");
    ASSERT_(RefCount("@", 1));

    this->setBreakPoint(OpCode::RAND);
    this->eval("try { while $true { var a = $@; break; } } finally {  $RANDOM; }");
    ASSERT_(RefCount("@", 1));
}

TEST_F(VMTest, deinit8) {
    this->setBreakPoint(OpCode::RAND);
    this->eval("try { var a = $@; 34 / 0 } catch $e { $RANDOM; }");
    ASSERT_(RefCount("@", 1));
}

TEST_F(VMTest, deinit9) {
    this->setBreakPoint(OpCode::RAND);
    this->eval("try { var a = $@; 34 / 0 } catch $e { var b = $@; throw 34; } finally {  $RANDOM; }");
    ASSERT_(RefCount("@", 1));
}

TEST_F(VMTest, deinit10) {
    this->setBreakPoint(OpCode::RAND);
    this->eval("try { var a = $@; var b = $a; 34 / 0 } catch $e : Int { var b = $@; var c = $b; var d = $c; } finally {  $RANDOM; }");
    ASSERT_(RefCount("@", 1));
}

TEST_F(VMTest, sig1) {
    this->eval("function f($s : Signal) {}");
//    auto *func = this->getFuncObject("f");
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