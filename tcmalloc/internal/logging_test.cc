// Copyright 2019 The TCMalloc Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tcmalloc/internal/logging.h"

#include <stdint.h>
#include <string.h>

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace tcmalloc {
namespace tcmalloc_internal {
namespace {

static std::string* log_buffer;

static void RecordLogMessage(const char* msg, int length) {
  // Make tests less brittle by trimming trailing whitespace
  while (length > 0 && (msg[length - 1] == ' ' || msg[length - 1] == '\n')) {
    length--;
  }
  log_buffer->assign(msg, length);
}

TEST(InternalLogging, MessageFormatting) {
  std::string long_string;
  for (int i = 0; i < 100; i++) {
    long_string += "the quick brown fox jumped over the lazy dog";
  }

  // Arrange to intercept Log() output
  log_buffer = new std::string();
  void (*old_writer)(const char*, int) = log_message_writer;
  log_message_writer = RecordLogMessage;

  Log(kLog, "foo.cc", 100, "Hello");
  EXPECT_EQ("foo.cc:100] Hello", *log_buffer);

  Log(kLog, "foo.cc", 100, 123u, -456, 0);
  EXPECT_EQ("foo.cc:100] 123 -456 0", *log_buffer);

  Log(kLog, "foo.cc", 100, 123u, std::numeric_limits<int64_t>::min());
  EXPECT_EQ("foo.cc:100] 123 -9223372036854775808", *log_buffer);

  Log(kLog, "foo.cc", 2,
      reinterpret_cast<const void*>(static_cast<uintptr_t>(1025)));
  EXPECT_EQ("foo.cc:2] 0x401", *log_buffer);

  Log(kLog, "foo.cc", 100, 1, 2, 3, 4, 5, 6);
  EXPECT_EQ("foo.cc:100] 1 2 3 4 5 6", *log_buffer);

  Log(kLog, "foo.cc", 10, "hello", long_string.c_str());
  EXPECT_THAT(*log_buffer,
              testing::StartsWith(
                  "foo.cc:10] hello the quick brown fox jumped over the lazy "
                  "dogthe quick brown fox jumped over the lazy dog"));

  // Check string_view, including case where not null terminated.
  const char* some_text = "a b c d e";
  absl::string_view strv1(some_text);
  absl::string_view strv2(some_text, 3);
  Log(kLog, "foo.cc", 11, strv1, strv2, some_text);
  EXPECT_NE(strv2.data()[3], 0);
  EXPECT_EQ(*log_buffer,
            absl::StrCat("foo.cc:11] ", strv1, " ", strv2, " ", some_text));

  Log(kLogWithStack, "foo.cc", 10, "stk");
  EXPECT_TRUE(strstr(log_buffer->c_str(), "stk @ 0x") != nullptr)
      << *log_buffer;

  log_message_writer = old_writer;
  delete log_buffer;
}

TEST(InternalLogging, Crash) {
  EXPECT_DEATH(Crash(kCrash, "foo.cc", 100, "a", "b", "c", "d", "e", "f"),
               "foo.cc:100] a b c d e f");
}

TEST(Printer, RequiredSpace) {
  const char kChunk[] = "0123456789";
  std::string expected;

  for (int i = 0; i < 10; i++) {
    int length = strlen(kChunk) * i + 1;
    std::unique_ptr<char[]> buf(new char[length]);
    Printer printer(buf.get(), length);

    for (int j = 0; j < i; j++) {
      printer.printf("%s", kChunk);
    }
    EXPECT_EQ(buf.get(), expected);
    EXPECT_EQ(length - 1, printer.SpaceRequired());

    // Go past the end of the buffer.  This should not overrun or affect the
    // existing contents of buf, but we should see SpaceRequired tick up.
    printer.printf("%s", kChunk);
    EXPECT_EQ(buf.get(), expected);
    EXPECT_EQ(length - 1 + strlen(kChunk), printer.SpaceRequired());

    expected.append(kChunk);
  }
}

TEST(Check, OK) {
  TC_CHECK(true);
  TC_CHECK_EQ(1, 1);
  TC_CHECK_NE(1, 2);
  TC_CHECK_GT(2, 1);
  TC_CHECK_GE(2, 1);
  TC_CHECK_GE(2, 2);
  TC_CHECK_LT(1, 2);
  TC_CHECK_LE(-1, 1);
  TC_CHECK_LE(2, 2);

  void* ptr1 = &ptr1;
  void** ptr2 = &ptr1;
  TC_CHECK_EQ(ptr1, ptr2);
  TC_CHECK_NE(ptr1, nullptr);

  TC_ASSERT(true);
  TC_ASSERT_EQ(1, 1);
  TC_ASSERT_NE(1, 2);
  TC_ASSERT_GT(2, 1);
  TC_ASSERT_GE(2, 1);
  TC_ASSERT_GE(2, 2);
  TC_ASSERT_LT(1, 2);
  TC_ASSERT_LE(-1, 1);
  TC_ASSERT_LE(2, 2);

  ABSL_ATTRIBUTE_UNUSED int unused[] = {
      (TC_CHECK(true), 1),
      (TC_CHECK_EQ(1, 1), 2),
      (TC_ASSERT(true), 3),
      (TC_ASSERT_EQ(1, 1), 4),
  };
}

TEST(Check, UnusedVars) {
  int a = 1, b = 1;
  TC_ASSERT_EQ(a, b);
}

TEST(Check, DebugCheck) {
  int eval1 = 0, eval2 = 0;
  TC_CHECK_EQ([&]() { return ++eval1; }(), [&]() { return ++eval2; }());
  ASSERT_EQ(eval1, 1);
  ASSERT_EQ(eval2, 1);
}

TEST(Check, DebugAssert) {
  int eval1 = 0, eval2 = 0;
  TC_ASSERT_EQ([&]() { return ++eval1; }(), [&]() { return ++eval2; }(),
               "val=%d", 1);
#ifdef NDEBUG
  ASSERT_EQ(eval1, 0);
  ASSERT_EQ(eval2, 0);
#else
  ASSERT_EQ(eval1, 1);
  ASSERT_EQ(eval2, 1);
#endif
}

TEST(Check, Message) {
  bool my_false = false;
  EXPECT_DEATH(TC_CHECK(my_false, "ptr=%p foo=%d str=%s", &my_false, 42, "bar"),
               "[0-9]+ .*tcmalloc\\/internal/logging_test\\.cc:[0-9]+: CHECK "
               "in TestBody: my_false "
               "\\(false\\) ptr=0x[0-9a-f]+ foo=42 str=bar");

  int x = -1, y = 1;
  EXPECT_DEATH(TC_CHECK_GE(x, y),
               "[0-9]+ .*tcmalloc\\/internal/logging_test\\.cc:[0-9]+: CHECK "
               "in TestBody: x >= y \\(-1 >= 1\\)");

  int64_t a = -1, b = 1;
  EXPECT_DEATH(TC_CHECK_EQ(a, b, "ptr=%p foo=%d str=%s", &my_false, 42, "bar"),
               "[0-9]+ .*tcmalloc\\/internal/logging_test\\.cc:[0-9]+: CHECK "
               "in TestBody: a == b \\(-1 "
               "== 1\\) ptr=0x[0-9a-f]+ foo=42 str=bar");

  enum class Something : unsigned {
    kFoo = 1,
    kBar = 2,
  };
  auto bar = []() { return Something::kBar; };
  EXPECT_DEATH(TC_CHECK_EQ(bar(), Something::kFoo),
               "bar\\(\\) == Something::kFoo \\(2 == 1\\)");

  EXPECT_DEATH(TC_BUG("bad: foo=%d bar=%s", 42, "str"),
               "[0-9]+ .*tcmalloc\\/internal/logging_test\\.cc:[0-9]+: CHECK "
               "in TestBody: bad: foo=42 bar=str");

  int s = 1;
  // Ensure %s in the expression won't confuse the macro.
  // clang-format off
  EXPECT_DEATH(TC_CHECK_EQ(0%s, 1), "0%s == 1 \\(0 == 1\\)");
  TC_ASSERT_NE(0%s, 1);
  // clang-format on

#ifndef NDEBUG
  EXPECT_DEATH(TC_ASSERT(false, "foo=%d", 42), "false \\(false\\) foo=42");
#endif
}

TEST(Check, DoubleEvaluation) {
  int eval1 = 0, eval2 = 0;
  auto f1 = [&]() { return ++eval1; };
  auto f2 = [&]() { return ++eval2; };
  EXPECT_DEATH(TC_CHECK_NE(f1(), f2()),
               "CHECK in TestBody: f1\\(\\) != f2\\(\\) \\(1 != 1\\)");
}

TEST(Check, Optional) {
  std::optional<int> opt1(1);
  std::optional<int> opt2(2);
  std::optional<int> noopt;
  TC_CHECK_EQ(opt1, opt1);
  TC_CHECK_NE(opt1, opt2);
  TC_CHECK_NE(opt1, noopt);
  TC_CHECK_NE(noopt, 1);
  EXPECT_DEATH(TC_CHECK_EQ(opt1, opt2), "opt1 == opt2 \\(1 == 2\\)");
  EXPECT_DEATH(TC_CHECK_EQ(opt1, noopt), "opt1 == noopt \\(1 == \\?\\?\\?\\)");
}

}  // namespace
}  // namespace tcmalloc_internal
}  // namespace tcmalloc
