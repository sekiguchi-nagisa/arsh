#include "gtest/gtest.h"

#include "../test_common.h"
#include <misc/split_random.hpp>
#include <ordered_map.h>
#include <vm.h>

using BreakPointHandler = std::function<void()>;

class VMInspector : public VMHook {
private:
  OpCode breakOp{OpCode::SUBSHELL_EXIT};
  BreakPointHandler handler;
  bool called{true};

public:
  void setHandler(OpCode op, BreakPointHandler &&hd) {
    this->breakOp = op;
    this->handler = std::move(hd);
    if (this->handler) {
      this->called = false;
    }
  }

  bool getCalled() const { return this->called; }

  void vmFetchHook(ARState &, OpCode op) override {
    if (this->breakOp == op) {
      if (this->handler) {
        this->handler();
        this->called = true;
      }
    }
  }

  void vmThrowHook(ARState &) override {}
};

class VMTest : public ExpectOutput {
protected:
  ARState *state{nullptr};
  VMInspector inspector{};

public:
  ~VMTest() override = default;

  void SetUp() override {
    this->state = ARState_create();
    this->state->setVMHook(&this->inspector);
  }

  void TearDown() override {
    this->state->jobTable.send(SIGCONT);
    this->state->jobTable.send(SIGKILL);
    ARState_delete(&this->state);
  }

private:
  void setBreakPointHandler(OpCode breakOp, BreakPointHandler &&handler) {
    this->inspector.setHandler(breakOp, std::move(handler));
  }

protected:
  void eval(const char *code, ARErrorKind kind = AR_ERROR_KIND_SUCCESS) {
    ARError e;
    ARState_eval(this->state, "(dummy)", code, strlen(code), &e);
    auto actualKind = e.kind;
    ARError_release(&e);
    ASSERT_EQ(kind, actualKind);
    ASSERT_TRUE(this->inspector.getCalled());
  }

  void eval(const char *code, ARErrorKind kind, OpCode breakOp, BreakPointHandler &&handler) {
    this->setBreakPointHandler(breakOp, std::move(handler));
    this->eval(code, kind);
  }

  Value getValue(const char *name) const {
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
      int r = ARState_eval(this->state, "<dummy>", code, strlen(code), nullptr);
      ARState_delete(&this->state);
      return r;
    });
    return handle.waitAndGetResult(true);
  }
};

TEST_F(VMTest, base) {
  ASSERT_NO_FATAL_FAILURE(this->eval("12", AR_ERROR_KIND_SUCCESS, OpCode::POP, [&] {
    ASSERT_EQ(12, this->state->getCallStack().peek().asInt());
  }));
}

TEST_F(VMTest, deinit1) {
  ASSERT_NO_FATAL_FAILURE(this->eval("var a = new [Int]()"));
  ASSERT_NO_FATAL_FAILURE(RefCount("a", 1));

  ASSERT_NO_FATAL_FAILURE(this->eval("{ var b = $a}"));
  ASSERT_NO_FATAL_FAILURE(RefCount("a", 1));

  ASSERT_NO_FATAL_FAILURE(this->eval("{ var b = $a; if $true { var c = $a }; $RANDOM; }",
                                     AR_ERROR_KIND_SUCCESS, OpCode::RAND,
                                     [&] { ASSERT_NO_FATAL_FAILURE(RefCount("a", 2)); }));
}

TEST_F(VMTest, deinit2) {
  ASSERT_NO_FATAL_FAILURE(
      this->eval("{ var b = $@; throw new Error(''); }", AR_ERROR_KIND_RUNTIME_ERROR));
  ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));

  ASSERT_NO_FATAL_FAILURE(
      this->eval("{ var a = $@; { var b = $@; var c = $b; throw new Error(''); }}",
                 AR_ERROR_KIND_RUNTIME_ERROR));
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
                                     AR_ERROR_KIND_SUCCESS, OpCode::RAND,
                                     [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 2)); }));
}

