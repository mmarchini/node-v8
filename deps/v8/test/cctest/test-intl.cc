// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef V8_INTL_SUPPORT

#include "src/builtins/builtins-intl.h"
#include "src/lookup.h"
#include "src/objects-inl.h"
#include "src/objects/intl-objects.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

// This operator overloading enables CHECK_EQ to be used with
// std::vector<NumberFormatSpan>
bool operator==(const NumberFormatSpan& lhs, const NumberFormatSpan& rhs) {
  return memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
}
template <typename _CharT, typename _Traits, typename T>
std::basic_ostream<_CharT, _Traits>& operator<<(
    std::basic_ostream<_CharT, _Traits>& self, const std::vector<T>& v) {
  self << "[";
  for (auto it = v.begin(); it < v.end();) {
    self << *it;
    it++;
    if (it < v.end()) self << ", ";
  }
  return self << "]";
}
template <typename _CharT, typename _Traits>
std::basic_ostream<_CharT, _Traits>& operator<<(
    std::basic_ostream<_CharT, _Traits>& self, const NumberFormatSpan& part) {
  return self << "{" << part.field_id << "," << part.begin_pos << ","
              << part.end_pos << "}";
}

void test_flatten_regions_to_parts(
    const std::vector<NumberFormatSpan>& regions,
    const std::vector<NumberFormatSpan>& expected_parts) {
  std::vector<NumberFormatSpan> mutable_regions = regions;
  std::vector<NumberFormatSpan> parts = FlattenRegionsToParts(&mutable_regions);
  CHECK_EQ(expected_parts, parts);
}

TEST(FlattenRegionsToParts) {
  test_flatten_regions_to_parts(
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(-1, 0, 10), NumberFormatSpan(1, 2, 8),
          NumberFormatSpan(2, 2, 4), NumberFormatSpan(3, 6, 8),
      },
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(-1, 0, 2), NumberFormatSpan(2, 2, 4),
          NumberFormatSpan(1, 4, 6), NumberFormatSpan(3, 6, 8),
          NumberFormatSpan(-1, 8, 10),
      });
  test_flatten_regions_to_parts(
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(0, 0, 1),
      },
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(0, 0, 1),
      });
  test_flatten_regions_to_parts(
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(-1, 0, 1), NumberFormatSpan(0, 0, 1),
      },
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(0, 0, 1),
      });
  test_flatten_regions_to_parts(
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(0, 0, 1), NumberFormatSpan(-1, 0, 1),
      },
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(0, 0, 1),
      });
  test_flatten_regions_to_parts(
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(-1, 0, 10), NumberFormatSpan(1, 0, 1),
          NumberFormatSpan(2, 0, 2), NumberFormatSpan(3, 0, 3),
          NumberFormatSpan(4, 0, 4), NumberFormatSpan(5, 0, 5),
          NumberFormatSpan(15, 5, 10), NumberFormatSpan(16, 6, 10),
          NumberFormatSpan(17, 7, 10), NumberFormatSpan(18, 8, 10),
          NumberFormatSpan(19, 9, 10),
      },
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(1, 0, 1), NumberFormatSpan(2, 1, 2),
          NumberFormatSpan(3, 2, 3), NumberFormatSpan(4, 3, 4),
          NumberFormatSpan(5, 4, 5), NumberFormatSpan(15, 5, 6),
          NumberFormatSpan(16, 6, 7), NumberFormatSpan(17, 7, 8),
          NumberFormatSpan(18, 8, 9), NumberFormatSpan(19, 9, 10),
      });

  //              :          4
  //              :      22 33    3
  //              :      11111   22
  // input regions:     0000000  111
  //              :     ------------
  // output parts:      0221340--231
  test_flatten_regions_to_parts(
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(-1, 0, 12), NumberFormatSpan(0, 0, 7),
          NumberFormatSpan(1, 9, 12), NumberFormatSpan(1, 1, 6),
          NumberFormatSpan(2, 9, 11), NumberFormatSpan(2, 1, 3),
          NumberFormatSpan(3, 10, 11), NumberFormatSpan(3, 4, 6),
          NumberFormatSpan(4, 5, 6),
      },
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(0, 0, 1), NumberFormatSpan(2, 1, 3),
          NumberFormatSpan(1, 3, 4), NumberFormatSpan(3, 4, 5),
          NumberFormatSpan(4, 5, 6), NumberFormatSpan(0, 6, 7),
          NumberFormatSpan(-1, 7, 9), NumberFormatSpan(2, 9, 10),
          NumberFormatSpan(3, 10, 11), NumberFormatSpan(1, 11, 12),
      });
}

