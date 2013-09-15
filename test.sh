
# Tup with CONFIG_LUPPP_BUILD_TESTS=y

# Compile with:
# define BUILD_TESTS
# define BUILD_COVERAGE_TEST
# -lgcov

# run the tests:
#  Luppp quits after tests are finished
#  gcov scrapes .gcna / .gcdo files, produces .gcov files
#  cp source files into dir: needed for analysis by lcov
#  lcov scrapes .gcov files into lcov.info
#  genhtml produces index.html from lcov.info

set -e

rm -rf buildTest/src/

tup upd buildTest/

cd buildTest/

./luppp

cd src

gcov -r -b *
cp -r ../../src/* ./
lcov --directory . --capture --output-file lcov.info
genhtml lcov.info

firefox index.html
