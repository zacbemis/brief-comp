cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_native.cmake")

# This is an unfenced reference sheet, not one runnable source file. Preserve
# and list ALL bytes; do not extract functions or mock User/Json/collection APIs.
native_begin(lang-ex-base 0 ae2940e8e43e55c65f36dff14681d56d3827868b37259cdf2391f627baf98cec)
native_raw_listing("Whole mixed reference: notation fragments, incomplete bindings, duplicate alternatives, missing names/APIs, N4/N5 syntax, const, lambdas, collections, traits/modules and N2/N3 policy; no native acceptance or extraction")

# Independent complete examples. Expectations are committed baseline data,
# never captured from the compiler under test. These are NOT raw-sheet checks.
native_example(bank bd94cee25ecbbeca8619043e8d591f53ef206943a08a8e2c2b0986770761510b)
native_example(calendar 5563b9a7e7caa29205e9e1e13de3b6ea1c75389d343004e75898f6e068ecbd14)
native_example(life f783e89c19567e8a1ea187bc0b60ff0a30b6d9a856efae1d36d0d55ff30ea93d)
native_example(loan 4952df5d325a1ab04bb914c0c0bae3a2bb58e7d894c151ef05c1ea0fa5a05570)
native_example(numbers 48ca793f36f323b14c96f8477e84792304ea47ba116f52982af7eb7acf320951)
native_example(symbolic 3e709c8c8a830a859a3560e5c55bdc852f6501e26c00484fd147ab2954410a94)

native_finish(6 0 1 0 6)
native_execute()
