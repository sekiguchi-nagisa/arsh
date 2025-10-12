#include "gtest/gtest.h"

#include "../test_common.h"
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
                                     AR_ERROR_KIND_SUCCESS, OpCode::LOAD_SPECIAL,
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
                                     AR_ERROR_KIND_SUCCESS, OpCode::LOAD_SPECIAL,
                                     [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 2)); }));
}

TEST_F(VMTest, deinit5) {
  ASSERT_NO_FATAL_FAILURE(this->eval(
      "function f($a : [String]) { var b = $a; { var c = $b; $RANDOM; }; var c = $b; }; $f($@)",
      AR_ERROR_KIND_SUCCESS, OpCode::LOAD_SPECIAL,
      [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 4)); }));
}

TEST_F(VMTest, deinit6) {
  ASSERT_NO_FATAL_FAILURE(this->eval(
      "function f($a : [String]) { var b = $a; { var c = $b }; $RANDOM; var c = $b; }; $f($@)",
      AR_ERROR_KIND_SUCCESS, OpCode::LOAD_SPECIAL,
      [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 3)); }));
}

TEST_F(VMTest, deinit7) {
  ASSERT_NO_FATAL_FAILURE(this->eval("try { var a = $@; 34 / 0; var b = $a; } catch($e) {}"));
  ASSERT_NO_FATAL_FAILURE(RefCount("@", 1));

  ASSERT_NO_FATAL_FAILURE(this->eval(
      "try { while $true { var a = $@; break; } } finally {  $RANDOM; }", AR_ERROR_KIND_SUCCESS,
      OpCode::LOAD_SPECIAL, [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, deinit8) {
  ASSERT_NO_FATAL_FAILURE(this->eval("try { var a = $@; 34 / 0 } catch $e { $RANDOM; }",
                                     AR_ERROR_KIND_SUCCESS, OpCode::LOAD_SPECIAL,
                                     [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, deinit9) {
  ASSERT_NO_FATAL_FAILURE(this->eval("try { var a = $@; 34 / 0 } catch $e { var b = $@; throw new "
                                     "Error('34'); } finally {  $RANDOM; }",
                                     AR_ERROR_KIND_RUNTIME_ERROR, OpCode::LOAD_SPECIAL,
                                     [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, deinit10) {
  ASSERT_NO_FATAL_FAILURE(
      this->eval("try { var a = $@; var b = $a; 34 / 0 } catch $e : RegexMatchError { var "
                 "b = $@; var c = $b; var d = $c; } finally {  $RANDOM; }",
                 AR_ERROR_KIND_RUNTIME_ERROR, OpCode::LOAD_SPECIAL,
                 [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, deinit11) {
  const char *code = R"(
  try {
    var a = 34
    { var b = $@; $b.size()/0; }
  } catch e { $RANDOM; }
)";
  ASSERT_NO_FATAL_FAILURE(this->eval(code, AR_ERROR_KIND_SUCCESS, OpCode::LOAD_SPECIAL,
                                     [&] { ASSERT_NO_FATAL_FAILURE(RefCount("@", 1)); }));
}

TEST_F(VMTest, stacktop) {
  const char *text = R"(
{
  var a = 34; var b = do { $@.slice(0,$true ? (break 90) : 23); } while($false);
  $RANDOM
}
)";

  ASSERT_NO_FATAL_FAILURE(this->eval(text, AR_ERROR_KIND_SUCCESS, OpCode::LOAD_SPECIAL,
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
  function inc(a: [Int]) {  $a[0]++; }
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
    ASSERT_TRUE(type.isArrayType());
    auto value = Value::create<ArrayObject>(cast<ArrayType>(type));
    auto &obj = typeAs<ArrayObject>(value);
    obj.append(Value::createInt(78));
    ASSERT_EQ(78, obj[0].asInt());
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
  Job newJob() { return JobObject::fromProc(Proc(), Value::createStr()); }

  template <typename Func>
  Job newJob(Func func) {
    auto proc = Proc::fork(*this->state, {.pgid = 0, .jobControl = true, .foreground = false});
    if (proc.pid() == 0) {
      int s = func();
      exit(s);
    }
    return JobObject::fromProc(proc, Value::createStr());
  }

  template <typename Func>
  Job newAttachedJob(Func func) {
    auto proc = Proc::fork(*this->state, {.pgid = 0, .jobControl = true, .foreground = false});
    if (proc.pid() == 0) {
      int s = func();
      exit(s);
    }
    auto job = JobObject::fromProc(proc, Value::createStr());
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

  {
    auto j = jobTable.lookup("%%");
    ASSERT_TRUE(j.isError());
    ASSERT_EQ(JobLookupResult::ErrorType::NO_JOB, j.asError().type);
    ASSERT_EQ(0, j.asError().value);

    j = jobTable.lookup("%-");
    ASSERT_TRUE(j.isError());
    ASSERT_EQ(JobLookupResult::ErrorType::NO_JOB, j.asError().type);
    ASSERT_EQ(0, j.asError().value);
  }

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

  // job lookup api
  {
    auto j = jobTable.lookup("");
    ASSERT_TRUE(j.isError());
    ASSERT_EQ(JobLookupResult::ErrorType::INVALID, j.asError().type);
    ASSERT_EQ(0, j.asError().value);

    j = jobTable.lookup("%");
    ASSERT_TRUE(j.isError());
    ASSERT_EQ(JobLookupResult::ErrorType::INVALID, j.asError().type);
    ASSERT_EQ(0, j.asError().value);

    j = jobTable.lookup("fjriae");
    ASSERT_TRUE(j.isError());
    ASSERT_EQ(JobLookupResult::ErrorType::INVALID, j.asError().type);
    ASSERT_EQ(0, j.asError().value);

    j = jobTable.lookup("%1000");
    ASSERT_TRUE(j.isError());
    ASSERT_EQ(JobLookupResult::ErrorType::NO_JOB, j.asError().type);
    ASSERT_EQ(1000, j.asError().value);

    j = jobTable.lookup("%%");
    ASSERT_TRUE(j.isJob());
    ASSERT_EQ(3, j.asJob()->getJobID());

    j = jobTable.lookup("%+");
    ASSERT_TRUE(j.isJob());
    ASSERT_EQ(3, j.asJob()->getJobID());

    j = jobTable.lookup("%-");
    ASSERT_TRUE(j.isJob());
    ASSERT_EQ(2, j.asJob()->getJobID());

    j = jobTable.lookup("%2");
    ASSERT_TRUE(j.isJob());
    ASSERT_EQ(2, j.asJob()->getJobID());
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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}