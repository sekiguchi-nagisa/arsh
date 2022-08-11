#include "gtest/gtest.h"

#include "../test_common.h"
#include <vm.h>

using BreakPointHandler = std::function<void()>;

class VMInspector : public VMHook {
private:
  OpCode breakOp;
  BreakPointHandler handler;
  bool called;

public:
  VMInspector() : breakOp(OpCode::HALT), handler(), called(true) {}

  void setHandler(OpCode op, BreakPointHandler &&hd) {
    this->breakOp = op;
    this->handler = std::move(hd);
    if (this->handler) {
      this->called = false;
    }
  }

  bool getCalled() const { return this->called; }

  void vmFetchHook(DSState &, OpCode op) override {
    if (this->breakOp == op) {
      if (this->handler) {
        this->handler();
        this->called = true;
      }
    }
  }

  void vmThrowHook(DSState &) override {}
};

class VMTest : public ExpectOutput {
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
    this->state->jobTable.send(SIGCONT);
    this->state->jobTable.send(SIGKILL);
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
    auto handle = this->state->rootModScope->lookup(name);
    if (!handle) {
      return nullptr;
    }
    return this->state->getGlobal(handle.asOk()->getIndex());
  }

  void RefCount(const char *gvarName, unsigned int refCount) {
    ASSERT_TRUE(gvarName != nullptr);

    auto handle = this->state->rootModScope->lookup(gvarName);
    ASSERT_TRUE(handle);

    auto &v = this->state->getGlobal(handle.asOk()->getIndex());
    ASSERT_TRUE(v.isObject());

    ASSERT_EQ(refCount, v.get()->getRefcount());
  }

  Output evalInChild(const char *code, OpCode breakOp, BreakPointHandler &&handler) {
    IOConfig config{IOConfig::INHERIT, IOConfig::PIPE, IOConfig::PIPE};
    auto handle = ProcBuilder::spawn(config, [&] {
      this->setBreakPointHandler(breakOp, std::move(handler));
      int r = DSState_eval(this->state, "<dummy>", code, strlen(code), nullptr);
      DSState_delete(&this->state);
      return r;
    });
    return handle.waitAndGetResult(true);
  }
};

TEST_F(VMTest, base) {
  ASSERT_NO_FATAL_FAILURE(this->eval("12", DS_ERROR_KIND_SUCCESS, OpCode::POP, [&] {
    ASSERT_EQ(12, this->state->getCallStack().peek().asInt());
  }));
}

TEST_F(VMTest, deinit1) {
  ASSERT_NO_FATAL_FAILURE(this->eval("var a = new [Int]()"));
  ASSERT_NO_FATAL_FAILURE(RefCount("a", 1));

  ASSERT_NO_FATAL_FAILURE(this->eval("{ var b = $a}"));
  ASSERT_NO_FATAL_FAILURE(RefCount("a", 1));

  ASSERT_NO_FATAL_FAILURE(this->eval("{ var b = $a; if $true { var c = $a }; $RANDOM; }",
                                     DS_ERROR_KIND_SUCCESS, OpCode::RAND,
                                     [&] { ASSERT_NO_FATAL_FAILURE(RefCount("a", 2)); }));
}

TEST_F(VMTest, deinit2) {
  ASSERT_NO_FATAL_FAILURE(this->eval("{ var b = $@; throw 34; }", DS_ERROR_KIND_RUNTIME_ERROR));
  ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));

  ASSERT_NO_FATAL_FAILURE(this->eval("{ var a = $@; { var b = $@; var c = $b; throw 34; }}",
                                     DS_ERROR_KIND_RUNTIME_ERROR));
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
                                     DS_ERROR_KIND_SUCCESS, OpCode::RAND,
                                     [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 2)); }));
}

TEST_F(VMTest, deinit5) {
  ASSERT_NO_FATAL_FAILURE(this->eval(
      "function f($a : [String]) { var b = $a; { var c = $b; $RANDOM; }; var c = $b; }; $f($@)",
      DS_ERROR_KIND_SUCCESS, OpCode::RAND, [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 4)); }));
}

