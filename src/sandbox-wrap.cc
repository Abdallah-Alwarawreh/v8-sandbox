#include "sandbox-wrap.h"
#include "common.h"

#include <iostream>
#include <memory>
#include <v8.h>

#include <unistd.h>
#include <iostream>

void Debug(const char *msg) {
  std::cout << getpid() << " : " << msg << std::endl;
}

// #include "sandbox.h"

using namespace v8;

Nan::Persistent<v8::Function> SandboxWrap::constructor;

SandboxWrap::SandboxWrap()
  : callback_(nullptr),
    result_(""),
    dispatchResult_(""),
    buffers_(),
    bytesRead_(-1),
    bytesExpected_(-1),
    message_("")
{
}

SandboxWrap::~SandboxWrap() {
}

void SandboxWrap::Init(v8::Local<v8::Object> exports) {
  Nan::HandleScope scope;

  v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("Sandbox").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  Nan::SetPrototypeMethod(tpl, "execute", Execute);
  Nan::SetPrototypeMethod(tpl, "callback", Callback);

  auto function = Nan::GetFunction(tpl).ToLocalChecked();

  constructor.Reset(function);

  Nan::Set(exports, Nan::New("Sandbox").ToLocalChecked(), function);
}

NAN_METHOD(SandboxWrap::New) {
  if (info.IsConstructCall()) {
    SandboxWrap *obj = new SandboxWrap();
    obj->Wrap(info.This());

    NODE_ARG_STRING(0, "socket");

    Nan::Utf8String socket(info[0]);

    obj->socket_ = *socket;

    info.GetReturnValue().Set(info.This());
  } else {
    const int argc = 1;
    v8::Local<v8::Value> argv[argc] = { info[0] };
    v8::Local<v8::Function> cons = Nan::New<v8::Function>(constructor);
    info.GetReturnValue().Set(Nan::NewInstance(cons, argc, argv).ToLocalChecked());
  }
}

NAN_METHOD(SandboxWrap::Execute) {
  NODE_ARG_STRING(0, "code");
  NODE_ARG_FUNCTION(1, "callback");

  Nan::Utf8String code(info[0]);

  SandboxWrap* sandbox = ObjectWrap::Unwrap<SandboxWrap>(info.Holder());

  // Nan::Callback *callback = new Nan::Callback(info[1].As<v8::Function>());

  sandbox->bridge_.Reset(info[2].As<v8::Function>());
  sandbox->Execute(*code);

  info.GetReturnValue().Set(info.This());
}

void SandboxWrap::Execute(const char *code) {
  // isolate_ = Isolate::GetCurrent();
  nodeContext_ = Nan::GetCurrentContext();

  Debug("Execute");

  nodeContext_.Reset(Nan::New(nodeContext_));

  sandboxContext_.Reset(Context::New(Isolate::GetCurrent()));
  
  auto context = Nan::New(sandboxContext_);
  auto global = context->Global();

  Context::Scope context_scope(context);

  sandboxGlobal_.Reset(global);

  Nan::SetPrivate(global, Nan::New("sandbox").ToLocalChecked(), External::New(Isolate::GetCurrent(), this));

  Nan::Set(global, Nan::New("global").ToLocalChecked(), context->Global());
  Nan::SetMethod(global, "_setResult", SetResult);
  Nan::SetMethod(global, "_dispatchSync", DispatchSync);
  Nan::SetMethod(global, "_dispatchAsync", DispatchAsync);
  Nan::SetMethod(global, "_debug", DebugLog);
  // Nan::SetMethod(global, "_dispatchAsync", DispatchAsync);

  result_= "";

  Nan::TryCatch tryCatch;

  Debug("Execute2");

  MaybeLocal<Script> script = Script::Compile(context, Nan::New(code).ToLocalChecked());

  if (!tryCatch.HasCaught()) {
    Debug("Executeing!!!");
    (void)script.ToLocalChecked()->Run(context);
  }

  if (tryCatch.HasCaught()) {
    Debug("HasCaught");
  }

  MaybeHandleError(tryCatch, context);

  Debug("Execute3");
  // return result_;
}

void SandboxWrap::Callback(int id, const char *args) {
  auto baton = pendingOperations_[id];

  auto context = Nan::New(sandboxContext_);
  auto global = context->Global();

  Context::Scope context_scope(context);

  // Nan::Set(global, Nan::New("_message").ToLocalChecked(), Nan::New(message).ToLocalChecked());

  Nan::TryCatch tryCatch;

  Local<Function> callback = Nan::New(baton->callback->As<Function>());

  // auto callback = Nan::Get(global, Nan::New("_callback").ToLocalChecked()).ToLocalChecked(); 

  Debug("ARGSSS");
  Debug(args);

  if (callback->IsFunction()) {
    Debug("CALLBACKCALLING!!!!");

    v8::Local<v8::Value> argv[] = {
      Nan::New(args).ToLocalChecked()
    };

    Nan::Call(callback, global, 1, argv);
  }

  if (tryCatch.HasCaught()) {
    Debug("HasCaught");
  }

  MaybeHandleError(tryCatch, context);

  pendingOperations_.erase(id);

  Debug("Callback::Execute3");
  // return result_;
}

