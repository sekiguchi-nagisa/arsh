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
    VMInspector() : breakOp(OpCode::NOP) {}

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
        this->state->hook = &this->inspector;
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
        this->setBreakPoint(OpCode::NOP);
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

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}