TEST_F(VMTest, deinit6) {
  ASSERT_NO_FATAL_FAILURE(this->eval(
      "function f($a : [String]) { var b = $a; { var c = $b }; $RANDOM; var c = $b; }; $f($@)",
      DS_ERROR_KIND_SUCCESS, OpCode::RAND, [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 3)); }));
}

TEST_F(VMTest, deinit7) {
  ASSERT_NO_FATAL_FAILURE(this->eval("try { var a = $@; 34 / 0; var b = $a; } catch($e) {}"));
  ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));

  ASSERT_NO_FATAL_FAILURE(this->eval(
      "try { while $true { var a = $@; break; } } finally {  $RANDOM; }", DS_ERROR_KIND_SUCCESS,
      OpCode::RAND, [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, deinit8) {
  ASSERT_NO_FATAL_FAILURE(this->eval("try { var a = $@; 34 / 0 } catch $e { $RANDOM; }",
                                     DS_ERROR_KIND_SUCCESS, OpCode::RAND,
                                     [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, deinit9) {
  ASSERT_NO_FATAL_FAILURE(this->eval(
      "try { var a = $@; 34 / 0 } catch $e { var b = $@; throw 34; } finally {  $RANDOM; }",
      DS_ERROR_KIND_RUNTIME_ERROR, OpCode::RAND,
      [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, deinit10) {
  ASSERT_NO_FATAL_FAILURE(this->eval("try { var a = $@; var b = $a; 34 / 0 } catch $e : Int { var "
                                     "b = $@; var c = $b; var d = $c; } finally {  $RANDOM; }",
                                     DS_ERROR_KIND_RUNTIME_ERROR, OpCode::RAND,
                                     [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, stacktop) {
  const char *text = R"(
{
  var a = 34; var b = do { $@.slice(0,$true ? (break 90) : 23); } while($false);
  $RANDOM
}
)";

  ASSERT_NO_FATAL_FAILURE(this->eval(text, DS_ERROR_KIND_SUCCESS, OpCode::RAND,
                                     [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, sig1) {
  ASSERT_NO_FATAL_FAILURE(this->eval("function f($s : Signal) {}"));
  auto func = toObjPtr<FuncObject>(this->getValue("f"));
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
  auto func1 = toObjPtr<FuncObject>(this->getValue("SIG_DFL"));
  ASSERT_EQ(func, v.lookup(3));
  ASSERT_NE(func, func1);
  v.insertOrUpdate(3, func1);
  ASSERT_EQ(func1, v.lookup(3));
  ASSERT_EQ(3u, v.getData().size());

  // remove
  v.insertOrUpdate(4, nullptr);
  ASSERT_EQ(2u, v.getData().size());
  ASSERT_TRUE(v.lookup(4) == nullptr);

  // do nothing
  v.insertOrUpdate(5, nullptr);
  ASSERT_EQ(2u, v.getData().size());
  ASSERT_TRUE(v.lookup(5) == nullptr);
}

TEST_F(VMTest, error) {
  ASSERT_NO_FATAL_FAILURE(this->eval("var a = 45 / 0;", DS_ERROR_KIND_RUNTIME_ERROR));
  ASSERT_NO_FATAL_FAILURE(this->eval("$a;", DS_ERROR_KIND_RUNTIME_ERROR));
}

TEST_F(VMTest, abort) {
  ASSERT_NO_FATAL_FAILURE(this->eval("assert $false; var b = 34", DS_ERROR_KIND_ASSERTION_ERROR));
  ASSERT_NO_FATAL_FAILURE(this->eval("$b;", DS_ERROR_KIND_RUNTIME_ERROR));
}

TEST_F(VMTest, exit) {
  ASSERT_NO_FATAL_FAILURE(this->eval("false || exit; var c = 34", DS_ERROR_KIND_EXIT));
  ASSERT_NO_FATAL_FAILURE(this->eval("$c;", DS_ERROR_KIND_RUNTIME_ERROR));
}

TEST_F(VMTest, compCancel) {
  // interrupt file name completion
  const char *code = R"(
complete -A file ''
)";
  const char *err = R"([runtime error]
SystemError: code completion is cancelled, caused by `Interrupted system call'
    from <dummy>:2 '<toplevel>()')";
  auto output = this->evalInChild(code, OpCode::CALL_CMD, [&] {
    DSState_setOption(this->state, DS_OPTION_JOB_CONTROL);
    raise(SIGINT);
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(output, 1, WaitStatus::EXITED, "", err));

  // interrupt module name completion
  code = R"(
complete -A module ''
)";
  output = this->evalInChild(code, OpCode::CALL_CMD, [&] {
    DSState_setOption(this->state, DS_OPTION_JOB_CONTROL);
    raise(SIGINT);
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(output, 1, WaitStatus::EXITED, "", err));

  // interrupt command name completion
  code = R"(
complete -A cmd ''
)";
  output = this->evalInChild(code, OpCode::CALL_CMD, [&] {
    DSState_setOption(this->state, DS_OPTION_JOB_CONTROL);
    raise(SIGINT);
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(output, 1, WaitStatus::EXITED, "", err));

  // interrupt
  code = R"(
complete 'echo '
)";
  output = this->evalInChild(code, OpCode::CALL_CMD, [&] {
    DSState_setOption(this->state, DS_OPTION_JOB_CONTROL);
    raise(SIGINT);
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(output, 1, WaitStatus::EXITED, "", err));
}

TEST(ProcTableTest, base) {
  ProcTable table;
  auto *e = table.addProc(12, 1, 1);
  ASSERT_EQ(12, e->pid());
  ASSERT_EQ(1, e->jobId());
  ASSERT_EQ(1, e->procOffset());

  e = table.addProc(34, 1, 2);
  ASSERT_EQ(34, e->pid());
  ASSERT_EQ(1, e->jobId());
  ASSERT_EQ(2, e->procOffset());

  e = table.addProc(2, 1, 3);
  ASSERT_EQ(2, e->pid());
  ASSERT_EQ(1, e->jobId());
  ASSERT_EQ(3, e->procOffset());

  e = table.addProc(-1, 1, 3);
  ASSERT_EQ(nullptr, e);

  e = table.addProc(100, 0, 3);
  ASSERT_EQ(nullptr, e);

  ASSERT_EQ(3, table.getEntries().size());
  ASSERT_TRUE(table.deleteProc(12));
  ASSERT_TRUE(table.deleteProc(2));
  ASSERT_FALSE(table.deleteProc(-1));
  ASSERT_FALSE(table.deleteProc(1000));
  table.batchedRemove();
  ASSERT_EQ(1, table.getEntries().size());

  table.clear();
  ASSERT_EQ(0, table.getEntries().size());
}

static JobTable::ConstEntryIter getBeginIter(const JobTable &table) { return table.begin(); }

static JobTable::ConstEntryIter getEndIter(const JobTable &table) { return table.end(); }

struct JobTableTest : public VMTest {
  Job newJob() {
    return JobObject::create(Proc(), this->state->emptyFDObj, this->state->emptyFDObj,
                             DSValue::createStr());
  }

  template <typename Func>
  Job newJob(Func func) {
    auto proc = Proc::fork(*this->state, 0, Proc::Op::JOB_CONTROL);
    if (proc.pid() == 0) {
      int s = func();
      exit(s);
    }
    return JobObject::create(proc, this->state->emptyFDObj, this->state->emptyFDObj,
                             DSValue::createStr());
  }

  template <typename Func>
  Job newAttachedJob(Func func) {
    auto proc = Proc::fork(*this->state, 0, Proc::Op::JOB_CONTROL);
    if (proc.pid() == 0) {
      int s = func();
      exit(s);
    }
    auto job = JobObject::create(proc, this->state->emptyFDObj, this->state->emptyFDObj,
                                 DSValue::createStr());
    this->state->jobTable.attach(job);
    return job;
  }

  JobTable &jobTable() { return this->state->jobTable; }
};

TEST_F(JobTableTest, attach) {
  JobTable jobTable;

  auto job1 = newJob();
  auto job2 = newJob([] { return 12; });
  auto job3 = newJob();
  auto job4 = newJob();
  auto job5 = newJob([] {
    sleep(1);
    return 15;
  });
  auto job6 = newJob();

  jobTable.attach(job1);
  ASSERT_EQ(1u, job1->getJobID());
  ASSERT_EQ(job1, jobTable.getCurrentJob());

  jobTable.attach(job2);
  ASSERT_EQ(2u, job2->getJobID());
  ASSERT_EQ(job2, jobTable.getCurrentJob());

  jobTable.attach(job3);
  ASSERT_EQ(3u, job3->getJobID());
  ASSERT_EQ(job3, jobTable.getCurrentJob());

  jobTable.attach(job4);
  ASSERT_EQ(4u, job4->getJobID());
  ASSERT_EQ(job4, jobTable.getCurrentJob());

  jobTable.attach(job5);
  ASSERT_EQ(5u, job5->getJobID());
  ASSERT_EQ(job5, jobTable.getCurrentJob());
  ASSERT_EQ(5, jobTable.size());

  int s = jobTable.waitForJob(job2, WaitOp::BLOCK_UNTRACED);
  ASSERT_EQ(12, s);
  ASSERT_EQ(JobObject::State::TERMINATED, job2->state());
  ASSERT_EQ(0, job2->getJobID()); // after termination, jobId will be 0
  ASSERT_EQ(4, jobTable.size());
  ASSERT_EQ(job5, jobTable.getCurrentJob());

  job3->disown();
  ASSERT_EQ(job5, jobTable.getCurrentJob());

  s = jobTable.waitForJob(job5, WaitOp::BLOCK_UNTRACED);
  ASSERT_EQ(15, s);
  ASSERT_EQ(JobObject::State::TERMINATED, job5->state());
  ASSERT_EQ(0, job5->getJobID()); // after termination, jobId will be 0
  ASSERT_EQ(3, jobTable.size());
  ASSERT_EQ(job4, jobTable.getCurrentJob());

  // job entry layout
  auto begin = getBeginIter(jobTable);
  ASSERT_EQ(1u, (*begin)->getJobID());
  ++begin;
  ASSERT_EQ(3u, (*begin)->getJobID()); // disowned but job table still maintains
  ++begin;
  ASSERT_EQ(4u, (*begin)->getJobID());
  ++begin;
  ASSERT_EQ(getEndIter(jobTable), begin);

  // re-attach terminated job (do nothing)
  jobTable.attach(job5);
  ASSERT_EQ(0u, job5->getJobID());
  ASSERT_EQ(job4, jobTable.getCurrentJob());

  begin = getBeginIter(jobTable);
  ASSERT_EQ(1u, (*begin)->getJobID());
  ++begin;
  ASSERT_EQ(3u, (*begin)->getJobID());
  ++begin;
  ASSERT_EQ(4u, (*begin)->getJobID());
  ++begin;
  ASSERT_EQ(getEndIter(jobTable), begin);

  // attach
  jobTable.attach(job6);
  ASSERT_EQ(2u, job6->getJobID());
  ASSERT_EQ(job6, jobTable.getCurrentJob());

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
}

TEST_F(JobTableTest, waitJob) {
  ASSERT_EQ(0, this->jobTable().size());
  ASSERT_EQ(0, this->jobTable().getProcTable().viableProcSize());

  auto job1 = this->newAttachedJob([] { return 23; });
  ASSERT_TRUE(job1->available());
  int s = this->jobTable().waitForJob(job1, WaitOp::BLOCK_UNTRACED);
  ASSERT_EQ(23, s);
  ASSERT_FALSE(job1->available());
  ASSERT_EQ(0, this->jobTable().size());
  ASSERT_EQ(0, this->jobTable().getProcTable().viableProcSize());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}