NAN_METHOD(SandboxWrap::SetResult) {
  NODE_ARG_STRING(0, "result");

  Nan::Utf8String value(info[0]);

  SandboxWrap* sandbox = GetSandboxFromContext();

  sandbox->result_ = *value;
}


NAN_METHOD(SandboxWrap::Callback) {
  NODE_ARG_INTEGER(0, "id");
  NODE_ARG_STRING(1, "args");

  Nan::Utf8String args(info[1]);

  SandboxWrap* sandbox = ObjectWrap::Unwrap<SandboxWrap>(info.Holder());

  sandbox->Callback(Nan::To<uint32_t>(info[0]).FromJust(), *args);
}

NAN_METHOD(SandboxWrap::DispatchSync) {
  NODE_ARG_INTEGER(0, "id");
  NODE_ARG_STRING(1, "parameters");

  Nan::Utf8String arguments(info[1]);

  SandboxWrap* sandbox = GetSandboxFromContext();

  std::string result = sandbox->DispatchSync(*arguments);

  info.GetReturnValue().Set(Nan::New(result.c_str()).ToLocalChecked());
}

NAN_METHOD(SandboxWrap::DispatchAsync) {
  NODE_ARG_INTEGER(0, "id");
  NODE_ARG_STRING(1, "parameters");
  NODE_ARG_FUNCTION(2, "callback");

  int id = Nan::To<int>(info[0]).FromJust();
  Nan::Utf8String arguments(info[1]);

  SandboxWrap* sandbox = GetSandboxFromContext();

  Debug("DispatchAsyncING BEFORE");
  std::string result = sandbox->DispatchAsync(id, *arguments, info[2].As<Function>());
  Debug("DispatchAsyncING AFTER");
  // info.GetReturnValue().Set(Nan::New(result.c_str()).ToLocalChecked());
}

NAN_METHOD(SandboxWrap::DebugLog) {
  NODE_ARG_STRING(0, "message");

  Nan::Utf8String message(info[0]);

  Debug(*message);

  // info.GetReturnValue().Set(Nan::New(result.c_str()).ToLocalChecked());
}

void SandboxWrap::MaybeHandleError(Nan::TryCatch &tryCatch, Local<Context> &context) {
  if (!tryCatch.HasCaught()) {
    return;
  }

  auto result = Nan::New<Object>();
  auto error = Nan::New<Object>();

  Nan::Utf8String message(tryCatch.Message()->Get());
  Nan::Utf8String stack(tryCatch.StackTrace().ToLocalChecked());
  int lineNumber = tryCatch.Message()->GetLineNumber(context).FromJust();

  Nan::Set(result, Nan::New("error").ToLocalChecked(), error);

  Nan::Set(error, Nan::New("message").ToLocalChecked(), Nan::New(*message).ToLocalChecked());
  Nan::Set(error, Nan::New("stack").ToLocalChecked(), Nan::New(*stack).ToLocalChecked());
  Nan::Set(error, Nan::New("lineNumber").ToLocalChecked(), Nan::New(lineNumber));

  auto json = JSON::Stringify(context, result).ToLocalChecked();

  result_ = *Nan::Utf8String(json);

  Debug(result_.c_str());
}

SandboxWrap *SandboxWrap::GetSandboxFromContext() {
  auto context = Isolate::GetCurrent()->GetCurrentContext();

  auto hidden = Nan::GetPrivate(context->Global(), Nan::New("sandbox").ToLocalChecked()).ToLocalChecked();

  Local<External> field = Local<External>::Cast(hidden);

  SandboxWrap *sandbox = (SandboxWrap *)field->Value();

  return sandbox;
}

std::string SandboxWrap::DispatchSync(const char *arguments) {
  uv_pipe_t *pipe = nullptr;
  uv_loop_t *loop = nullptr;

  Debug("DispatchSync");
  Debug(arguments);

  bytesRead_ = -1;
  bytesExpected_ = -1;
  buffers_.clear();
  message_ = arguments;
  dispatchResult_ = "";
  
  loop = (uv_loop_t *)malloc(sizeof(uv_loop_t));

  uv_loop_init(loop);

  uv_connect_t *request = (uv_connect_t *)malloc(sizeof(uv_connect_t));

  pipe = (uv_pipe_t *)malloc(sizeof(uv_pipe_t));

  uv_pipe_init(loop, pipe, 0);

  pipe->data = (void *)this;

  uv_pipe_connect(request, pipe, socket_.c_str(), OnConnected);

  uv_run(loop, UV_RUN_DEFAULT);

  uv_loop_close(loop);

  // free(request);
  // free(pipe);
  free(loop);

  return dispatchResult_;
}