TEST(GetOptions) {
  LocalContext env;
  Isolate* isolate = CcTest::i_isolate();
  v8::Isolate* v8_isolate = env->GetIsolate();
  v8::HandleScope handle_scope(v8_isolate);

  Handle<JSObject> options = isolate->factory()->NewJSObjectWithNullProto();
  Handle<String> key = isolate->factory()->NewStringFromAsciiChecked("foo");
  Handle<String> service =
      isolate->factory()->NewStringFromAsciiChecked("service");
  Handle<Object> undefined = isolate->factory()->undefined_value();
  Handle<FixedArray> empty_fixed_array =
      isolate->factory()->empty_fixed_array();

  // No value found
  Handle<Object> result =
      Object::GetOption(isolate, options, key, Object::OptionType::String,
                        empty_fixed_array, undefined, service)
          .ToHandleChecked();
  CHECK(result->IsUndefined(isolate));

  // Value found
  v8::internal::LookupIterator it(options, key);
  CHECK(Object::SetProperty(&it, Handle<Smi>(Smi::FromInt(42), isolate),
                            LanguageMode::kStrict,
                            AllocationMemento::MAY_BE_STORE_FROM_KEYED)
            .FromJust());
  result = Object::GetOption(isolate, options, key, Object::OptionType::String,
                             empty_fixed_array, undefined, service)
               .ToHandleChecked();
  CHECK(result->IsString());
  Handle<String> expected_str =
      isolate->factory()->NewStringFromAsciiChecked("42");
  CHECK(String::Equals(isolate, expected_str, Handle<String>::cast(result)));

  result = Object::GetOption(isolate, options, key, Object::OptionType::Boolean,
                             empty_fixed_array, undefined, service)
               .ToHandleChecked();
  CHECK(result->IsBoolean());
  CHECK(result->IsTrue(isolate));

  // No expected value in values array
  Handle<FixedArray> values = isolate->factory()->NewFixedArray(1);
  {
    CHECK(!Object::GetOption(isolate, options, key, Object::OptionType::String,
                             values, undefined, service)
               .ToHandle(&result));
    CHECK(isolate->has_pending_exception());
    isolate->clear_pending_exception();
  }

  // Add expected value to values array
  values->set(0, *expected_str);
  result = Object::GetOption(isolate, options, key, Object::OptionType::String,
                             values, undefined, service)
               .ToHandleChecked();
  CHECK(result->IsString());
  CHECK(String::Equals(isolate, expected_str, Handle<String>::cast(result)));

  // Test boolean values
  CHECK(Object::SetProperty(&it, isolate->factory()->ToBoolean(false),
                            LanguageMode::kStrict,
                            AllocationMemento::MAY_BE_STORE_FROM_KEYED)
            .FromJust());
  result = Object::GetOption(isolate, options, key, Object::OptionType::String,
                             empty_fixed_array, undefined, service)
               .ToHandleChecked();
  CHECK(result->IsString());

  result = Object::GetOption(isolate, options, key, Object::OptionType::Boolean,
                             empty_fixed_array, undefined, service)
               .ToHandleChecked();
  CHECK(result->IsBoolean());
  CHECK(result->IsFalse(isolate));
}

bool ScriptTagWasRemoved(std::string locale, std::string expected) {
  std::string without_script_tag;
  bool didShorten =
      IntlUtil::RemoveLocaleScriptTag(locale, &without_script_tag);
  return didShorten && expected == without_script_tag;
}

bool ScriptTagWasNotRemoved(std::string locale) {
  std::string without_script_tag;
  bool didShorten =
      IntlUtil::RemoveLocaleScriptTag(locale, &without_script_tag);
  return !didShorten && without_script_tag.empty();
}

TEST(RemoveLocaleScriptTag) {
  CHECK(ScriptTagWasRemoved("aa_Bbbb_CC", "aa_CC"));
  CHECK(ScriptTagWasRemoved("aaa_Bbbb_CC", "aaa_CC"));

  CHECK(ScriptTagWasNotRemoved("aa"));
  CHECK(ScriptTagWasNotRemoved("aaa"));
  CHECK(ScriptTagWasNotRemoved("aa_CC"));
  CHECK(ScriptTagWasNotRemoved("aa_Bbb_CC"));
  CHECK(ScriptTagWasNotRemoved("aa_1bbb_CC"));
}

TEST(GetAvailableLocales) {
  std::set<std::string> locales;

  locales = IntlUtil::GetAvailableLocales(IcuService::kBreakIterator);
  CHECK(locales.count("en-US"));
  CHECK(!locales.count("abcdefg"));

  locales = IntlUtil::GetAvailableLocales(IcuService::kCollator);
  CHECK(locales.count("en-US"));

  locales = IntlUtil::GetAvailableLocales(IcuService::kDateFormat);
  CHECK(locales.count("en-US"));

  locales = IntlUtil::GetAvailableLocales(IcuService::kNumberFormat);
  CHECK(locales.count("en-US"));

  locales = IntlUtil::GetAvailableLocales(IcuService::kPluralRules);
  CHECK(locales.count("en-US"));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_INTL_SUPPORT
