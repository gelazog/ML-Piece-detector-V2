#pragma once

#include <cassert>
#include <string>
#include <utility>
#include <variant>

namespace pci::core {

struct Error {
    std::string message;
};

// Resultado explícito para fronteras de módulo: obliga a decidir el caso de
// error en el llamador en lugar de propagar excepciones entre capas.
template <typename T>
class Result {
public:
    static Result ok(T value) { return Result(std::move(value)); }
    static Result err(std::string message) { return Result(Error{std::move(message)}); }

    [[nodiscard]] bool isOk() const { return std::holds_alternative<T>(data_); }

    [[nodiscard]] T& value() {
        assert(isOk());
        return std::get<T>(data_);
    }
    [[nodiscard]] const T& value() const {
        assert(isOk());
        return std::get<T>(data_);
    }
    [[nodiscard]] const Error& error() const {
        assert(!isOk());
        return std::get<Error>(data_);
    }

private:
    explicit Result(T value) : data_(std::move(value)) {}
    explicit Result(Error error) : data_(std::move(error)) {}

    std::variant<T, Error> data_;
};

template <>
class Result<void> {
public:
    static Result ok() { return Result(true, {}); }
    static Result err(std::string message) { return Result(false, Error{std::move(message)}); }

    [[nodiscard]] bool isOk() const { return ok_; }
    [[nodiscard]] const Error& error() const {
        assert(!ok_);
        return error_;
    }

private:
    Result(bool ok, Error error) : ok_(ok), error_(std::move(error)) {}

    bool ok_;
    Error error_;
};

}  // namespace pci::core