// make a single interface with (int id, const char *arguments, Local<Function> callback)
// always dispatch batons the same way, no different with sync or async, just the presence of the callback
std::string SandboxWrap::DispatchAsync(int id, const char *arguments, Local<Function> callback) {
  auto cb = std::make_shared<Nan::Persistent<Function>>(callback);

  auto baton = std::make_shared<AsyncSandboxOperationBaton>(id, this, cb);

  // LockPendingOperations();

  pendingOperations_[baton->id] = baton;

  // UnlockPendingOperations();

  baton->arguments = arguments;

  return DispatchSync(arguments);
  // auto context = Nan::New(nodeContext_);
  // auto global = context->Global();

  // Context::Scope context_scope(context);

  // Local<Function> cb = Nan::New(bridge_.As<Function>());

  // v8::Local<v8::Value> argv[] = {
  //   Nan::New(arguments).ToLocalChecked()
  // };

  // Nan::Call(cb, global, 1, argv);

  // return "";
}

// std::string SandboxWrap::DispatchAsyncOld(const char *arguments) {
//   auto context = Nan::New(nodeContext_);
//   auto global = context->Global();

//   Context::Scope context_scope(context);

//   Local<Function> cb = Nan::New(bridge_.As<Function>());

//   v8::Local<v8::Value> argv[] = {
//     Nan::New(arguments).ToLocalChecked()
//   };

//   Nan::Call(cb, global, 1, argv);

//   return "";
// }

void SandboxWrap::AllocateBuffer(uv_handle_t *handle, size_t size, uv_buf_t *buffer) {
  buffer->base = (char *)malloc(size);
  buffer->len = size;
}

void SandboxWrap::OnConnected(uv_connect_t *request, int status) {
  assert(status == 0);

  SandboxWrap *sandbox = (SandboxWrap *)request->handle->data;

  uv_read_start((uv_stream_t *)request->handle, AllocateBuffer, OnRead);

  WriteData((uv_stream_t *)request->handle, sandbox->message_);
}

void SandboxWrap::OnRead(uv_stream_t *pipe, ssize_t bytesRead, const uv_buf_t *buffer) {
  SandboxWrap *sandbox = (SandboxWrap *)pipe->data;

  if (bytesRead > 0) {
    char *chunk = (char *)buffer->base;
    int chunkLength = bytesRead;
    
    if (sandbox->bytesExpected_ == -1) {
      sandbox->bytesExpected_ = ((int32_t *)chunk)[0];
      sandbox->bytesRead_ = 0;
      sandbox->dispatchResult_ = "";

      chunk += sizeof(int32_t);
      chunkLength -= sizeof(int32_t);
    }

    // std::cout << getpid() << " : reading " << sandbox->bytesExpected_ << " : " << bytesRead << " : " << chunkLength << std::endl;

    std::string chunkString(chunk, chunkLength);

    sandbox->buffers_.push_back(chunkString);
    sandbox->bytesRead_ += chunkLength;

    // std::cout << getpid() << " : expected " << sandbox->bytesExpected_ << " : got " << sandbox->bytesRead_ << std::endl;

    if (sandbox->bytesRead_ == sandbox->bytesExpected_) {
      for (const auto &chunk : sandbox->buffers_) {
        sandbox->dispatchResult_ += chunk;
      }

      uv_read_stop(pipe);
      uv_close((uv_handle_t *)pipe, OnClose);
    }
  }
  else if (bytesRead < 0) {
    uv_close((uv_handle_t *)pipe, OnClose);
  }

  free(buffer->base);
}

void SandboxWrap::OnClose(uv_handle_t *pipe) {
  free(pipe);
}

void SandboxWrap::WriteData(uv_stream_t *pipe, std::string &message) {
  uv_write_t *write = (uv_write_t *) malloc(sizeof(uv_write_t));

  char *data = (char *)malloc(message.length());

  // std::cout << getpid() << " : writing " << message.length() << std::endl;

  memcpy((void *)data, message.c_str(), message.length());

  uv_buf_t buffers[] = {
    { .base = data, .len = message.length() }
  };

  write->data = data;

  uv_write(write, pipe, buffers, 1, OnWriteComplete);
}

void SandboxWrap::OnWriteComplete(uv_write_t *request, int status) {
  assert(status == 0);

  free(request->data);
  free(request);
}