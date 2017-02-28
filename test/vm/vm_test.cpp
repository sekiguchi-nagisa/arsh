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

    DSValue getGlobal(const char *varName) const {
        auto *handle = this->state->symbolTable.lookupHandle(varName);
        unsigned int index = handle->getFieldIndex();
        return state->getGlobal(index);
    }
};

TEST_F(VMTest, base) {
    this->setBreakPoint(OpCode::POP);
    this->eval("12");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(12, typeAs<Int_Object>(this->state->peek())->getValue()));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}