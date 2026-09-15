#pragma once
#include <string>
#include <utility>
#include <variant>

namespace notes::application {

enum class ErrorKind {
  NotFound,
  ValidationFailed,
  StorageFailure,
  RevisionConflict
};

struct Error {
  ErrorKind kind;
  std::string message;
};

template <typename T>
class Result {
public:
  static Result ok(T value) { return Result{std::move(value)}; }
  static Result fail(Error err) { return Result{std::move(err)}; }

  [[nodiscard]] bool has_value() const noexcept { return std::holds_alternative<T>(data_); }
  [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

  [[nodiscard]] T& value() & { return std::get<T>(data_); }
  [[nodiscard]] const T& value() const& { return std::get<T>(data_); }
  [[nodiscard]] T&& value() && { return std::get<T>(std::move(data_)); }

  [[nodiscard]] const Error& error() const { return std::get<Error>(data_); }

private:
  explicit Result(T value) : data_(std::move(value)) {}
  explicit Result(Error err) : data_(std::move(err)) {}
  std::variant<T, Error> data_;
};

template <>
class Result<void> {
public:
  static Result ok() { return Result{true}; }
  static Result fail(Error err) { return Result{std::move(err)}; }

  [[nodiscard]] bool has_value() const noexcept { return ok_; }
  [[nodiscard]] explicit operator bool() const noexcept { return ok_; }
  [[nodiscard]] const Error& error() const { return error_; }

private:
  explicit Result(bool) : ok_(true) {}
  explicit Result(Error err) : ok_(false), error_(std::move(err)) {}
  bool ok_{false};
  Error error_{};
};

}  // namespace notes::application