TEST_F(VMTest, deinit5) {
  ASSERT_NO_FATAL_FAILURE(this->eval(
      "function f($a : [String]) { var b = $a; { var c = $b; $RANDOM; }; var c = $b; }; $f($@)",
      AR_ERROR_KIND_SUCCESS, OpCode::RAND, [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 4)); }));
}

TEST_F(VMTest, deinit6) {
  ASSERT_NO_FATAL_FAILURE(this->eval(
      "function f($a : [String]) { var b = $a; { var c = $b }; $RANDOM; var c = $b; }; $f($@)",
      AR_ERROR_KIND_SUCCESS, OpCode::RAND, [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 3)); }));
}

TEST_F(VMTest, deinit7) {
  ASSERT_NO_FATAL_FAILURE(this->eval("try { var a = $@; 34 / 0; var b = $a; } catch($e) {}"));
  ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));

  ASSERT_NO_FATAL_FAILURE(this->eval(
      "try { while $true { var a = $@; break; } } finally {  $RANDOM; }", AR_ERROR_KIND_SUCCESS,
      OpCode::RAND, [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, deinit8) {
  ASSERT_NO_FATAL_FAILURE(this->eval("try { var a = $@; 34 / 0 } catch $e { $RANDOM; }",
                                     AR_ERROR_KIND_SUCCESS, OpCode::RAND,
                                     [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, deinit9) {
  ASSERT_NO_FATAL_FAILURE(this->eval("try { var a = $@; 34 / 0 } catch $e { var b = $@; throw new "
                                     "Error('34'); } finally {  $RANDOM; }",
                                     AR_ERROR_KIND_RUNTIME_ERROR, OpCode::RAND,
                                     [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, deinit10) {
  ASSERT_NO_FATAL_FAILURE(
      this->eval("try { var a = $@; var b = $a; 34 / 0 } catch $e : RegexMatchError { var "
                 "b = $@; var c = $b; var d = $c; } finally {  $RANDOM; }",
                 AR_ERROR_KIND_RUNTIME_ERROR, OpCode::RAND,
                 [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, deinit11) {
  const char *code = R"(
  try {
    var a = 34
    { var b = $@; $b.size()/0; }
  } catch e { $RANDOM; }
)";
  ASSERT_NO_FATAL_FAILURE(this->eval(code, AR_ERROR_KIND_SUCCESS, OpCode::RAND,
                                     [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, stacktop) {
  const char *text = R"(
{
  var a = 34; var b = do { $@.slice(0,$true ? (break 90) : 23); } while($false);
  $RANDOM
}
)";

  ASSERT_NO_FATAL_FAILURE(this->eval(text, AR_ERROR_KIND_SUCCESS, OpCode::RAND,
                                     [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, sig1) {
  ASSERT_NO_FATAL_FAILURE(this->eval("function f($s : Signal) {}"));
  auto func = ObjPtr<Object>(this->getValue("f").get());
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
  auto func1 = ObjPtr<Object>(this->getValue("SIG_DFL").get());
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
  ASSERT_NO_FATAL_FAILURE(this->eval("var a = 45 / 0;", AR_ERROR_KIND_RUNTIME_ERROR));
  ASSERT_NO_FATAL_FAILURE(this->eval("$a;", AR_ERROR_KIND_RUNTIME_ERROR));
}

TEST_F(VMTest, abort) {
  ASSERT_NO_FATAL_FAILURE(this->eval("assert $false; var b = 34", AR_ERROR_KIND_ASSERTION_ERROR));
  ASSERT_NO_FATAL_FAILURE(this->eval("$b;", AR_ERROR_KIND_RUNTIME_ERROR));
}

TEST_F(VMTest, exit) {
  ASSERT_NO_FATAL_FAILURE(this->eval("false || exit; var c = 34", AR_ERROR_KIND_EXIT));
  ASSERT_NO_FATAL_FAILURE(this->eval("$c;", AR_ERROR_KIND_RUNTIME_ERROR));
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
    ARState_setOption(this->state, AR_OPTION_JOB_CONTROL);
    raise(SIGINT);
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(output, 1, WaitStatus::EXITED, "", err));

  // interrupt module name completion
  code = R"(
complete -A module ''
)";
  output = this->evalInChild(code, OpCode::CALL_CMD, [&] {
    ARState_setOption(this->state, AR_OPTION_JOB_CONTROL);
    raise(SIGINT);
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(output, 1, WaitStatus::EXITED, "", err));

  // interrupt command name completion
  code = R"(
complete -A cmd ''
)";
  output = this->evalInChild(code, OpCode::CALL_CMD, [&] {
    ARState_setOption(this->state, AR_OPTION_JOB_CONTROL);
    raise(SIGINT);
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(output, 1, WaitStatus::EXITED, "", err));

  // interrupt
  code = R"(
complete 'echo '
)";
  output = this->evalInChild(code, OpCode::CALL_CMD, [&] {
    ARState_setOption(this->state, AR_OPTION_JOB_CONTROL);
    raise(SIGINT);
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(output, 1, WaitStatus::EXITED, "", err));
}

TEST_F(VMTest, callFuncAPI) {
  const char *src = R"(
  function sum(a: Int, b : Int): Int { return $a + $b; }
  function inc(a: (Int,)) {  $a._0++; }
)";
  ASSERT_NO_FATAL_FAILURE(this->eval(src));

  // function with return
  {
    auto *modType = this->state->typePool.getModTypeById(ROOT_MOD_ID);
    ASSERT_TRUE(modType);
    auto handle = modType->lookup(this->state->typePool, "sum");
    ASSERT_TRUE(handle);
    auto func = this->state->getGlobal(handle->getIndex());
    ASSERT_TRUE(func);
    auto ret = VM::callFunction(*this->state, std::move(func),
                                makeArgs(Value::createInt(99), Value::createInt(32)));
    ASSERT_TRUE(ret);
    ASSERT_EQ(99 + 32, ret.asInt());
  }

  // function no return
  {
    auto *modType = this->state->typePool.getModTypeById(ROOT_MOD_ID);
    ASSERT_TRUE(modType);
    auto handle = modType->lookup(this->state->typePool, "inc");
    ASSERT_TRUE(handle);
    auto func = this->state->getGlobal(handle->getIndex());
    ASSERT_TRUE(func);

    auto &funcType = this->state->typePool.get(handle->getTypeId());
    ASSERT_TRUE(isa<FunctionType>(funcType));
    auto &type = cast<FunctionType>(funcType).getParamTypeAt(0);
    ASSERT_TRUE(type.isTupleType());
    auto value = Value::create<BaseObject>(cast<TupleType>(type));
    auto &obj = typeAs<BaseObject>(value);
    obj[0] = Value::createInt(78);
    auto ret = VM::callFunction(*this->state, std::move(func), makeArgs(Value(value)));
    ASSERT_FALSE(ret); // void
    ASSERT_EQ(79, obj[0].asInt());
  }
}

TEST_F(VMTest, callMethodAPI) {
  const char *src = R"(
  typedef AAA {
    var begin : Int
    var end: Int
  }
  function dist(): Int for AAA { return $this.end - $this.begin; }
  function swap() for AAA {
    var tmp = $this.begin
    $this.begin = $this.end
    $this.end = $tmp
  }
)";
  ASSERT_NO_FATAL_FAILURE(this->eval(src));

  // native method
  {
    auto handle = this->state->typePool.lookupMethod(this->state->typePool.get(TYPE::Int), OP_ADD);
    ASSERT_TRUE(handle);
    auto ret =
        VM::callMethod(*this->state, *handle, Value::createInt(45), makeArgs(Value::createInt(67)));
    ASSERT_TRUE(ret);
    ASSERT_EQ(45 + 67, ret.asInt());
  }

  // user-defined constructor
  auto typeOrError = this->state->rootModScope->lookup(toTypeAliasFullName("AAA"));
  ASSERT_TRUE(typeOrError);
  auto &recordType = cast<RecordType>(this->state->typePool.get(typeOrError.asOk()->getTypeId()));
  Value instance;
  {
    auto *handle = this->state->rootModScope->lookupConstructor(this->state->typePool, recordType);
    ASSERT_TRUE(handle);
    ASSERT_TRUE(handle->isConstructor());
    auto ret = VM::callConstructor(*this->state, *handle,
                                   makeArgs(Value::createInt(2), Value::createInt(23)));
    ASSERT_TRUE(ret);
    ASSERT_TRUE(ret.isObject());
    ASSERT_TRUE(isa<BaseObject>(ret.get()));
    ASSERT_EQ(2, typeAs<BaseObject>(ret)[0].asInt());
    ASSERT_EQ(23, typeAs<BaseObject>(ret)[1].asInt());
    instance = std::move(ret);
  }

  // user-defined method with return
  {
    auto handleOrError =
        this->state->rootModScope->lookupMethod(this->state->typePool, recordType, "dist");
    ASSERT_TRUE(handleOrError);
    auto *handle = handleOrError.asOk();
    ASSERT_TRUE(!handle->isNative());
    auto ret = VM::callMethod(*this->state, *handle, Value(instance), makeArgs());
    ASSERT_TRUE(ret);
    ASSERT_EQ(21, ret.asInt());
  }

  // user-defined method no return
  {
    auto handleOrError =
        this->state->rootModScope->lookupMethod(this->state->typePool, recordType, "swap");
    ASSERT_TRUE(handleOrError);
    auto *handle = handleOrError.asOk();
    ASSERT_TRUE(!handle->isNative());
    auto ret = VM::callMethod(*this->state, *handle, Value(instance), makeArgs());
    ASSERT_FALSE(ret);
    ASSERT_EQ(23, typeAs<BaseObject>(instance)[0].asInt());
    ASSERT_EQ(2, typeAs<BaseObject>(instance)[1].asInt());
  }
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
                             Value::createStr());
  }

  template <typename Func>
  Job newJob(Func func) {
    auto proc = Proc::fork(*this->state, 0, Proc::Op::JOB_CONTROL);
    if (proc.pid() == 0) {
      int s = func();
      exit(s);
    }
    return JobObject::create(proc, this->state->emptyFDObj, this->state->emptyFDObj,
                             Value::createStr());
  }

  template <typename Func>
  Job newAttachedJob(Func func) {
    auto proc = Proc::fork(*this->state, 0, Proc::Op::JOB_CONTROL);
    if (proc.pid() == 0) {
      int s = func();
      exit(s);
    }
    auto job = JobObject::create(proc, this->state->emptyFDObj, this->state->emptyFDObj,
                                 Value::createStr());
    this->state->jobTable.attach(job);
    return job;
  }

  JobTable &jobTable() { return this->state->jobTable; }
};

TEST_F(JobTableTest, attach) {
  JobTable jobTable;
  {
    auto &e = jobTable.syncAndGetCurPrevJobs();
    ASSERT_FALSE(e.cur);
    ASSERT_FALSE(e.prev);
  }

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
  {
    auto &e = jobTable.syncAndGetCurPrevJobs();
    ASSERT_EQ(job1, e.cur);
    ASSERT_FALSE(e.prev);
  }

  jobTable.attach(job2);
  ASSERT_EQ(2u, job2->getJobID());
  {
    auto &e = jobTable.syncAndGetCurPrevJobs();
    ASSERT_EQ(job2, e.cur);
    ASSERT_EQ(job1, e.prev);
  }

  jobTable.attach(job3);
  ASSERT_EQ(3u, job3->getJobID());
  {
    auto &e = jobTable.syncAndGetCurPrevJobs();
    ASSERT_EQ(job3, e.cur);
    ASSERT_EQ(job2, e.prev);
  }

  jobTable.attach(job4);
  ASSERT_EQ(4u, job4->getJobID());
  {
    auto &e = jobTable.syncAndGetCurPrevJobs();
    ASSERT_EQ(job4, e.cur);
    ASSERT_EQ(job3, e.prev);
  }

  jobTable.attach(job5);
  ASSERT_EQ(5u, job5->getJobID());
  ASSERT_EQ(5, jobTable.size());
  {
    auto &e = jobTable.syncAndGetCurPrevJobs();
    ASSERT_EQ(job5, e.cur);
    ASSERT_EQ(job4, e.prev);
  }

  int s = jobTable.waitForJob(job2, WaitOp::BLOCK_UNTRACED);
  ASSERT_EQ(12, s);
  ASSERT_EQ(JobObject::State::TERMINATED, job2->state());
  ASSERT_EQ(0, job2->getJobID()); // after termination, jobId will be 0
  ASSERT_EQ(4, jobTable.size());
  {
    auto &e = jobTable.syncAndGetCurPrevJobs();
    ASSERT_EQ(job5, e.cur);
    ASSERT_EQ(job4, e.prev);
  }

  job3->disown();
  {
    auto &e = jobTable.syncAndGetCurPrevJobs();
    ASSERT_EQ(job5, e.cur);
    ASSERT_EQ(job4, e.prev);
  }

  s = jobTable.waitForJob(job5, WaitOp::BLOCK_UNTRACED);
  ASSERT_EQ(15, s);
  ASSERT_EQ(JobObject::State::TERMINATED, job5->state());
  ASSERT_EQ(0, job5->getJobID()); // after termination, jobId will be 0
  ASSERT_EQ(3, jobTable.size());
  {
    auto &e = jobTable.syncAndGetCurPrevJobs();
    ASSERT_EQ(job4, e.cur);
    ASSERT_EQ(job1, e.prev);
  }

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
  {
    auto &e = jobTable.syncAndGetCurPrevJobs();
    ASSERT_EQ(job4, e.cur);
    ASSERT_EQ(job1, e.prev);
  }

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
  {
    auto &e = jobTable.syncAndGetCurPrevJobs();
    ASSERT_EQ(job6, e.cur);
    ASSERT_EQ(job4, e.prev);
  }

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
  ASSERT_TRUE(job1->isAvailable());
  int s = this->jobTable().waitForJob(job1, WaitOp::BLOCK_UNTRACED);
  ASSERT_EQ(23, s);
  ASSERT_FALSE(job1->isAvailable());
  ASSERT_EQ(0, this->jobTable().size());
  ASSERT_EQ(0, this->jobTable().getProcTable().viableProcSize());
}

struct ObjectTest : ::testing::Test {
  struct Param {
    StringRef ref;
    bool smallStr;
    unsigned int meta;
  };

  static void checkMetaData(const Param &p) {
    auto v = Value::createStr(p.ref);

    ASSERT_EQ(p.ref, v.asStrRef());
    ASSERT_EQ(p.ref.size(), v.asStrRef().size());
    ASSERT_EQ(p.smallStr, isSmallStr(v.kind()));

    auto o = v.withMetaData(p.meta);
    ASSERT_TRUE(v.equals(o));
    ASSERT_EQ(p.ref, o.asStrRef());
    ASSERT_EQ(p.ref.size(), o.asStrRef().size());
    ASSERT_EQ(p.meta, o.getMetaData());
  }
};

static unsigned int next32(L64X128MixRNG &rng) {
  uint64_t v = rng.next();
  v &= UINT32_MAX;
  return static_cast<unsigned int>(v);
}

TEST_F(ObjectTest, meta) {
  L64X128MixRNG rng(42);
  ASSERT_NE(next32(rng), next32(rng));

  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456789abcdef", false, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456789abcde", false, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456789abcd", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456789abc", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456789ab", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456789a", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456789", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"012345678", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"01234567", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"012345", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"01234", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"012", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"01", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"", true, next32(rng)}));
}

TEST(MapTest, base) {
  TypePool pool;
  const auto &mapType = *pool.createMapType(pool.get(TYPE::String), pool.get(TYPE::Int)).take();

  auto value = Value::create<OrderedMapObject>(mapType, 42);
  auto obj = toObjPtr<OrderedMapObject>(value);

  ASSERT_EQ(0, obj->size());

  auto pair = obj->insert(Value::createStr("ABC"), Value::createInt(12));
  ASSERT_EQ(1, obj->size());
  ASSERT_EQ(1, obj->getEntries().getUsedSize());
  int retIndex = obj->lookup(Value::createStr("ABCD"));
  ASSERT_EQ(-1, retIndex);
  retIndex = obj->lookup(Value::createStr("ABC"));
  ASSERT_EQ(0, retIndex);
  ASSERT_EQ(retIndex, pair.first);
  ASSERT_TRUE(pair.second);
  ASSERT_EQ("ABC", (*obj)[retIndex].getKey().asStrRef());
  ASSERT_EQ(12, (*obj)[retIndex].getValue().asInt());

  // insert already defined key
  pair = obj->insert(Value::createStr("ABC"), Value::createInt(1232));
  ASSERT_EQ(1, obj->size());
  ASSERT_EQ(1, obj->getEntries().getUsedSize());
  ASSERT_EQ(0, pair.first);
  ASSERT_FALSE(pair.second);

  pair = obj->insert(Value::createStr("1234"), Value::createInt(-99));
  ASSERT_EQ(2, obj->size());
  ASSERT_EQ(2, obj->getEntries().getUsedSize());
  ASSERT_EQ(1, pair.first);
  ASSERT_TRUE(pair.second);
  retIndex = obj->lookup(Value::createStr("1234"));
  ASSERT_EQ(1, retIndex);
  ASSERT_EQ("1234", (*obj)[retIndex].getKey().asStrRef());
  ASSERT_EQ(-99, (*obj)[retIndex].getValue().asInt());

  pair = obj->insert(Value::createStr("***"), Value::createInt(9876));
  ASSERT_EQ(3, obj->size());
  ASSERT_EQ(3, obj->getEntries().getUsedSize());
  ASSERT_EQ(2, pair.first);
  ASSERT_TRUE(pair.second);
  retIndex = obj->lookup(Value::createStr("***"));
  ASSERT_EQ(2, retIndex);
  ASSERT_EQ("***", (*obj)[retIndex].getKey().asStrRef());
  ASSERT_EQ(9876, (*obj)[retIndex].getValue().asInt());
  ASSERT_EQ("1234", (*obj)[1].getKey().asStrRef());
  ASSERT_EQ(-99, (*obj)[1].getValue().asInt());
  ASSERT_EQ("ABC", (*obj)[0].getKey().asStrRef());
  ASSERT_EQ(12, (*obj)[0].getValue().asInt());
}

static std::string location(unsigned int index,
                            const std::vector<std::pair<std::string, uint64_t>> &values) {
  auto &keyValue = values[index];

  std::string message = "at ";
  message += std::to_string(index);
  message += " (";
  message += keyValue.first;
  message += ", ";
  message += std::to_string(keyValue.second);
  message += ")";
  return message;
}

TEST(MapTest, rand1) {
  TypePool pool;
  const auto &mapType = *pool.createMapType(pool.get(TYPE::String), pool.get(TYPE::Int)).take();

  auto value = Value::create<OrderedMapObject>(mapType, 42);
  auto obj = toObjPtr<OrderedMapObject>(value);

  ASSERT_EQ(0, obj->size());

  constexpr unsigned int N = 2000;
  L64X128MixRNG rng(42);
  std::vector<std::pair<std::string, uint64_t>> keyValues;
  keyValues.reserve(N);
  for (unsigned int i = 0; i < N; i++) {
    static_assert(sizeof(uint64_t) == sizeof(uintmax_t));

    uint64_t v = rng.next();
    char data[64];
    int size = snprintf(data, std::size(data), "%#jx", static_cast<uintmax_t>(v));
    keyValues.emplace_back(std::string(data, size), v);
  }

  // insert
  ASSERT_FALSE(keyValues.empty());
  for (unsigned int i = 0; i < keyValues.size(); i++) {
    SCOPED_TRACE(location(i, keyValues));

    const auto &keyValue = keyValues[i];
    auto pair = obj->insert(Value::createStr(keyValue.first),
                            Value::createInt(static_cast<int64_t>(keyValue.second)));
    ASSERT_TRUE(pair.second);
    ASSERT_EQ(i, pair.first);
    ASSERT_EQ(keyValue.first, (*obj)[pair.first].getKey().asStrRef());
    ASSERT_EQ(keyValue.second, (*obj)[pair.first].getValue().asInt());
    ASSERT_EQ(i + 1, obj->size());
    ASSERT_EQ(i + 1, obj->getEntries().getUsedSize());
  }

  // lookup
  ASSERT_FALSE(keyValues.empty());
  for (unsigned int i = 0; i < keyValues.size(); i++) {
    SCOPED_TRACE(location(i, keyValues));

    const auto &keyValue = keyValues[i];
    auto retIndex = obj->lookup(Value::createStr(keyValue.first));
    ASSERT_EQ(i, retIndex);
    ASSERT_EQ(keyValue.first, (*obj)[retIndex].getKey().asStrRef());
    ASSERT_EQ(keyValue.second, (*obj)[retIndex].getValue().asInt());
  }

  // already inserted
  ASSERT_FALSE(keyValues.empty());
  for (unsigned int i = 0; i < keyValues.size(); i++) {
    SCOPED_TRACE(location(i, keyValues));

    const auto &keyValue = keyValues[i];
    auto pair = obj->insert(Value::createStr(keyValue.first),
                            Value::createInt(static_cast<int64_t>(keyValue.second + 9999)));
    ASSERT_FALSE(pair.second);
    ASSERT_EQ(i, pair.first);
    ASSERT_EQ(keyValue.first, (*obj)[pair.first].getKey().asStrRef());
    ASSERT_EQ(keyValue.second, (*obj)[pair.first].getValue().asInt());
    ASSERT_EQ(keyValues.size(), obj->size());
    ASSERT_EQ(keyValues.size(), obj->getEntries().getUsedSize());
  }

  // lookup not found key
  for (unsigned int i = 0; i < 150; i++) {
    uint64_t v = rng.next();
    char data[64];
    int size = snprintf(data, std::size(data), "%#jx", static_cast<uintmax_t>(v));
    std::string key(data, size);

    SCOPED_TRACE("at " + std::to_string(i) + " " + key);

    auto retIndex = obj->lookup(Value::createStr(key));
    ASSERT_EQ(-1, retIndex);
  }

  // check insertion order
  std::vector<std::pair<StringRef, uint64_t>> entries;
  for (auto &e : obj->getEntries()) {
    if (!e) {
      continue;
    }
    entries.emplace_back(e.getKey().asStrRef(), static_cast<uint64_t>(e.getValue().asInt()));
  }
  ASSERT_EQ(entries.size(), keyValues.size());
  for (unsigned int i = 0; i < keyValues.size(); i++) {
    SCOPED_TRACE(location(i, keyValues));

    ASSERT_EQ(keyValues[i].first, entries[i].first);
    ASSERT_EQ(keyValues[i].first, entries[i].first);
  }

  // remove
  std::unordered_set<std::string> removeTargets;
  for (unsigned int i = 0; i < 777; i++) {
    auto index = static_cast<unsigned int>(rng.next() % N);
    removeTargets.insert(keyValues[index].first);
  }

  for (auto iter = keyValues.begin(); iter != keyValues.end();) {
    const auto &e = *iter;
    if (removeTargets.find(e.first) != removeTargets.end()) {
      SCOPED_TRACE("(" + e.first + ", " + std::to_string(e.second) + ")");

      auto entry = obj->remove(Value::createStr(e.first));
      ASSERT_TRUE(entry);
      ASSERT_EQ(e.first, entry.getKey().asStrRef());
      ASSERT_EQ(e.second, entry.getValue().asInt());
      iter = keyValues.erase(iter);
    } else {
      ++iter;
    }
  }
  ASSERT_EQ(keyValues.size(), obj->size());

  // lookup removed entry
  for (auto &e : removeTargets) {
    SCOPED_TRACE(e);

    auto retIndex = obj->lookup(Value::createStr(e));
    ASSERT_EQ(-1, retIndex);
  }
  // lookup remain entry
  for (auto &e : keyValues) {
    SCOPED_TRACE("(" + e.first + ", " + std::to_string(e.second) + ")");

    auto retIndex = obj->lookup(Value::createStr(e.first));
    ASSERT_NE(-1, retIndex);
    ASSERT_EQ(e.first, (*obj)[retIndex].getKey().asStrRef());
    ASSERT_EQ(e.second, (*obj)[retIndex].getValue().asInt());
  }

  // check insertion order after remove
  entries.clear();
  for (auto &e : obj->getEntries()) {
    if (!e) {
      continue;
    }
    entries.emplace_back(e.getKey().asStrRef(), static_cast<uint64_t>(e.getValue().asInt()));
  }
  ASSERT_EQ(entries.size(), keyValues.size());
  for (unsigned int i = 0; i < keyValues.size(); i++) {
    SCOPED_TRACE(location(i, keyValues));

    ASSERT_EQ(keyValues[i].first, entries[i].first);
    ASSERT_EQ(keyValues[i].first, entries[i].first);
  }

  // insert after remove
  for (unsigned int i = 0; i < 1000; i++) {
    uint64_t v = rng.next();
    char data[64];
    int size = snprintf(data, std::size(data), "%#jX", static_cast<uintmax_t>(v));
    std::string key(data, size);
    keyValues.emplace_back(key, v);

    SCOPED_TRACE("(" + key + ", " + std::to_string(v) + ")");

    auto pair = obj->insert(Value::createStr(key), Value::createInt(static_cast<int64_t>(v)));
    ASSERT_TRUE(pair.second);
    ASSERT_NE(-1, pair.first);

    ASSERT_EQ(key, (*obj)[pair.first].getKey().asStrRef());
    ASSERT_EQ(v, (*obj)[pair.first].getValue().asInt());
  }

  // check insertion order after remove and insertion
  entries.clear();
  for (auto &e : obj->getEntries()) {
    if (!e) {
      continue;
    }
    entries.emplace_back(e.getKey().asStrRef(), static_cast<uint64_t>(e.getValue().asInt()));
  }
  ASSERT_EQ(entries.size(), keyValues.size());
  for (unsigned int i = 0; i < keyValues.size(); i++) {
    SCOPED_TRACE(location(i, keyValues));

    ASSERT_EQ(keyValues[i].first, entries[i].first);
    ASSERT_EQ(keyValues[i].first, entries[i].first);
  }
}

TEST(MapTest, rand2) {
  TypePool pool;
  const auto &mapType = *pool.createMapType(pool.get(TYPE::String), pool.get(TYPE::Int)).take();

  auto value = Value::create<OrderedMapObject>(mapType, 42);
  auto obj = toObjPtr<OrderedMapObject>(value); // FIXME:
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}