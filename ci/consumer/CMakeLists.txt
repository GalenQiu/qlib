# A downstream consumer of an installed qlib, built by the `package` job in
# .github/workflows/ci.yml and by ci/check.sh. See main.c for why it is checked
# in rather than generated.
cmake_minimum_required(VERSION 3.16)

project(qlib_consumer
    LANGUAGES C
    VERSION 0.3.0
)

find_package(qlib 0.3 REQUIRED)

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE qlib::qlib)
